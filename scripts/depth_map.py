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
import matplotlib.pyplot as plt

def main():
    parser = argparse.ArgumentParser(description="Create a scatter depth map PNG from an ROV CSV.")
    parser.add_argument("input_csv")
    parser.add_argument("--output")
    parser.add_argument("--point-size", type=float, default=18.0)
    args = parser.parse_args()

    points = load_points(args.input_csv)
    output_path = args.output or default_output_path(args.input_csv, "_depth_map", ".png")

    lats = [p["lat"] for p in points]
    lons = [p["lon"] for p in points]
    depths = [p["depth_m"] for p in points]

    plt.figure(figsize=(8, 6))
    scatter = plt.scatter(lons, lats, c=depths, s=args.point_size)
    plt.colorbar(scatter, label="Depth (m)")
    plt.xlabel("Longitude")
    plt.ylabel("Latitude")
    plt.title("ROV Depth Map")
    plt.tight_layout()
    plt.savefig(output_path, dpi=300)
    plt.close()

    print(f"Created {output_path}")

if __name__ == "__main__":
    main()
