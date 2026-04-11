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
    parser = argparse.ArgumentParser(description="Create a contour bathymetric map PNG using IDW.")
    parser.add_argument("input_csv")
    parser.add_argument("--output")
    parser.add_argument("--grid-size", type=int, default=250)
    parser.add_argument("--power", type=float, default=2.0)
    parser.add_argument("--levels", type=int, default=12)
    parser.add_argument("--point-size", type=float, default=6.0)
    args = parser.parse_args()

    points = load_points(args.input_csv)
    output_path = args.output or default_output_path(args.input_csv, "_contours", ".png")

    lons, lats, depths, xi, yi, zi, lon_min, lon_max, lat_min, lat_max = build_grid(points, args.grid_size, args.power)

    plt.figure(figsize=(9, 7))
    filled = plt.contourf(xi, yi, zi, levels=args.levels)
    plt.contour(xi, yi, zi, levels=args.levels)
    plt.colorbar(filled, label="Depth (m)")
    plt.scatter(lons, lats, s=args.point_size)
    plt.xlabel("Longitude")
    plt.ylabel("Latitude")
    plt.title("Bathymetric Contour Map")
    plt.tight_layout()
    plt.savefig(output_path, dpi=300)
    plt.close()

    print(f"Created {output_path}")

if __name__ == "__main__":
    main()
