# System Documentation

## System Overview

OMEGA is a mobile bathymetric survey system designed to collect underwater depth measurements along a traveled path and reconstruct a mapped representation of submerged terrain from filtered spatial samples. It combines field sensing, real-time validation, structured logging, and post-processing into a single acquisition and mapping pipeline.

The system is designed around a simple idea: not every field measurement is equally useful for mapping. Depth values must be interpreted in context, evaluated against position and motion quality, and filtered before they are used to reconstruct a surface. To support this, OMEGA preserves both the complete raw log of the survey and a reduced mapping dataset containing only higher-quality accepted points.

In practice, the platform operates in four stages:

1. **Acquisition** — sensor data is collected continuously during movement
2. **Validation** — each observation is checked against quality rules in real time
3. **Logging** — the system writes both raw and filtered datasets
4. **Reconstruction** — mapping products are generated afterward from accepted points

This architecture allows the system to remain lightweight in the field while still supporting structured bathymetric reconstruction and geographic export in post-processing.

---

## Purpose

The purpose of the system is to produce a usable spatial survey record from a moving field platform. It is intended to:

* collect georeferenced underwater depth measurements
* associate those measurements with environmental and motion context
* reject observations that are unsuitable for mapping
* preserve both raw and filtered data products
* generate interpretable map outputs from accepted spatial samples

OMEGA is best understood not as a single logger, but as a full measurement-to-map workflow.

---

## Architecture

The system is organized into four main functional layers.

### 1. Sensor Layer

The sensor layer gathers both mapping variables and measurement context.

Typical inputs include:

* **GPS**

  * latitude
  * longitude
  * UTC time
  * speed
  * satellite count
  * HDOP
  * fix age

* **Depth sensor / sonar**

  * depth below the sensor

* **DS18B20 temperature sensor**

  * water temperature

* **BNO085 IMU**

  * pitch
  * roll

These measurements are combined into structured observations that represent both the environment and the state of the survey platform.

### 2. Validation Layer

The validation layer determines whether a recorded observation is suitable for mapping. It evaluates:

* GPS quality
* motion stability
* depth plausibility
* spatial spacing

Its role is to prevent unreliable or redundant measurements from dominating the mapping dataset.

### 3. Logging Layer

The logging layer writes two parallel outputs:

* a complete raw acquisition log
* a filtered mapping-quality dataset

This separation preserves the full field record while creating a cleaner input for later reconstruction.

### 4. Reconstruction Layer

The reconstruction layer uses filtered spatial samples to generate:

* scatter maps
* interpolated depth surfaces
* contour maps
* KML exports
* Google Earth overlays

---

## Data Model

Each logged record is treated as a structured measurement event rather than a single sensor reading.

A typical observation may include:

* UTC timestamp
* latitude
* longitude
* depth
* water temperature
* pitch
* roll
* speed
* satellite count
* HDOP
* fix age
* acceptance state

This structure makes it possible to judge depth measurements in context rather than in isolation.

### Core Mapping Representation

The mapping dataset is ultimately reduced to samples of the form:

`(lat, lng, depth)`

These accepted points serve as the basis for reconstruction.

### Supporting Metadata

Additional fields are retained because they help determine whether a point should be trusted. These include GPS quality indicators, motion state, and environmental correction inputs.

---

## Data Flow

The system follows a staged processing pipeline:

**Sensor input**
→ **measurement assembly**
→ **real-time validation**
→ **raw log + filtered map log**
→ **post-processing and reconstruction**
→ **map outputs**

### Measurement Assembly

Sensor readings are combined into one observation that associates:

* time
* position
* depth
* motion state
* environmental conditions

### Validation

Each observation is checked against quality constraints. A measurement may be logged but rejected from the mapping dataset if its conditions are poor.

### Dual Logging

The system writes:

* all observations to the raw file
* accepted observations to the mapping file

### Reconstruction

Accepted mapping points are then used to produce visual and geographic outputs.

---

## Filtering Logic

Real-time filtering is a central design feature of the system. It improves mapping quality by screening measurements as they are collected instead of waiting until the end of the survey.

### GPS Constraints

A point is only eligible for mapping when position quality meets defined requirements, such as:

* valid GPS fix
* minimum satellite count
* HDOP below threshold
* acceptable fix age

This helps ensure that accepted depth samples are tied to credible locations.

### Motion Constraints

The platform’s motion state affects both measurement geometry and spatial sampling quality. The system evaluates:

* speed
* pitch
* roll

This reduces the likelihood that unstable measurements become control points in the reconstructed surface.

### Depth Constraints

Depth values are tested for:

* valid reading
* plausible range
* consistency relative to recent values
* resistance to spikes or unstable jumps

These checks suppress noisy readings while preserving actual terrain variation.

### Spatial Constraint

A minimum spacing rule is used to prevent accepted mapping points from clustering too densely. This improves point distribution and reduces oversampling of nearly identical locations.

---

## Dual-File Logging Strategy

OMEGA writes two CSV datasets with different roles.

### `dataXX.csv`

This is the complete acquisition log. It preserves the full field record and may include:

* accepted and rejected measurements
* unstable sensor states
* low-quality observations
* diagnostic information

This file supports review, debugging, and future reprocessing.

### `mapXX.csv`

This is the filtered mapping dataset. It contains only the observations that pass the acceptance logic and are intended for surface reconstruction.

### Why the Two-File Design Matters

This structure allows the system to:

