import csv
import os
import argparse
import zipfile
import tempfile
import shutil
import numpy as np
import matplotlib.pyplot as plt


def load_points(csv_path):
    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        rows = [line for line in f if not line.lstrip().startswith("#") and line.strip()]

    reader = csv.DictReader(rows)
    points = []

    for row in reader:
        try:
            lat = float(row["lat"])
            lon = float(row["lon"]) if "lon" in row and row["lon"] not in ("", None) else float(row["lng"])

            if "depth_m" in row and row["depth_m"] not in ("", None):
                depth_m = float(row["depth_m"])
            elif "depth_cm" in row and row["depth_cm"] not in ("", None):
                depth_m = float(row["depth_cm"]) / 100.0
            else:
                continue

            points.append({"lat": lat, "lon": lon, "depth_m": depth_m})
        except Exception:
            continue

    if not points:
        raise ValueError("No valid mapping points found in CSV.")

    return points


def default_output_prefix(input_path):
    base, _ = os.path.splitext(input_path)
    return f"{base}_overlay"


def idw_interpolation(x, y, z, xi, yi, power=2.0, epsilon=1e-12):
    zi = np.zeros_like(xi, dtype=float)

    for row in range(xi.shape[0]):
        for col in range(xi.shape[1]):
            dx = x - xi[row, col]
            dy = y - yi[row, col]
            dist = np.sqrt(dx * dx + dy * dy)

            if np.any(dist < epsilon):
                zi[row, col] = z[np.argmin(dist)]
                continue

            weights = 1.0 / np.power(dist, power)
            zi[row, col] = np.sum(weights * z) / np.sum(weights)

    return zi


def build_grid(points, grid_size, power):
    lats = np.array([p["lat"] for p in points], dtype=float)
    lons = np.array([p["lon"] for p in points], dtype=float)
    depths = np.array([p["depth_m"] for p in points], dtype=float)

    lon_min, lon_max = lons.min(), lons.max()
    lat_min, lat_max = lats.min(), lats.max()

    xi_vals = np.linspace(lon_min, lon_max, grid_size)
    yi_vals = np.linspace(lat_min, lat_max, grid_size)
    xi, yi = np.meshgrid(xi_vals, yi_vals)
    zi = idw_interpolation(lons, lats, depths, xi, yi, power=power)

    return zi, lon_min, lon_max, lat_min, lat_max


def main():
    parser = argparse.ArgumentParser(description="Create a Google Earth KMZ overlay from an ROV mapping CSV.")
    parser.add_argument("input_csv", help="Path to input CSV file")
    parser.add_argument("--grid-size", type=int, default=250, help="Interpolation grid size")
    parser.add_argument("--power", type=float, default=2.0, help="IDW power parameter")
    parser.add_argument("--output-prefix", help="Optional output prefix")
    args = parser.parse_args()

    points = load_points(args.input_csv)
    prefix = args.output_prefix or default_output_prefix(args.input_csv)

    kmz_path = f"{prefix}.kmz"
    image_name = "overlay.png"
    kml_name = "doc.kml"

    zi, lon_min, lon_max, lat_min, lat_max = build_grid(points, args.grid_size, args.power)

    temp_dir = tempfile.mkdtemp(prefix="rov_overlay_")
    try:
        image_path = os.path.join(temp_dir, image_name)
        kml_path = os.path.join(temp_dir, kml_name)

        fig = plt.figure(figsize=(10, 8))
        ax = fig.add_axes([0, 0, 1, 1])
        ax.imshow(
            zi,
            extent=(lon_min, lon_max, lat_min, lat_max),
            origin="lower",
            aspect="auto",
            alpha=0.85,
        )
        ax.axis("off")
        plt.savefig(image_path, dpi=300, transparent=True, bbox_inches="tight", pad_inches=0)
        plt.close(fig)

        kml = f'''<?xml version="1.0" encoding="UTF-8"?>
<kml xmlns="http://www.opengis.net/kml/2.2">
  <Document>
    <name>ROV Bathymetric Overlay</name>
    <GroundOverlay>
      <name>Bathymetric Overlay</name>
      <Icon>
        <href>{image_name}</href>
      </Icon>
      <LatLonBox>
        <north>{lat_max}</north>
        <south>{lat_min}</south>
        <east>{lon_max}</east>
        <west>{lon_min}</west>
      </LatLonBox>
    </GroundOverlay>
  </Document>
</kml>
'''
        with open(kml_path, "w", encoding="utf-8") as f:
            f.write(kml)

        with zipfile.ZipFile(kmz_path, "w", zipfile.ZIP_DEFLATED) as kmz:
            kmz.write(kml_path, arcname=kml_name)
            kmz.write(image_path, arcname=image_name)

        print(f"Created {kmz_path}")
        print("Open the KMZ file in Google Earth.")
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
