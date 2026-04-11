// ROV Logger for Arduino Mega with LCD, SD, GPS, Ultrasonic, Temp Sensor, and IMU
// GPS time is logged and displayed in UTC
/*
* Wiring Reference (Arduino Mega):
* --------------------------------
* GPS Module               -> RX1 (pin 19), TX1 (pin 18)
* Ultrasonic Sensor        -> RX2 (pin 17), TX2 (pin 16)
* SD Card CS               -> Pin 53
* Temperature Sensor       -> Pin 6 (1-Wire Data)
* I2C LCD Display          -> SDA (pin 20), SCL (pin 21)
* BNO085 IMU Breakout      -> SDA (pin 20), SCL (pin 21), 5V, GND
*/

#include <TinyGPS++.h>
#include <SD.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_BNO08x.h>
#include <string.h>

// ---------------- Hardware Pin Definitions ----------------
#define GPS_RX    19
#define GPS_TX    18
#define ULTRA_RX  17
#define ULTRA_TX  16
#define SD_CS     53
#define TEMP_PIN  6

// ---------------- Display and Sensor Setup ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
TinyGPSPlus gps;
HardwareSerial &gpsSerial = Serial1;
HardwareSerial &ultraSerial = Serial2;
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// BNO085 / BNO08x
Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t imuSensorValue;

struct EulerAngles {
  float yaw;
  float pitch;
  float roll;
};

EulerAngles ypr;

// ---------------- Files / Logging ----------------
File logFile;
File mapFile;
String logFilename = "";
String mapFilename = "";

bool sdReady = false;
bool mapReady = false;
bool isTempSensorAvailable = false;

// ---------------- Main Data State ----------------
float speed = NAN;
float depthCm = NAN;
float tempC = NAN;

uint32_t logRowCount = 0;
uint32_t mapPointCount = 0;
uint16_t sdRecoveryCount = 0;
int sessionNumber = -1;

// ---------------- GPS / Fix Tracking ----------------
unsigned long startupMillis = 0;
bool firstFixCaptured = false;
unsigned long firstFixTimeMs = 0;

// ---------------- Timing ----------------
unsigned long lastLog = 0;
const unsigned long logInterval = 1000;

unsigned long lastTemp = 0;
const unsigned long tempInterval = 5000;

unsigned long tempRequestTime = 0;
const unsigned long tempConversionDelay = 200;   // good for DS18B20 at 10-bit
bool tempConversionInProgress = false;

unsigned long lastScreenSwitch = 0;
const unsigned long screenInterval = 6000;
uint8_t screenIndex = 0;

unsigned long lastLcdUpdate = 0;
const unsigned long lcdUpdateInterval = 250;

unsigned long lastGpsCharMillis = 0;
const unsigned long gpsDataTimeout = 5000;

unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 500;
bool heartbeatState = false;

unsigned long lastSdRecoveryAttempt = 0;
const unsigned long sdRecoveryInterval = 3000;

// ---------------- Ultrasonic Timing / Smoothing ----------------
unsigned long lastUltrasonicTrigger = 0;
const unsigned long ultraTriggerInterval = 200;
const unsigned long ultraResponseDelay = 80;
bool awaitingUltrasonic = false;

unsigned long lastValidDepthMillis = 0;
const unsigned long depthTimeout = 2000;

#define DEPTH_BUFFER_SIZE 5
float depthReadingsBuffer[DEPTH_BUFFER_SIZE] = {0};
uint8_t depthBufferIndex = 0;
float depthReadingsTotal = 0;
uint8_t depthSamplesCollected = 0;

// ---------------- IMU / Mapping ----------------
bool imuReady = false;
bool imuOrientationValid = false;
float imuYawDeg = NAN;
float imuPitchDeg = NAN;
float imuRollDeg = NAN;
uint8_t imuAccuracy = 0;
unsigned long lastImuMillis = 0;
const unsigned long imuTimeout = 2000;

const sh2_SensorId_t imuReportType = SH2_ARVR_STABILIZED_RV;
const long imuReportIntervalUs = 50000; // 20Hz

const float maxMapHdop = 2.5f;
const uint8_t minMapSats = 5;
const float maxMapSpeedKmph = 8.0f;
const unsigned long maxMapFixAgeMs = 2000;
const float minMapSpacingMeters = 2.0f;