* preserve the raw record
* keep filtering transparent
* support alternate reprocessing later
* simplify mapping workflows
* prevent noisy data from dominating reconstruction

The raw log preserves everything the system saw. The mapping file preserves what the system judged suitable for map generation.

---

## Depth Processing

Depth is treated as a measured signal that may require refinement before reconstruction.

### Smoothing

A moving average filter is used to reduce short-term noise and isolated spikes in the depth series. This improves stability and reduces the visual impact of transient sensor noise.

### Temperature-Based Sound Speed Correction

Because acoustic depth sensing depends on the speed of sound in water, and that speed changes with temperature, the system applies a temperature-based correction using measured water temperature. This improves depth consistency across varying conditions.

---

## Reconstruction Method

### Reconstruction Input

The reconstruction stage begins with filtered spatial samples:

`(lat, lng, depth)`

These samples are irregularly spaced and do not directly define a continuous terrain surface.

### Why Interpolation Is Needed

A moving survey platform only measures specific locations along its path. The bottom surface must therefore be estimated between recorded points.

### Inverse Distance Weighting (IDW)

OMEGA reconstructs the surface using Inverse Distance Weighting (IDW). In this method:

* nearby points influence the estimate more strongly
* distant points influence it less
* each output location is computed from surrounding accepted samples

IDW is a practical choice because it works well with irregular point spacing and is easy to interpret.

### Strengths of the Method

* simple and efficient
* stable for irregularly spaced field points
* well suited to lightweight exploratory mapping
* easy to use across small and medium datasets

### Limitations of the Method

* assumes local smoothness
* may oversimplify abrupt terrain changes
* estimates unsampled regions rather than measuring them
* does not express uncertainty on its own

The reconstructed surface should therefore be interpreted as a modeled estimate constrained by accepted field samples.

---

## Output Products

The system supports multiple output types.

### Raw and Filtered CSV Files

These preserve the complete acquisition history and the filtered mapping dataset.

### Scatter Maps

Scatter maps display accepted points directly. They are useful for:

* checking survey coverage
* reviewing point spacing
* diagnosing acquisition behavior

### Interpolated Surface Maps

These produce a continuous estimated depth surface from accepted spatial samples.

### Contour Maps

Contour maps show depth zones and transitions, making bottom structure easier to read.

### Geographic Exports

These include:

* point KML exports
* colored point KML exports
* georeferenced overlays for Google Earth

---

## Coordinate System and Units

### Coordinate Reference System

* **WGS84**

### Units

* **depth** — centimeters in logging and processed depth values as applicable
* **temperature** — degrees Celsius
* **speed** — kilometers per hour

---

## Sources of Error

The system is affected by several interacting forms of uncertainty.

### Sensor Uncertainty

Depth sensing may be influenced by:

* acoustic noise
* beam spread
* range limitations
* unstable returns
* bottom reflectivity
* sloped terrain

### GPS Uncertainty

Position may be affected by:

* horizontal error
* poor satellite geometry
* stale fixes
* degraded reception

### Motion-Induced Error

Motion affects both geometry and sample distribution through:

* pitch
* roll
* speed
* platform instability

### Environmental Variability

Measurements may also be affected by:

* water temperature changes
* acoustic interference
* turbulence
* changing field conditions

### Reconstruction Uncertainty

Interpolation introduces assumptions about the behavior of unsampled areas and depends heavily on the spatial distribution of accepted points.

---

## Design Decisions

Several design choices shape the system.

### Real-Time Filtering

Filtering in the field improves the quality of the mapping dataset early and reduces later cleanup.

### Separate Raw and Filtered Files

This preserves both traceability and flexibility. The raw file keeps the complete field history, while the filtered file remains focused on mapping.

### Post-Processed Reconstruction

Map generation is deferred until after acquisition so the logging system remains lightweight and reconstruction settings can be tuned afterward.

### IDW Surface Estimation

IDW provides a practical balance between simplicity, interpretability, and computational cost for this kind of irregular mobile survey data.

---

## Known Limitations

The current system has several important limits:

* single-beam depth sensing rather than swath coverage
* strong dependence on GPS quality
* no salinity-based sound speed correction in the current model
* interpolation of unsampled regions
* reduced accuracy under unstable motion conditions
* better suited to exploratory mapping than formal hydrographic survey work

These limitations define the system’s intended scope.

---

## Future Development

Possible future directions include:

### Sensor Improvements

* pressure-based depth sensing
* improved sonar hardware
* multibeam or wider-swath sensing

### Validation Improvements

* adaptive thresholds
* confidence scoring
* motion-aware acceptance logic
* stronger sensor fusion

### Reconstruction Improvements

* alternative interpolation methods
* uncertainty-aware mapping
* denser and more configurable grid control

### System Integration

* wireless telemetry
* remote monitoring
* live preview mapping
* distributed sensing and networked devices

### Environmental Modeling

* salinity-aware sound speed correction
* improved acoustic calibration
* more advanced environmental compensation

---

## Summary

OMEGA is a structured bathymetric acquisition and reconstruction system built around the distinction between measurement and mapping-quality measurement. Its core strength is the integration of real-time filtering, dual-file logging, and post-processed surface generation into a lightweight field workflow.

Rather than treating every observation as equally trustworthy, the system preserves the full record while selectively constructing a cleaner mapping dataset for reconstruction. That separation between acquisition, validation, logging, and surface generation is the core architectural principle of the platform.
