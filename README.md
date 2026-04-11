## Oceanic Geospatial Measurement System

A portable underwater mapping system that records depth, position, and environmental data to generate bathymetric maps from a small ROV platform. This project combines embedded sensing, spatial filtering, and data visualization to transform raw measurements into readable maps and geographic overlays.

---

### ## Overview
This system captures underwater depth data using an **Arduino-based logger** and converts it into visual maps using **Python-based processing tools**.

**It is designed to:**
* Log reliable field data in real time.
* Filter out low-quality measurements.
* Generate usable spatial maps from irregular samples.
* Visualize results in both image form and geographic context.

---

### ## System Architecture

| Layer | Components / Functions |
| :--- | :--- |
| **Capture Layer (Arduino)** | GPS (Position/UTC), Ultrasonic Sonar (Depth), DS18B20 (Temp), BNO085 IMU (Orientation), SD Card (Logging) |
| **Processing Layer (Python)** | CSV parsing, Spatial Interpolation (IDW), Contour generation, KML + Image overlays |
| **Output Layer** | Raw logs, Filtered mapping data, Depth maps (PNG), Contour maps, Google Earth overlays (KML) |

---

### ## Hardware & Wiring

#### **Hardware Components**
* **Controller:** Arduino Mega
* **Positioning:** GPS module (UART)
* **Sensing:** Underwater ultrasonic distance sensor, DS18B20 temperature sensor, BNO085 IMU
* **Interface:** I2C 16x2 LCD, SD card module

#### **Wiring Summary**
| Component | Connection |
| :--- | :--- |
| **GPS** | RX1 (19), TX1 (18) |
| **Ultrasonic** | RX2 (17), TX2 (16) |
| **SD Card** | CS Pin 53 |
| **Temp Sensor** | Pin 6 |
| **LCD / IMU** | SDA (20), SCL (21) |

---

### ## Data Pipeline

#### **1. Data Capture**
The Arduino logger produces two distinct files:
* **`dataXX.csv`**: Full system log for diagnostics.
* **`mapXX.csv`**: Filtered survey points only, ensuring a valid GPS fix, acceptable HDOP, stable orientation (pitch/roll), and proper spatial spacing.

#### **2. Processing**
Run scripts on the mapping file to generate visuals:
```bash
python contour_map.py MAP00.CSV
python interpolated_map.py MAP00.CSV
python google_earth_overlay.py MAP00.CSV
```

#### **3. Outputs**
* **Scatter map:** Quick validation.
* **Interpolated map:** Continuous surface.
* **Contour map:** Readable bathymetry.
* **KML overlay:** Geographic visualization.

---

### ## Key Features
* **UTC Time Logging:** Avoids timezone inconsistencies.
* **Depth Smoothing:** Moving average filter reduces noise in sonar readings.
* **Sound Speed Correction:** Depth is adjusted using temperature-based sound speed estimation.
* **Intelligent Mapping Filter:** Only high-quality data points are recorded.
* **Spatial Interpolation:** Inverse Distance Weighting (IDW) converts discrete samples into continuous surfaces.

---

### ## Data Format
**Mapping File (`mapXX.csv`) Structure:**
`point, date_utc, time_utc, lat, lng, depth_cm, temp_c, satellites, hdop, speed_kmph, fix_age_ms, pitch_deg, roll_deg, imu_acc`

---

### ## Limitations & Future Improvements

**Limitations**
* Ultrasonic sensors have limited range and beam spread.
* No salinity correction (temperature-only model).
* GPS accuracy limits spatial resolution.

**Future Improvements**
* Pressure-based depth sensor integration.
* Salinity-aware sound speed correction.
* Real-time map generation and autonomous survey path planning.

