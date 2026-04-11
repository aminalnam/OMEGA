// ============================================================
// ROV LOGGER + DEPTH MAPPING SYSTEM (Arduino Mega)
// ============================================================
//
// PURPOSE
// -------
// This sketch turns an Arduino Mega into a compact survey logger.
// It records GPS, depth, temperature, and orientation data, then
// writes two CSV files to the SD card:
//
//   1. dataXX.csv -> broader system log
//   2. mapXX.csv  -> filtered mapping points
//
// The map file is intended for downstream visualization in Google
// Earth, contour generation, and interpolated bathymetric mapping.
//
// ============================================================
// WIRING REFERENCE (Arduino Mega)
// ============================================================
// GPS Module               -> RX1 (pin 19), TX1 (pin 18)
// Ultrasonic Sensor        -> RX2 (pin 17), TX2 (pin 16)
// SD Card CS               -> Pin 53
// Temperature Sensor       -> Pin 6 (1-Wire Data)
// I2C LCD Display          -> SDA (pin 20), SCL (pin 21)
// BNO085 IMU Breakout      -> SDA (pin 20), SCL (pin 21), 5V, GND
//
// ============================================================

#include <TinyGPS++.h>
#include <SD.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_BNO08x.h>
#include <string.h>

// ============================================================
// ---------------- HARDWARE PIN DEFINITIONS -------------------
// ============================================================

#define GPS_RX    19
#define GPS_TX    18
#define ULTRA_RX  17
#define ULTRA_TX  16
#define SD_CS     53
#define TEMP_PIN  6

// ============================================================
// ---------------- DISPLAY AND SENSOR OBJECTS -----------------
// ============================================================

// 16x2 I2C LCD at the common 0x27 address
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GPS parser and serial connections
TinyGPSPlus gps;
HardwareSerial &gpsSerial = Serial1;
HardwareSerial &ultraSerial = Serial2;

// 1-wire temperature sensor bus
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// BNO085 / BNO08x IMU
Adafruit_BNO08x bno08x(-1);
sh2_SensorValue_t imuSensorValue;

// Simple Euler-angle container used for display and filtering
struct EulerAngles {
  float yaw;
  float pitch;
  float roll;
};

EulerAngles ypr;

// ============================================================
// ---------------- FILES AND LOGGING STATE --------------------
// ============================================================

File logFile;
File mapFile;

String logFilename = "";
String mapFilename = "";

bool sdReady = false;
bool mapReady = false;
bool isTempSensorAvailable = false;

// ============================================================
// ---------------- MAIN MEASUREMENT STATE ---------------------
// ============================================================

// GPS-derived speed (smoothed)
float speed = NAN;
float smoothedSpeed = NAN;

// Depth and temperature
float depthCm = NAN;
float lastDepthCm = NAN;
float tempC = NAN;

// Counters for logging and diagnostics
uint32_t logRowCount = 0;
uint32_t mapPointCount = 0;
uint16_t sdRecoveryCount = 0;
int sessionNumber = -1;

// ============================================================
// ---------------- GPS / FIX TRACKING -------------------------
// ============================================================

unsigned long startupMillis = 0;
bool firstFixCaptured = false;
unsigned long firstFixTimeMs = 0;

// ============================================================
// ---------------- TIMING CONFIGURATION -----------------------
// ============================================================

// Main CSV logging interval
unsigned long lastLog = 0;
const unsigned long logInterval = 1000;

// Temperature polling interval
unsigned long lastTemp = 0;
const unsigned long tempInterval = 5000;

// Non-blocking DS18B20 conversion timing
unsigned long tempRequestTime = 0;
const unsigned long tempConversionDelay = 200;   // appropriate for 10-bit resolution
bool tempConversionInProgress = false;

// LCD page switching
unsigned long lastScreenSwitch = 0;
const unsigned long screenInterval = 6000;
uint8_t screenIndex = 0;

// LCD redraw rate
unsigned long lastLcdUpdate = 0;
const unsigned long lcdUpdateInterval = 250;

// Time since any GPS character was received
unsigned long lastGpsCharMillis = 0;
const unsigned long gpsDataTimeout = 5000;

// Heartbeat LED blink
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 500;
bool heartbeatState = false;

// SD recovery retry interval
unsigned long lastSdRecoveryAttempt = 0;
const unsigned long sdRecoveryInterval = 3000;

// Prevent early bad GPS data from entering the map
const unsigned long gpsWarmupTime = 10000;

// ============================================================
// ------------- ULTRASONIC TIMING AND SMOOTHING ---------------
// ============================================================

// Trigger / response timing for UART ultrasonic sensor
unsigned long lastUltrasonicTrigger = 0;
const unsigned long ultraTriggerInterval = 200;
const unsigned long ultraResponseDelay = 80;
bool awaitingUltrasonic = false;