const bool requireLevelForMapping = true;
const float maxMapPitchDeg = 10.0f;
const float maxMapRollDeg = 10.0f;
const uint8_t minImuAccuracyForMapping = 2;

double lastMapLat = 0.0;
double lastMapLng = 0.0;
bool hasLastMapPoint = false;

// ---------------- Sonar Sound-Speed Compensation ----------------
// Temperature-only correction. No salinity input required.
const float sonarCalSoundSpeed = 1500.0f;  // baseline calibration speed
const float sonarCalTempC = 20.0f;         // calibration water temperature

// ---------------- Helper Functions ----------------

void trimLeadingSpaces(char *s) {
  while (*s == ' ') {
    memmove(s, s + 1, strlen(s));
  }
}

void formatFloatValue(float value, uint8_t decimals, char *out, size_t outSize) {
  if (isnan(value)) {
    out[0] = '\0';
    return;
  }

  char temp[20];
  dtostrf(value, 0, decimals, temp);
  trimLeadingSpaces(temp);
  strncpy(out, temp, outSize - 1);
  out[outSize - 1] = '\0';
}

void formatDateUTC(char *out, size_t outSize) {
  if (gps.date.isValid()) {
    snprintf(out, outSize, "%02d/%02d/%04d",
             gps.date.month(), gps.date.day(), gps.date.year());
  } else {
    strncpy(out, "--/--/----", outSize - 1);
    out[outSize - 1] = '\0';
  }
}

