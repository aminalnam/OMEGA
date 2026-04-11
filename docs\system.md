# System Documentation

## System Overview

This system is a mobile bathymetric survey platform that collects depth measurements along a trajectory and reconstructs a terrain surface from filtered spatial samples.

It is designed as a self-contained data acquisition and mapping pipeline, combining real-time sensing, validation, and post-processing.

The system operates in three stages:

1. Acquisition — sensor data is collected continuously
2. Validation — measurements are evaluated and filtered in real time
3. Reconstruction — a surface is generated from selected data points

---

## Data Acquisition

The Arduino-based logger continuously samples:

- GPS — latitude, longitude, and UTC time
- Sonar — depth measurement below the sensor
- Temperature Sensor (DS18B20) — water temperature
- IMU (BNO085) — pitch and roll

Each measurement includes:
- timestamp
- geographic position
- system state

---

## Real-Time Data Filtering

Each measurement is evaluated before being used for mapping.

GPS constraints:
- valid fix
- minimum satellites
- HDOP below threshold
- fix age within limit

Motion constraints:
- speed below threshold
- pitch within limits
- roll within limits

Depth constraints:
- valid reading
- within expected range
- stable vs recent values

Spatial constraint:
- minimum spacing between accepted points

---

## Dual Dataset Design

The system generates two files:

dataXX.csv
- full log
- all measurements

mapXX.csv
- filtered dataset
- mapping-quality points only

---

## Depth Processing

Smoothing:
- moving average filter
- reduces noise and spikes

Sound speed correction:
- based on temperature
- improves depth consistency

---

## Surface Reconstruction

Mapping data consists of:

(lat, lng, depth)

A continuous surface is generated using interpolation.

Inverse Distance Weighting (IDW):
- closer points have more influence
- farther points contribute less

---

## Mapping Resolution

Depends on:
- sampling density
- survey coverage
- platform speed
- filtering thresholds

---

## Coordinates and Units

Coordinate system:
- WGS84

Units:
- depth: cm
- temperature: C
- speed: km/h

---

## Outputs

Raster maps:
- scatter
- interpolated surface

Contour maps:
- depth lines

Geographic outputs:
- KML
- overlays

---

## Error Sources

Sensor:
- beam spread
- noise
- range limits

GPS:
- positional error
- HDOP variation

Motion:
- tilt effects
- speed spacing

Environment:
- temperature variation
- acoustic interference

---

## System Behavior

The system does not generate maps in real time.

It:
- collects filtered samples
- reconstructs surfaces afterward

---

## Design Decisions

Real-time filtering:
- improves data quality early

Dual files:
- preserves raw data
- enables reprocessing

Interpolation:
- handles irregular sampling
- efficient and stable

---

## Limitations

- single-beam sampling
- no salinity correction
- GPS dependency
- interpolation assumptions

---

## Future Work

- pressure depth sensor
- multi-beam sonar
- adaptive sampling
- real-time mapping
- wireless telemetry
- improved interpolation