// Timeout after which depth is treated as invalid
unsigned long lastValidDepthMillis = 0;
const unsigned long depthTimeout = 2000;

// Simple moving-average buffer for depth smoothing
#define DEPTH_BUFFER_SIZE 5
float depthReadingsBuffer[DEPTH_BUFFER_SIZE] = {0};
uint8_t depthBufferIndex = 0;
float depthReadingsTotal = 0;
uint8_t depthSamplesCollected = 0;

// Reject unrealistic jumps in depth
const float maxDepthJumpCm = 50.0f;

// ============================================================
// ------------------ IMU / MAPPING SETTINGS -------------------
// ============================================================

bool imuReady = false;
bool imuOrientationValid = false;
float imuYawDeg = NAN;
float imuPitchDeg = NAN;
float imuRollDeg = NAN;
uint8_t imuAccuracy = 0;
unsigned long lastImuMillis = 0;
const unsigned long imuTimeout = 2000;

// Use the stabilized AR/VR rotation vector
const sh2_SensorId_t imuReportType = SH2_ARVR_STABILIZED_RV;
const long imuReportIntervalUs = 50000; // 20 Hz

// Mapping-quality thresholds
const float maxMapHdop = 1.8f;
const uint8_t minMapSats = 5;
const float maxMapSpeedKmph = 8.0f;
const unsigned long maxMapFixAgeMs = 2000;
const float minMapSpacingMetersBase = 2.0f;

// Require the platform to be reasonably level for map points
const bool requireLevelForMapping = true;
const float maxMapPitchDeg = 10.0f;
const float maxMapRollDeg = 10.0f;
const uint8_t minImuAccuracyForMapping = 2;

// Track the last accepted map point to enforce spacing
double lastMapLat = 0.0;
double lastMapLng = 0.0;
bool hasLastMapPoint = false;

// Master switch for mapping points
bool mappingEnabled = true;

// ============================================================
// -------- SONAR SOUND-SPEED COMPENSATION (TEMP ONLY) ---------
// ============================================================

// Temperature-only sound-speed model.
// This avoids requiring the user to manually set salinity.
const float sonarCalSoundSpeed = 1500.0f;  // baseline reference speed (m/s)
const float sonarCalTempC = 20.0f;         // reference temperature (°C)

// ============================================================
// --------------------- HELPER FUNCTIONS ----------------------
// ============================================================

// Remove leading spaces from a C string.
// Helpful after dtostrf(), which often pads with spaces.
void trimLeadingSpaces(char *s) {
  while (*s == ' ') {
    memmove(s, s + 1, strlen(s));
  }
}

// Format a float into a compact C string.
// Returns an empty string if the value is NaN.
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

// Format GPS date as MM/DD/YYYY in UTC.
void formatDateUTC(char *out, size_t outSize) {
  if (gps.date.isValid()) {
    snprintf(out, outSize, "%02d/%02d/%04d",
             gps.date.month(), gps.date.day(), gps.date.year());
  } else {
    strncpy(out, "--/--/----", outSize - 1);
    out[outSize - 1] = '\0';
  }
}