void formatTimeUTC(char *out, size_t outSize) {
  if (gps.time.isValid()) {
    snprintf(out, outSize, "%02d:%02d:%02d",
             gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    strncpy(out, "--:--:--", outSize - 1);
    out[outSize - 1] = '\0';
  }
}

void lcdPrintLine(uint8_t row, const char *text) {
  char line[17];
  memset(line, ' ', 16);
  line[16] = '\0';

  size_t len = strlen(text);
  if (len > 16) len = 16;
  memcpy(line, text, len);

  lcd.setCursor(0, row);
  lcd.print(line);
}

bool gpsDataPresent(unsigned long currentMillis) {
  return (currentMillis - lastGpsCharMillis) <= gpsDataTimeout;
}

const char* getGpsStatus(unsigned long currentMillis) {
  if (!gpsDataPresent(currentMillis)) return "NODATA";
  if (!gps.location.isValid()) return "NOFIX";
  return "OK";
}

void clearDepthBuffer() {
  for (uint8_t i = 0; i < DEPTH_BUFFER_SIZE; i++) {
    depthReadingsBuffer[i] = 0.0f;
  }
  depthBufferIndex = 0;
  depthReadingsTotal = 0.0f;
  depthSamplesCollected = 0;
}

void addDepthSample(float sample) {
  depthReadingsTotal -= depthReadingsBuffer[depthBufferIndex];
  depthReadingsBuffer[depthBufferIndex] = sample;
  depthReadingsTotal += sample;
  depthBufferIndex = (depthBufferIndex + 1) % DEPTH_BUFFER_SIZE;

  if (depthSamplesCollected < DEPTH_BUFFER_SIZE) {
    depthSamplesCollected++;
  }

  depthCm = depthReadingsTotal / depthSamplesCollected;
}

float estimateSoundSpeed(float waterTempC) {
  float c = sonarCalSoundSpeed;

  if (!isnan(waterTempC)) {
    c += 4.0f * (waterTempC - sonarCalTempC);
  }

  return c;
}

float correctSonarRangeCm(float reportedRangeCm, float waterTempC) {
  float cActual = estimateSoundSpeed(waterTempC);
  return reportedRangeCm * (cActual / sonarCalSoundSpeed);
}

// ---------------- BNO08x Helper Functions ----------------

void quaternionToEuler(float qr, float qi, float qj, float qk, EulerAngles *angles, bool degrees = false) {
  float sqr = sq(qr);
  float sqi = sq(qi);
  float sqj = sq(qj);
  float sqk = sq(qk);

  angles->yaw   = atan2(2.0f * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
  angles->pitch = asin(-2.0f * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
  angles->roll  = atan2(2.0f * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

  if (degrees) {
    angles->yaw   *= RAD_TO_DEG;
    angles->pitch *= RAD_TO_DEG;
    angles->roll  *= RAD_TO_DEG;
  }
}

void quaternionToEulerRV(sh2_RotationVectorWAcc_t *rotational_vector, EulerAngles *angles, bool degrees = false) {
  quaternionToEuler(rotational_vector->real,
                    rotational_vector->i,
                    rotational_vector->j,
                    rotational_vector->k,
                    angles,
                    degrees);
}

void setImuReports() {
  if (!bno08x.enableReport(imuReportType, imuReportIntervalUs)) {
    Serial.println(F("[IMU] Could not enable stabilized rotation vector report."));
  }
}

void updateImu(unsigned long currentMillis) {
  if (!imuReady) return;

  if (bno08x.wasReset()) {
    Serial.println(F("[IMU] Sensor reset detected. Re-enabling reports."));
    setImuReports();
  }

  while (bno08x.getSensorEvent(&imuSensorValue)) {
    if (imuSensorValue.sensorId == SH2_ARVR_STABILIZED_RV) {
      quaternionToEulerRV(&imuSensorValue.un.arvrStabilizedRV, &ypr, true);
      imuYawDeg = ypr.yaw;
      imuPitchDeg = ypr.pitch;
      imuRollDeg = ypr.roll;
      imuAccuracy = imuSensorValue.status;
      imuOrientationValid = true;
      lastImuMillis = currentMillis;
    }
  }

  if (imuOrientationValid && (currentMillis - lastImuMillis > imuTimeout)) {
    imuOrientationValid = false;
    imuYawDeg = NAN;
    imuPitchDeg = NAN;
    imuRollDeg = NAN;
    imuAccuracy = 0;
  }
}

// ---------------- SD / File Functions ----------------

bool openNextLogFile() {
  char filename[15];
  strcpy(filename, "data00.csv");

  for (uint8_t i = 0; i < 100; i++) {
    filename[4] = '0' + i / 10;
    filename[5] = '0' + i % 10;

    if (!SD.exists(filename)) {
      logFilename = String(filename);
      sessionNumber = i;
      logFile = SD.open(filename, FILE_WRITE);

      if (logFile) {
        logFile.println(F("date_utc,time_utc,lat,lat_dir,lng,lng_dir,alt_m,depth_cm,temp_c,satellites,hdop,speed_kmph"));
        logFile.flush();
        return true;
      }
      return false;
    }
  }

  return false;
}

bool openMapFileForSession() {
  if (sessionNumber < 0) return false;

  char filename[15];
  snprintf(filename, sizeof(filename), "map%02d.csv", sessionNumber);

  if (SD.exists(filename)) {
    SD.remove(filename);
  }

  mapFilename = String(filename);
  mapFile = SD.open(filename, FILE_WRITE);

  if (!mapFile) return false;

  mapFile.println(F("point,date_utc,time_utc,lat,lng,depth_cm,temp_c,satellites,hdop,speed_kmph,fix_age_ms,pitch_deg,roll_deg,imu_acc"));
  mapFile.flush();
  return true;
}

bool attemptSdRecovery(unsigned long currentMillis) {
  if (logFilename.length() == 0) return false;
  if (currentMillis - lastSdRecoveryAttempt < sdRecoveryInterval) return false;

  lastSdRecoveryAttempt = currentMillis;

  Serial.println(F("[SD] Attempting recovery..."));

  if (logFile) logFile.close();
  if (mapFile) mapFile.close();

  if (!SD.begin(SD_CS)) {
    sdReady = false;
    mapReady = false;
    Serial.println(F("[SD] Recovery failed at SD.begin()."));
    return false;
  }

  logFile = SD.open(logFilename.c_str(), FILE_WRITE);
  if (!logFile) {
    sdReady = false;
    mapReady = false;
    Serial.println(F("[SD] Recovery failed reopening main log file."));
    return false;
  }

  sdReady = true;

  if (mapFilename.length() > 0) {
    mapFile = SD.open(mapFilename.c_str(), FILE_WRITE);
    mapReady = mapFile ? true : false;
  } else {
    mapReady = false;
  }

  sdRecoveryCount++;
  Serial.println(F("[SD] Recovery successful."));
  return true;
}

bool writeLogRow(const char* dateStr,
                 const char* timeStr,
                 const char* latStr,
                 char latDir,
                 const char* lngStr,
                 char lngDir,
                 const char* altStr,
                 const char* depthStr,
                 const char* tempStr,
                 const char* satsStr,
                 const char* hdopStr,
                 const char* speedStr) {
  if (!sdReady || !logFile) return false;

  uint32_t startSize = logFile.size();

  logFile.print(dateStr);                 logFile.print(",");
  logFile.print(timeStr);                 logFile.print(",");
  logFile.print(latStr);                  logFile.print(",");
  if (latDir) logFile.print(latDir);      logFile.print(",");
  logFile.print(lngStr);                  logFile.print(",");
  if (lngDir) logFile.print(lngDir);      logFile.print(",");
  logFile.print(altStr);                  logFile.print(",");
  logFile.print(depthStr);                logFile.print(",");
  logFile.print(tempStr);                 logFile.print(",");
  logFile.print(satsStr);                 logFile.print(",");
  logFile.print(hdopStr);                 logFile.print(",");
  logFile.println(speedStr);
  logFile.flush();

  return (logFile.size() > startSize);
}

// ---------------- Mapping Functions ----------------

const char* getMapStatus(unsigned long currentMillis) {
  if (!mapReady || !sdReady || !mapFile) return "NOFILE";
  if (!gpsDataPresent(currentMillis)) return "NOGPS";
  if (!gps.location.isValid()) return "NOFIX";
  if (!gps.date.isValid() || !gps.time.isValid()) return "NOTIME";
  if (isnan(depthCm)) return "NODEP";
  if (depthSamplesCollected < 3) return "WARM";

  if (!gps.satellites.isValid()) return "SAT";
  if (gps.satellites.value() < minMapSats) return "SAT";

  if (!gps.hdop.isValid()) return "HDOP";
  if (gps.hdop.hdop() > maxMapHdop) return "HDOP";

  if (gps.location.age() > maxMapFixAgeMs) return "STALE";

  if (gps.speed.isValid() && gps.speed.kmph() > maxMapSpeedKmph) return "FAST";

  if (requireLevelForMapping) {
    if (!imuReady) return "NOIMU";
    if (!imuOrientationValid) return "IMU?";
    if (imuAccuracy < minImuAccuracyForMapping) return "CAL";
    if (fabs(imuPitchDeg) > maxMapPitchDeg || fabs(imuRollDeg) > maxMapRollDeg) return "UNLVL";
  }

  if (hasLastMapPoint) {
    double distanceMeters = TinyGPSPlus::distanceBetween(
      gps.location.lat(), gps.location.lng(),
      lastMapLat, lastMapLng
    );
    if (distanceMeters < minMapSpacingMeters) return "HOLD";
  }

  return "ARMED";
}

bool shouldLogMapPoint(unsigned long currentMillis) {
  return strcmp(getMapStatus(currentMillis), "ARMED") == 0;
}

bool writeMapPoint() {
  if (!mapReady || !sdReady || !mapFile) return false;

  char dateStr[16];
  char timeStr[16];
  char depthStr[16] = "";
  char tempStr[16] = "";
  char hdopStr[16] = "";
  char speedStr[16] = "";
  char latStr[20] = "";
  char lngStr[20] = "";
  char satsStr[8] = "";
  char pitchStr[12] = "";
  char rollStr[12] = "";
  char imuAccStr[4] = "";

  formatDateUTC(dateStr, sizeof(dateStr));
  formatTimeUTC(timeStr, sizeof(timeStr));
  formatFloatValue(gps.location.lat(), 7, latStr, sizeof(latStr));
  formatFloatValue(gps.location.lng(), 7, lngStr, sizeof(lngStr));
  formatFloatValue(depthCm, 1, depthStr, sizeof(depthStr));

  if (!isnan(tempC)) formatFloatValue(tempC, 2, tempStr, sizeof(tempStr));
  if (gps.hdop.isValid()) formatFloatValue(gps.hdop.hdop(), 1, hdopStr, sizeof(hdopStr));
  if (gps.speed.isValid()) formatFloatValue(gps.speed.kmph(), 1, speedStr, sizeof(speedStr));
  if (gps.satellites.isValid()) snprintf(satsStr, sizeof(satsStr), "%lu", gps.satellites.value());
  if (imuOrientationValid) {
    formatFloatValue(imuPitchDeg, 1, pitchStr, sizeof(pitchStr));
    formatFloatValue(imuRollDeg, 1, rollStr, sizeof(rollStr));
    snprintf(imuAccStr, sizeof(imuAccStr), "%u", imuAccuracy);
  }

  uint32_t startSize = mapFile.size();

  mapFile.print(mapPointCount);       mapFile.print(",");
  mapFile.print(dateStr);             mapFile.print(",");
  mapFile.print(timeStr);             mapFile.print(",");
  mapFile.print(latStr);              mapFile.print(",");
  mapFile.print(lngStr);              mapFile.print(",");
  mapFile.print(depthStr);            mapFile.print(",");
  mapFile.print(tempStr);             mapFile.print(",");
  mapFile.print(satsStr);             mapFile.print(",");
  mapFile.print(hdopStr);             mapFile.print(",");
  mapFile.print(speedStr);            mapFile.print(",");
  mapFile.print(gps.location.age());  mapFile.print(",");
  mapFile.print(pitchStr);            mapFile.print(",");
  mapFile.print(rollStr);             mapFile.print(",");
  mapFile.println(imuAccStr);
  mapFile.flush();

  if (mapFile.size() <= startSize) return false;

  lastMapLat = gps.location.lat();
  lastMapLng = gps.location.lng();
  hasLastMapPoint = true;
  mapPointCount++;

  return true;
}

// ---------------- Misc ----------------

void updateFirstFix(unsigned long currentMillis) {
  if (!firstFixCaptured && gps.location.isValid()) {
    firstFixCaptured = true;
    firstFixTimeMs = currentMillis - startupMillis;
    Serial.print(F("[GPS] First fix acquired in "));
    Serial.print(firstFixTimeMs / 1000.0, 1);
    Serial.println(F(" s"));
  }
}

void showStartupScreen() {
  lcd.clear();
  lcdPrintLine(0, sdReady ? "SD:OK" : "SD:ERR");
  lcdPrintLine(1, imuReady ? "IMU:OK" : "IMU:ERR");
  delay(1500);

  lcd.clear();
  if (logFilename.length() > 0) {
    char line0[17];
    char line1[17];
    snprintf(line0, sizeof(line0), "File:%s", logFilename.c_str());
    snprintf(line1, sizeof(line1), "Map:%s", mapFilename.c_str());
    lcdPrintLine(0, line0);
    lcdPrintLine(1, line1);
  } else {
    lcdPrintLine(0, "No log file");
    lcdPrintLine(1, "Check SD card");
  }
  delay(1800);

  lcd.clear();
}

// ---------------- Setup ----------------

void setup() {
  startupMillis = millis();

  Serial.begin(115200);
  delay(500);
  Serial.println(F("[ROV] Serial started."));

  Wire.begin();
  Wire.setClock(100000);

  gpsSerial.begin(9600);
  ultraSerial.begin(115200);

  tempSensor.begin();
  if (tempSensor.getDeviceCount() > 0) {
    isTempSensorAvailable = true;
    tempSensor.setResolution(10);
    tempSensor.setWaitForConversion(false);
    Serial.println(F("[TEMP] Sensor found."));
  } else {
    Serial.println(F("[TEMP] No sensor detected."));
  }

  lcd.init();
  lcd.backlight();
  lcdPrintLine(0, "Initializing...");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  lastGpsCharMillis = millis();

  if (!SD.begin(SD_CS)) {
    Serial.println(F("[SD] Init failed."));
    lcd.clear();
    lcdPrintLine(0, "SD Error");
    sdReady = false;
  } else {
    Serial.println(F("[SD] Init success."));
    sdReady = openNextLogFile();

    if (sdReady) {
      mapReady = openMapFileForSession();
      Serial.print(F("[SD] Logging to: "));
      Serial.println(logFilename);
      if (mapReady) {
        Serial.print(F("[MAP] Logging to: "));
        Serial.println(mapFilename);
      } else {
        Serial.println(F("[MAP] Could not create map file."));
      }
    } else {
      Serial.println(F("[SD] Could not create log file."));
      lcd.clear();
      lcdPrintLine(0, "No Log Filename");
      delay(1800);
    }
  }

  if (bno08x.begin_I2C()) {
    imuReady = true;
    setImuReports();
    Serial.println(F("[IMU] BNO08x found."));
  } else {
    imuReady = false;
    Serial.println(F("[IMU] BNO08x not found."));
  }

  showStartupScreen();
}

// ---------------- Main Loop ----------------

void loop() {
  unsigned long currentMillis = millis();

  // Heartbeat LED
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    lastHeartbeat = currentMillis;
    heartbeatState = !heartbeatState;
    digitalWrite(LED_BUILTIN, heartbeatState ? HIGH : LOW);
  }

  // Read GPS data
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gps.encode(c);
    lastGpsCharMillis = millis();
  }

  updateFirstFix(currentMillis);

  // Update IMU
  updateImu(currentMillis);

  // Ultrasonic trigger / read state machine
  if (!awaitingUltrasonic && (currentMillis - lastUltrasonicTrigger >= ultraTriggerInterval)) {
    ultraSerial.write(0x55);
    lastUltrasonicTrigger = currentMillis;
    awaitingUltrasonic = true;
  }

  if (awaitingUltrasonic && (currentMillis - lastUltrasonicTrigger >= ultraResponseDelay)) {
    bool validReading = false;

    while (ultraSerial.available() >= 4) {
      if (ultraSerial.peek() == 0xFF) {
        ultraSerial.read();
        byte hi = ultraSerial.read();
        byte lo = ultraSerial.read();
        byte sum = ultraSerial.read();

        if (((0xFF + hi + lo) & 0xFF) != sum) {
          break;
        }

        uint16_t raw = (hi << 8) | lo;
        if (raw == 0 || raw == 0xFFFF) {
          break;
        }

        float reportedDepthCm = raw / 10.0f;
        float correctedDepthCm = correctSonarRangeCm(reportedDepthCm, tempC);

        if (correctedDepthCm >= 2.0f && correctedDepthCm <= 800.0f) {
          addDepthSample(correctedDepthCm);
          lastValidDepthMillis = currentMillis;
          validReading = true;
        }
        break;
      } else {
        ultraSerial.read();
      }
    }

    awaitingUltrasonic = false;

    if (!validReading && (currentMillis - lastValidDepthMillis > depthTimeout)) {
      depthCm = NAN;
      clearDepthBuffer();
    }
  }

  if ((currentMillis - lastValidDepthMillis > depthTimeout) && !isnan(depthCm)) {
    depthCm = NAN;
    clearDepthBuffer();
  }

  // Temperature: non-blocking request / read
  if (isTempSensorAvailable) {
    if (!tempConversionInProgress && (currentMillis - lastTemp >= tempInterval)) {
      tempSensor.requestTemperatures();
      tempRequestTime = currentMillis;
      tempConversionInProgress = true;
      lastTemp = currentMillis;
    }

    if (tempConversionInProgress && (currentMillis - tempRequestTime >= tempConversionDelay)) {
      float t = tempSensor.getTempCByIndex(0);
      if (t > -100.0f && t < 100.0f) {
        tempC = t;
      } else {
        tempC = NAN;
      }
      tempConversionInProgress = false;
    }
  }

  // Retry SD recovery if needed
  if ((!sdReady || !logFile) && logFilename.length() > 0) {
    attemptSdRecovery(currentMillis);
  }

  // Main log output
  if (currentMillis - lastLog >= logInterval) {
    lastLog = currentMillis;

    speed = gps.speed.isValid() ? gps.speed.kmph() : NAN;
    float soundSpeedEstimate = estimateSoundSpeed(tempC);

    char dateStr[16];
    char timeStr[16];
    char latStr[20] = "";
    char lngStr[20] = "";
    char altStr[16] = "";
    char depthStr[16] = "";
    char tempStr[16] = "";
    char satsStr[8] = "";
    char hdopStr[16] = "";
    char speedStr[16] = "";

    formatDateUTC(dateStr, sizeof(dateStr));
    formatTimeUTC(timeStr, sizeof(timeStr));

    if (gps.location.isValid()) {
      formatFloatValue(gps.location.lat(), 7, latStr, sizeof(latStr));
      formatFloatValue(gps.location.lng(), 7, lngStr, sizeof(lngStr));
    }

    if (gps.altitude.isValid()) formatFloatValue(gps.altitude.meters(), 1, altStr, sizeof(altStr));
    if (!isnan(depthCm)) formatFloatValue(depthCm, 1, depthStr, sizeof(depthStr));
    if (!isnan(tempC)) formatFloatValue(tempC, 2, tempStr, sizeof(tempStr));
    if (gps.satellites.isValid()) snprintf(satsStr, sizeof(satsStr), "%lu", gps.satellites.value());
    if (gps.hdop.isValid()) formatFloatValue(gps.hdop.hdop(), 1, hdopStr, sizeof(hdopStr));
    if (!isnan(speed)) formatFloatValue(speed, 1, speedStr, sizeof(speedStr));

    const char latDir = gps.location.isValid() ? (gps.location.rawLat().negative ? 'S' : 'N') : '\0';
    const char lngDir = gps.location.isValid() ? (gps.location.rawLng().negative ? 'W' : 'E') : '\0';

    bool wroteRow = false;

    if (sdReady && logFile) {
      wroteRow = writeLogRow(dateStr, timeStr, latStr, latDir, lngStr, lngDir,
                             altStr, depthStr, tempStr, satsStr, hdopStr, speedStr);

      if (!wroteRow) {
        Serial.println(F("[SD] Write failed. Trying recovery..."));
        sdReady = false;
        attemptSdRecovery(currentMillis);
      }
    }

    if (wroteRow) {
      logRowCount++;
    }

    // Mapping output
    if (shouldLogMapPoint(currentMillis)) {
      if (!writeMapPoint()) {
        Serial.println(F("[MAP] Failed to write map point."));
      }
    }

    // Serial output
    Serial.print(F("[ROV UTC] "));
    Serial.print(dateStr);
    Serial.print(" ");
    Serial.print(timeStr);
    Serial.print(F(" | Lat: "));   Serial.print(latStr);
    Serial.print(F(" | Lng: "));   Serial.print(lngStr);
    Serial.print(F(" | Alt: "));   Serial.print(altStr);   Serial.print(F("m"));
    Serial.print(F(" | Depth: ")); Serial.print(depthStr); Serial.print(F("cm"));
    Serial.print(F(" | Temp: "));  Serial.print(tempStr);  Serial.print(F("C"));
    Serial.print(F(" | c:"));
    if (!isnan(tempC)) Serial.print(soundSpeedEstimate, 1); else Serial.print(F("--"));
    Serial.print(F("m/s"));
    Serial.print(F(" | Sats: "));  Serial.print(satsStr);
    Serial.print(F(" | HDOP: "));  Serial.print(hdopStr);
    Serial.print(F(" | Speed: ")); Serial.print(speedStr); Serial.print(F(" km/h"));
    Serial.print(F(" | P:"));
    if (imuOrientationValid) Serial.print(imuPitchDeg, 1); else Serial.print(F("--"));
    Serial.print(F(" R:"));
    if (imuOrientationValid) Serial.print(imuRollDeg, 1); else Serial.print(F("--"));
    Serial.print(F(" | IMU:")); Serial.print(imuAccuracy);
    Serial.print(F(" | Map:")); Serial.print(getMapStatus(currentMillis));
    Serial.print(F(" | Rows:")); Serial.print(logRowCount);
    Serial.print(F(" | Mpts:")); Serial.print(mapPointCount);
    Serial.print(F(" | Rc:")); Serial.println(sdRecoveryCount);
  }

  // Screen cycling
  if (currentMillis - lastScreenSwitch >= screenInterval) {
    screenIndex = (screenIndex + 1) % 6;
    lastScreenSwitch = currentMillis;
    lcd.clear();
  }

  // LCD update
  if (currentMillis - lastLcdUpdate >= lcdUpdateInterval) {
    lastLcdUpdate = currentMillis;

    switch (screenIndex) {
      case 0: {
        char line0[17];
        char line1[17];
        char dateStr[16];
        char timeStr[16];

        formatDateUTC(dateStr, sizeof(dateStr));
        formatTimeUTC(timeStr, sizeof(timeStr));

        snprintf(line0, sizeof(line0), "Date:%s", dateStr);
        snprintf(line1, sizeof(line1), "UTC:%s", timeStr);

        lcdPrintLine(0, line0);
        lcdPrintLine(1, line1);
        break;
      }

      case 1: {
        char spdMph[12] = "";
        char altFt[12] = "";
        char depthFt[12] = "";
        char tempF[12] = "";

        if (!isnan(speed)) formatFloatValue(speed * 0.621371f, 1, spdMph, sizeof(spdMph));
        if (gps.altitude.isValid()) formatFloatValue(gps.altitude.feet(), 0, altFt, sizeof(altFt));
        if (!isnan(depthCm)) formatFloatValue(depthCm / 30.48f, 1, depthFt, sizeof(depthFt));
        if (!isnan(tempC)) formatFloatValue((tempC * 1.8f) + 32.0f, 1, tempF, sizeof(tempF));

        char line0[17];
        char line1[17];

        snprintf(line0, sizeof(line0), "S:%smph A:%s",
                 spdMph[0] ? spdMph : "--",
                 altFt[0] ? altFt : "--");

        snprintf(line1, sizeof(line1), "D:%sft T:%sF",
                 depthFt[0] ? depthFt : "--",
                 tempF[0] ? tempF : "--");

        lcdPrintLine(0, line0);
        lcdPrintLine(1, line1);
        break;
      }

      case 2: {
        char line0[17];
        char line1[17];

        if (!gpsDataPresent(currentMillis)) {
          lcdPrintLine(0, "No GPS data");
          snprintf(line1, sizeof(line1), "File:%s", logFilename.c_str());
          lcdPrintLine(1, line1);
        } else if (!gps.location.isValid()) {
          lcdPrintLine(0, "Waiting GPS fix");
          snprintf(line1, sizeof(line1), "File:%s", logFilename.c_str());
          lcdPrintLine(1, line1);
        } else {
          char latBuf[12] = "";
          char lngBuf[12] = "";

          float latAbs = gps.location.lat();
          if (latAbs < 0) latAbs = -latAbs;

          float lngAbs = gps.location.lng();
          if (lngAbs < 0) lngAbs = -lngAbs;

          formatFloatValue(latAbs, 4, latBuf, sizeof(latBuf));
          formatFloatValue(lngAbs, 4, lngBuf, sizeof(lngBuf));

          snprintf(line0, sizeof(line0), "Lat:%s%c",
                   latBuf,
                   gps.location.rawLat().negative ? 'S' : 'N');

          snprintf(line1, sizeof(line1), "Lng:%s%c",
                   lngBuf,
                   gps.location.rawLng().negative ? 'W' : 'E');

          lcdPrintLine(0, line0);
          lcdPrintLine(1, line1);
        }
        break;
      }

      case 3: {
        char hdopBuf[10] = "";
        char line0[17];
        char line1[17];

        if (gps.hdop.isValid()) formatFloatValue(gps.hdop.hdop(), 1, hdopBuf, sizeof(hdopBuf));

        snprintf(line0, sizeof(line0), "Sat:%lu HD:%s",
                 gps.satellites.isValid() ? gps.satellites.value() : 0,
                 hdopBuf[0] ? hdopBuf : "--");

        snprintf(line1, sizeof(line1), "GPS:%s SD:%s",
                 getGpsStatus(currentMillis),
                 sdReady ? "OK" : "ERR");

        lcdPrintLine(0, line0);
        lcdPrintLine(1, line1);
        break;
      }

      case 4: {
        char line0[17];
        char line1[17];

        snprintf(line0, sizeof(line0), "S%02d R%lu M%lu",
                 sessionNumber >= 0 ? sessionNumber : 0,
                 (unsigned long)logRowCount,
                 (unsigned long)mapPointCount);

        if (firstFixCaptured) {
          snprintf(line1, sizeof(line1), "1Fix:%lus Rc:%u",
                   firstFixTimeMs / 1000UL,
                   sdRecoveryCount);
        } else {
          snprintf(line1, sizeof(line1), "Lock:%lus Rc:%u",
                   (currentMillis - startupMillis) / 1000UL,
                   sdRecoveryCount);
        }

        lcdPrintLine(0, line0);
        lcdPrintLine(1, line1);
        break;
      }

      case 5: {
        char pitchBuf[8] = "";
        char rollBuf[8] = "";
        char line0[17];
        char line1[17];

        if (imuOrientationValid) {
          formatFloatValue(imuPitchDeg, 1, pitchBuf, sizeof(pitchBuf));
          formatFloatValue(imuRollDeg, 1, rollBuf, sizeof(rollBuf));
        }

        snprintf(line0, sizeof(line0), "P:%s R:%s",
                 pitchBuf[0] ? pitchBuf : "--",
                 rollBuf[0] ? rollBuf : "--");

        snprintf(line1, sizeof(line1), "I:%u M:%s",
                 imuAccuracy,
                 getMapStatus(currentMillis));

        lcdPrintLine(0, line0);
        lcdPrintLine(1, line1);
        break;
      }
    }
  }
}