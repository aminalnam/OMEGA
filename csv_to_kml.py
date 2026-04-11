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

def main():
    parser = argparse.ArgumentParser(description="Convert ROV mapping CSV to simple KML.")
    parser.add_argument("input_csv")
    parser.add_argument("--output")
    args = parser.parse_args()

    points = load_points(args.input_csv)
    output_path = args.output or default_output_path(args.input_csv, "", ".kml")

    with open(output_path, "w", encoding="utf-8") as kml:
        kml.write('<?xml version="1.0" encoding="UTF-8"?>\n')
        kml.write('<kml xmlns="http://www.opengis.net/kml/2.2">\n<Document>\n')
        for p in points:
            name = p["utc"] if p["utc"] else "ROV Point"
            desc = f"Depth: {p['depth_m']:.2f} m"
            kml.write(f"""<Placemark>
  <name>{name}</name>
  <description>{desc}</description>
  <Point><coordinates>{p['lon']},{p['lat']},0</coordinates></Point>
</Placemark>
""")
        kml.write('</Document>\n</kml>\n')
    print(f"Created {output_path}")

if __name__ == "__main__":
    main()
