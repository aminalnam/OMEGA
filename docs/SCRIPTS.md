# Oceanic Measurement & Environmental Geospatial Array

## Script Reference

These scripts process mapping CSV files produced by the Arduino ROV logger and generate Google Earth exports or bathymetric visualizations from recorded survey points. They are intended primarily for cleaned mapping datasets such as `MAP00.CSV`, rather than raw logger files such as `DATA00.CSV`.

## Recommended input

For best results, use filtered `MAPxx.CSV` files rather than raw `DATAxx.CSV` logs.

At minimum, the scripts expect:

* `lat`
* `lon` or `lng`
* `depth_m` or `depth_cm`

The scripts also support:

* comment lines beginning with `#`
* optional timestamp fields
* richer metadata fields in some mapping exports

If depth is provided in centimeters, it is converted internally to meters. Invalid or incomplete rows are skipped.

## Quick start

### Export measured points to Google Earth

```bash
python csv_to_kml_points.py MAP00.CSV
python csv_to_kml_points_colored.py MAP00.CSV
```

### Create PNG visualizations

```bash
python csv_to_png_depth_points.py MAP00.CSV
python csv_to_png_depth_interpolated.py MAP00.CSV
python csv_to_png_depth_contours.py MAP00.CSV
```

### Create a Google Earth surface overlay

```bash
python csv_to_kmz_depth_overlay.py MAP00.CSV
```

The point KML scripts export the recorded sample points directly. The interpolated PNG, contour PNG, and Earth overlay scripts generate continuous surfaces from the recorded points using inverse distance weighting (IDW).

## Scripts

### `csv_to_kml_points.py`

Creates a simple point-based KML for Google Earth. Each valid row in the CSV becomes a placemark at the recorded latitude and longitude. This script does not interpolate between points.

**Example**

```bash
python csv_to_kml_points.py MAP00.CSV
```

**Default output**

* `MAP00.kml`

**Best for**

* reviewing raw survey points
* opening measured positions directly in Google Earth
* preserving point metadata in placemark descriptions

---

### `csv_to_kml_points_colored.py`

Creates a point-based KML where placemark color is controlled by depth across the dataset. This script still uses only the original measured points; it does not create an interpolated surface. It also supports `--scale` to adjust point icon size.

**Example**

```bash
python csv_to_kml_points_colored.py MAP00.CSV
```

**Default output**

* `MAP00_colored.kml`

**Best for**

* quick depth-based point visualization in Google Earth
* comparing shallow and deep sample locations
* lightweight spatial review before generating surfaces

---

### `csv_to_png_depth_points.py`

Creates a scatter-plot PNG using recorded longitude, latitude, and depth values. This is the most direct 2D visualization of the actual measured points. Depth controls point color, and visible point size can be adjusted with `--point-size`.

**Example**

```bash
python csv_to_png_depth_points.py MAP00.CSV
```

**Default output**

* `MAP00_depth_map.png`

**Best for**

* checking sample coverage
* verifying the mapped area
* seeing the true distribution of collected points before interpolation

---

### `csv_to_png_depth_interpolated.py`

Creates an interpolated bathymetric PNG using inverse distance weighting (IDW). Instead of showing only measured points, it estimates depth between them on a generated grid. It supports `--grid-size`, `--power`, and `--point-size`.

**Example**

```bash
python csv_to_png_depth_interpolated.py MAP00.CSV
```

**Default output**

* `MAP00_interpolated.png`

**Best for**

* creating a smoothed bathymetric surface
* presentation-ready visualization
* estimating bottom shape between recorded measurements

---

### `csv_to_png_depth_contours.py`

Creates a filled contour bathymetric PNG with contour lines and sample-point overlay. Like the interpolated map script, it uses IDW to estimate depth between measured points, but renders the output as contour bands. It supports `--grid-size`, `--power`, `--levels`, and `--point-size`.

**Example**

```bash
python csv_to_png_depth_contours.py MAP00.CSV
```

**Default output**

* `MAP00_contours.png`

**Best for**

* visualizing depth intervals
* generating more map-like bathymetric graphics
* identifying slope changes and transitions across the survey area

---

### `csv_to_kmz_depth_overlay.py`

Creates a Google Earth overlay product from an interpolated surface. The script generates an interpolated depth grid, renders a transparent overlay image, writes a KML GroundOverlay, and packages them into a `.kmz` file. It supports `--grid-size`, `--power`, and `--output-prefix`.

**Example**

```bash
python csv_to_kmz_depth_overlay.py MAP00.CSV
```

**Default output**

* `MAP00.kmz`

**Internal KMZ contents**

* `overlay.png`
* `doc.kml`

**Best for**

* viewing an interpolated bathymetric surface in Google Earth
* draping the mapped depth surface over satellite imagery
* sharing a single georeferenced overlay file

## Common options

Not every script supports the same options, but the most common are:

* `--output` for a custom output filename
* `--grid-size` for interpolation density
* `--power` for the IDW weighting strength
* `--point-size` for visible point size in PNG outputs
* `--levels` for contour density
* `--scale` for colored KML point size
* `--output-prefix` for KMZ overlay naming

**Examples**

```bash
python csv_to_png_depth_interpolated.py MAP00.CSV --grid-size 300 --power 2.0
python csv_to_png_depth_contours.py MAP00.CSV --grid-size 300 --levels 12
python csv_to_kmz_depth_overlay.py MAP00.CSV --grid-size 300
python csv_to_png_depth_points.py MAP00.CSV --point-size 24
python csv_to_kml_points_colored.py MAP00.CSV --scale 0.9
```

## Output summary

By default, outputs are written next to the input CSV using derived names:

* `MAP00.kml`
* `MAP00_colored.kml`
* `MAP00_depth_map.png`
* `MAP00_interpolated.png`
* `MAP00_contours.png`
* `MAP00.kmz`

## Choosing the right script

Use `csv_to_kml_points.py` when you want to inspect measured sample locations directly.

Use `csv_to_kml_points_colored.py` when you want those same points styled by depth.

Use `csv_to_png_depth_points.py` when you want a direct scatter view of recorded points.

Use `csv_to_png_depth_interpolated.py` when you want a smoothed bathymetric surface.

Use `csv_to_png_depth_contours.py` when you want contour-based depth graphics.

Use `csv_to_kmz_depth_overlay.py` when you want a georeferenced surface overlay in Google Earth.

## Notes

* Depth is treated as positive downward.
* Comment lines beginning with `#` are ignored.
* The KML point exporters are not interpolated.
* The interpolated PNG, contour PNG, and Earth overlay scripts generate interpolated surfaces using IDW.
