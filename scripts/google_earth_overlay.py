import csv
import os
from typing import List, Dict

def load_points(csv_path: str):
    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        rows = [line for line in f if not line.lstrip().startswith("#") and line.strip()]

    reader = csv.DictReader(rows)
    points = []

    for row in reader:
        try:
            lat = float(row["lat"])
            if "lon" in row and row["lon"] not in ("", None):
                lon = float(row["lon"])
            else:
                lon = float(row["lng"])

            if "depth_m" in row and row["depth_m"] not in ("", None):
                depth_m = float(row["depth_m"])
            elif "depth_cm" in row and row["depth_cm"] not in ("", None):
                depth_m = float(row["depth_cm"]) / 100.0
            else:
                continue

            utc = row.get("utc", "")
            points.append({"lat": lat, "lon": lon, "depth_m": depth_m, "utc": utc})
        except Exception:
            continue

    if not points:
        raise ValueError("No valid mapping points found in CSV.")

    return points

def default_output_path(input_path: str, suffix: str, ext: str) -> str:
    base, _ = os.path.splitext(input_path)
    return f"{base}{suffix}{ext}"

import argparse
import numpy as np
import matplotlib.pyplot as plt

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

    return lons, lats, depths, xi, yi, zi, lon_min, lon_max, lat_min, lat_max

def main():
    parser = argparse.ArgumentParser(description="Create a Google Earth image overlay and matching KML.")
    parser.add_argument("input_csv")
    parser.add_argument("--output-prefix", help="Optional output prefix")
    parser.add_argument("--grid-size", type=int, default=250)
    parser.add_argument("--power", type=float, default=2.0)
    args = parser.parse_args()

    points = load_points(args.input_csv)
    base_prefix = args.output_prefix or os.path.splitext(args.input_csv)[0]
    image_path = f"{base_prefix}_overlay.png"
    kml_path = f"{base_prefix}_overlay.kml"

    lons, lats, depths, xi, yi, zi, lon_min, lon_max, lat_min, lat_max = build_grid(points, args.grid_size, args.power)

    # render transparent-ish overlay
    fig = plt.figure(figsize=(9, 7))
    ax = fig.add_subplot(111)
    im = ax.imshow(
        zi,
        extent=(lon_min, lon_max, lat_min, lat_max),
        origin="lower",
        aspect="auto",
        alpha=0.85
    )
    ax.set_xlabel("Longitude")
    ax.set_ylabel("Latitude")
    ax.set_title("Bathymetric Overlay")
    plt.colorbar(im, label="Depth (m)")
    plt.tight_layout()
    plt.savefig(image_path, dpi=300)
    plt.close()

    image_name = os.path.basename(image_path)

    with open(kml_path, "w", encoding="utf-8") as kml:
        kml.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        kml.write('<kml xmlns="http://www.opengis.net/kml/2.2">\n')
        kml.write('<Document>\n')
        kml.write('  <GroundOverlay>\n')
        kml.write('    <name>ROV Bathymetric Overlay</name>\n')
        kml.write('    <Icon>\n')
        kml.write(f'      <href>{image_name}</href>\n')
        kml.write('    </Icon>\n')
        kml.write('    <LatLonBox>\n')
        kml.write(f'      <north>{lat_max}</north>\n')
        kml.write(f'      <south>{lat_min}</south>\n')
        kml.write(f'      <east>{lon_max}</east>\n')
        kml.write(f'      <west>{lon_min}</west>\n')
        kml.write('    </LatLonBox>\n')
        kml.write('  </GroundOverlay>\n')
        kml.write('</Document>\n')
        kml.write('</kml>\n')

    print(f"Created {image_path}")
    print(f"Created {kml_path}")

if __name__ == "__main__":
    main()