// Format GPS time as HH:MM:SS in UTC.
void formatTimeUTC(char *out, size_t outSize) {
  if (gps.time.isValid()) {
    snprintf(out, outSize, "%02d:%02d:%02d",
             gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    strncpy(out, "--:--:--", outSize - 1);
    out[outSize - 1] = '\0';
  }
}

// Print a padded 16-character line to the LCD.
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

// Determine whether the GPS is still actively sending characters.
bool gpsDataPresent(unsigned long currentMillis) {
  return (currentMillis - lastGpsCharMillis) <= gpsDataTimeout;
}

// Human-readable GPS status used for LCD and serial diagnostics.
const char* getGpsStatus(unsigned long currentMillis) {
  if (!gpsDataPresent(currentMillis)) return "NODATA";
  if (!gps.location.isValid()) return "NOFIX";
  return "OK";
}

// Clear the moving-average depth buffer.
void clearDepthBuffer() {
  for (uint8_t i = 0; i < DEPTH_BUFFER_SIZE; i++) {
    depthReadingsBuffer[i] = 0.0f;
  }
  depthBufferIndex = 0;
  depthReadingsTotal = 0.0f;
  depthSamplesCollected = 0;
}

// Add a new depth sample to the moving-average buffer.
// Also rejects sudden unrealistic depth jumps.
void addDepthSample(float sample) {
  depthReadingsTotal -= depthReadingsBuffer[depthBufferIndex];
  depthReadingsBuffer[depthBufferIndex] = sample;
  depthReadingsTotal += sample;
  depthBufferIndex = (depthBufferIndex + 1) % DEPTH_BUFFER_SIZE;

  if (depthSamplesCollected < DEPTH_BUFFER_SIZE) {
    depthSamplesCollected++;
  }

  float newDepth = depthReadingsTotal / depthSamplesCollected;

  // Reject isolated spikes that jump too far from the previous value.
  if (!isnan(lastDepthCm) && fabs(newDepth - lastDepthCm) > maxDepthJumpCm) {
    return;
  }

  depthCm = newDepth;
  lastDepthCm = depthCm;
}

// Estimate sound speed using water temperature only.
// This is a simplified correction, but it improves results
// without requiring salinity input.
float estimateSoundSpeed(float waterTempC) {
  float c = sonarCalSoundSpeed;

  if (!isnan(waterTempC)) {
    c += 4.6f * (waterTempC - sonarCalTempC);
  }

  return c;
}

// Correct the sonar-reported distance based on estimated sound speed.
float correctSonarRangeCm(float reportedRangeCm, float waterTempC) {
  float cActual = estimateSoundSpeed(waterTempC);
  return reportedRangeCm * (cActual / sonarCalSoundSpeed);
}

// ============================================================
// ---------------- BNO08X HELPER FUNCTIONS --------------------
// ============================================================

// Convert quaternion to Euler angles.
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

// Convenience wrapper for the BNO08x rotation vector.
void quaternionToEulerRV(sh2_RotationVectorWAcc_t *rotational_vector, EulerAngles *angles, bool degrees = false) {
  quaternionToEuler(rotational_vector->real,
                    rotational_vector->i,
                    rotational_vector->j,
                    rotational_vector->k,
                    angles,
                    degrees);
}

// Enable the desired IMU report.
void setImuReports() {
  if (!bno08x.enableReport(imuReportType, imuReportIntervalUs)) {
    Serial.println(F("[IMU] Could not enable stabilized rotation vector report."));
  }
}

// Poll the IMU and update orientation state.
void updateImu(unsigned long currentMillis) {
  if (!imuReady) return;

  // If the sensor reset itself, restore the desired reports.
  if (bno08x.wasReset()) {
    Serial.println(F("[IMU] Sensor reset detected. Re-enabling reports."));
    setImuReports();
  }

  while (bno08x.getSensorEvent(&imuSensorValue)) {
    if (imuSensorValue.sensorId == SH2_ARVR_STABILIZED_RV) {
      quaternionToEulerRV(&imuSensorValue.un.arvrStabilizedRV, &ypr, true);

      // Constrain pitch/roll for extra stability in mapping logic.
      imuYawDeg = ypr.yaw;
      imuPitchDeg = constrain(ypr.pitch, -90.0f, 90.0f);
      imuRollDeg  = constrain(ypr.roll, -180.0f, 180.0f);

      imuAccuracy = imuSensorValue.status;
      imuOrientationValid = true;
      lastImuMillis = currentMillis;
    }
  }

  // If the IMU stops updating, invalidate the orientation.
  if (imuOrientationValid && (currentMillis - lastImuMillis > imuTimeout)) {
    imuOrientationValid = false;
    imuYawDeg = NAN;
    imuPitchDeg = NAN;
    imuRollDeg = NAN;
    imuAccuracy = 0;
  }
}

// ============================================================
// ---------------- SD / FILE FUNCTIONS ------------------------
// ============================================================

// Open the next available dataXX.csv file.
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
        logFile.println(F("# ROV LOG v1.1"));
        logFile.println(F("date_utc,time_utc,lat,lat_dir,lng,lng_dir,alt_m,depth_cm,temp_c,satellites,hdop,speed_kmph"));
        logFile.flush();
        return true;
      }
      return false;
    }
  }

  return false;
}

// Open the session-matched mapXX.csv file.
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

  mapFile.println(F("# ROV MAP v1.1"));
  mapFile.println(F("point,date_utc,time_utc,lat,lng,depth_cm,temp_c,satellites,hdop,speed_kmph,fix_age_ms,pitch_deg,roll_deg,imu_acc"));
  mapFile.flush();
  return true;
}

// Attempt to recover the SD card and reopen files after a failure.
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

// Write one row to the main data log.
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

// ============================================================
// ---------------- MAPPING FUNCTIONS --------------------------
// ============================================================

// Return a short status string describing whether mapping is allowed.
const char* getMapStatus(unsigned long currentMillis) {
  if (millis() - startupMillis < gpsWarmupTime) return "WARMUP";
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

  if (!isnan(smoothedSpeed) && smoothedSpeed > maxMapSpeedKmph) return "FAST";

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

    // Adaptive spacing based on speed.
    float minSpacing = minMapSpacingMetersBase;
    if (!isnan(smoothedSpeed)) {
      minSpacing = constrain(smoothedSpeed * 0.5f, 2.0f, 10.0f);
    }

    if (distanceMeters < minSpacing) return "HOLD";
  }

  return "ARMED";
}

