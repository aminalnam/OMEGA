# Oceanic Measurement & Environmental Geospatial Array

These scripts work with CSV files produced by the Arduino ROV mapping logger.

Supported CSV patterns:
- comment/version lines beginning with `#`
- `lat` + `lon` or `lat` + `lng`
- `depth_m` or `depth_cm`
- optional `utc`

## Included scripts

### 1. `csv_to_kml.py`
Creates a simple point KML for Google Earth.

```bash
python csv_to_kml.py MAP00.CSV
```

### 2. `csv_to_kml_colored.py`
Creates a colored point KML where depth controls point color.

```bash
python csv_to_kml_colored.py MAP00.CSV
```

### 3. `depth_map.py`
Creates a scatter map PNG from the CSV.

```bash
python depth_map.py MAP00.CSV
```

### 4. `interpolated_map.py`
Creates a smoother interpolated bathymetric image using inverse distance weighting.

```bash
python interpolated_map.py MAP00.CSV
```

### 5. `contour_map.py`
Creates a filled contour bathymetric image with contour lines and sample-point overlay.

```bash
python contour_map.py MAP00.CSV
```

### 6. `google_earth_overlay.py`
Creates:
- a georeferenced overlay KMZ
- a KML GroundOverlay that places the image correctly in Google Earth

```bash
python google_earth_overlay.py MAP00.CSV
```

## Common options

Many scripts support:
- `--output`
- `--grid-size`
- `--power`

Examples:

```bash
python interpolated_map.py MAP00.CSV --grid-size 300 --power 2.0
python contour_map.py MAP00.CSV --grid-size 300 --levels 12
python google_earth_overlay.py MAP00.CSV --grid-size 300
```

## Outputs

By default, files are written next to the input CSV using derived names:
- `MAP00.kml`
- `MAP00_colored.kml`
- `MAP00_depth_map.png`
- `MAP00_interpolated.png`
- `MAP00_contours.png`
- `MAP00_overlay.png`
- `MAP00_overlay.kml`

## Notes

- Depth is treated as positive downward
- Scripts ignore comment lines beginning with `#`
- For best results, use the filtered `MAPxx.CSV`, not the raw `DATAxx.CSV`