// Return true only if mapping is enabled and the status is ARMED.
bool shouldLogMapPoint(unsigned long currentMillis) {
  return strcmp(getMapStatus(currentMillis), "ARMED") == 0;
}

// Write one row to the mapping file.
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
  if (!isnan(smoothedSpeed)) formatFloatValue(smoothedSpeed, 1, speedStr, sizeof(speedStr));
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

// ============================================================
// ---------------- MISCELLANEOUS HELPERS ----------------------
// ============================================================

// Capture how long it took to achieve the first GPS fix.
void updateFirstFix(unsigned long currentMillis) {
  if (!firstFixCaptured && gps.location.isValid()) {
    firstFixCaptured = true;
    firstFixTimeMs = currentMillis - startupMillis;
    Serial.print(F("[GPS] First fix acquired in "));
    Serial.print(firstFixTimeMs / 1000.0, 1);
    Serial.println(F(" s"));
  }
}

// Startup LCD sequence.
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

// ============================================================
// -------------------------- SETUP ----------------------------
// ============================================================

void setup() {
  startupMillis = millis();

  Serial.begin(115200);
  delay(500);
  Serial.println(F("[ROV] Serial started."));

  // I2C bus for LCD and IMU
  Wire.begin();
  Wire.setClock(100000);

  // UART devices
  gpsSerial.begin(9600);
  ultraSerial.begin(115200);

  // Temperature sensor initialization
  tempSensor.begin();
  if (tempSensor.getDeviceCount() > 0) {
    isTempSensorAvailable = true;
    tempSensor.setResolution(10);
    tempSensor.setWaitForConversion(false);
    Serial.println(F("[TEMP] Sensor found."));
  } else {
    Serial.println(F("[TEMP] No sensor detected."));
  }

  // LCD
  lcd.init();
  lcd.backlight();
  lcdPrintLine(0, "Initializing...");

  // Heartbeat LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Treat GPS as "recently seen" at startup until timeout says otherwise
  lastGpsCharMillis = millis();

  // SD card and files
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

  // IMU
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

// ============================================================
// --------------------------- LOOP ----------------------------
// ============================================================

void loop() {
  unsigned long currentMillis = millis();

  // Heartbeat LED so the device visibly shows it is alive.
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    lastHeartbeat = currentMillis;
    heartbeatState = !heartbeatState;
    digitalWrite(LED_BUILTIN, heartbeatState ? HIGH : LOW);
  }

  // ---------------- Read GPS stream ----------------
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gps.encode(c);
    lastGpsCharMillis = millis();
  }

  updateFirstFix(currentMillis);

  // Smooth GPS speed to reduce jitter in map filtering.
  if (gps.speed.isValid()) {
    float s = gps.speed.kmph();
    if (isnan(smoothedSpeed)) smoothedSpeed = s;
    else smoothedSpeed = 0.7f * smoothedSpeed + 0.3f * s;
  }

  // ---------------- Read IMU ----------------
  updateImu(currentMillis);

  // ---------------- Trigger / read ultrasonic sensor ----------------
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

        // Check checksum.
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

    // If no valid reading for too long, clear depth.
    if (!validReading && (currentMillis - lastValidDepthMillis > depthTimeout)) {
      depthCm = NAN;
      clearDepthBuffer();
    }
  }

  // Hard timeout for stale depth values.
  if ((currentMillis - lastValidDepthMillis > depthTimeout) && !isnan(depthCm)) {
    depthCm = NAN;
    clearDepthBuffer();
  }

  // ---------------- Read temperature (non-blocking) ----------------
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

  // ---------------- SD recovery ----------------
  if ((!sdReady || !logFile) && logFilename.length() > 0) {
    attemptSdRecovery(currentMillis);
  }

  // ---------------- Main periodic logging ----------------
  if (currentMillis - lastLog >= logInterval) {
    lastLog = currentMillis;

    speed = smoothedSpeed;
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

    // Write main data log row.
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

    // Write mapping point only if the system judges the point to be good enough.
    if (mappingEnabled && shouldLogMapPoint(currentMillis)) {
      if (!writeMapPoint()) {
        Serial.println(F("[MAP] Failed to write map point."));
      }
    }

    // ---------------- Serial diagnostics ----------------
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

  // ---------------- LCD page cycling ----------------
  if (currentMillis - lastScreenSwitch >= screenInterval) {
    screenIndex = (screenIndex + 1) % 6;
    lastScreenSwitch = currentMillis;
    lcd.clear();
  }

  // ---------------- LCD updates ----------------
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
