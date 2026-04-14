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

            # Extract all sensor metadata
            point = {"lat": lat, "lon": lon, "depth_m": depth_m}
            
            # Timestamp
            date_utc = row.get("date_utc", "")
            time_utc = row.get("time_utc", "")
            point["date_utc"] = date_utc
            point["time_utc"] = time_utc
            
            # Temperature
            if "temp_c" in row and row["temp_c"] not in ("", None):
                point["temp_c"] = float(row["temp_c"])
            else:
                point["temp_c"] = None
            
            # GPS quality
            if "satellites" in row and row["satellites"] not in ("", None):
                point["satellites"] = int(row["satellites"])
            else:
                point["satellites"] = None
                
            if "hdop" in row and row["hdop"] not in ("", None):
                point["hdop"] = float(row["hdop"])
            else:
                point["hdop"] = None
                
            if "speed_kmph" in row and row["speed_kmph"] not in ("", None):
                point["speed_kmph"] = float(row["speed_kmph"])
            else:
                point["speed_kmph"] = None
                
            if "fix_age_ms" in row and row["fix_age_ms"] not in ("", None):
                point["fix_age_ms"] = int(row["fix_age_ms"])
            else:
                point["fix_age_ms"] = None
            
            # IMU data
            if "pitch_deg" in row and row["pitch_deg"] not in ("", None):
                point["pitch_deg"] = float(row["pitch_deg"])
            else:
                point["pitch_deg"] = None
                
            if "roll_deg" in row and row["roll_deg"] not in ("", None):
                point["roll_deg"] = float(row["roll_deg"])
            else:
                point["roll_deg"] = None
                
            if "imu_acc" in row and row["imu_acc"] not in ("", None):
                point["imu_acc"] = int(row["imu_acc"])
            else:
                point["imu_acc"] = None
            
            points.append(point)
        except Exception:
            continue

    if not points:
        raise ValueError("No valid mapping points found in CSV.")

    return points

def default_output_path(input_path: str, suffix: str, ext: str) -> str:
    base, _ = os.path.splitext(input_path)
    return f"{base}{suffix}{ext}"

def format_metadata_description(point):
    """Generate a description with all sensor metadata."""
    lines = []
    
    # Timestamp
    if point.get("date_utc") or point.get("time_utc"):
        timestamp = f"{point.get('date_utc', '')} {point.get('time_utc', '')}".strip()
        lines.append(f"UTC: {timestamp}")
    
    # Depth
    lines.append(f"Depth: {point['depth_m']:.2f} m")
    
    # Temperature
    if point.get("temp_c") is not None:
        lines.append(f"Temperature: {point['temp_c']:.1f} °C")
    
    # GPS Quality
    lines.append("")
    lines.append("GPS Quality:")
    if point.get("satellites") is not None:
        lines.append(f"  Satellites: {point['satellites']}")
    if point.get("hdop") is not None:
        lines.append(f"  HDOP: {point['hdop']:.2f}")
    if point.get("fix_age_ms") is not None:
        lines.append(f"  Fix Age: {point['fix_age_ms']} ms")
    
    # Motion
    if point.get("speed_kmph") is not None:
        lines.append(f"  Speed: {point['speed_kmph']:.2f} km/h")
    
    # IMU/Orientation
    if point.get("pitch_deg") is not None or point.get("roll_deg") is not None:
        lines.append("")
        lines.append("Platform Orientation:")
        if point.get("pitch_deg") is not None:
            lines.append(f"  Pitch: {point['pitch_deg']:.1f}°")
        if point.get("roll_deg") is not None:
            lines.append(f"  Roll: {point['roll_deg']:.1f}°")
        if point.get("imu_acc") is not None:
            acc_levels = {0: "Not calibrated", 1: "Low", 2: "Medium", 3: "High"}
            acc_label = acc_levels.get(point['imu_acc'], "Unknown")
            lines.append(f"  IMU Accuracy: {acc_label}")
    
    return "\n".join(lines)

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
            timestamp = f"{p.get('date_utc', '')} {p.get('time_utc', '')}".strip() or "ROV Point"
            description = format_metadata_description(p)
            kml.write(f"""<Placemark>
  <name>{timestamp}</name>
  <description><![CDATA[{description}]]></description>
  <Point><coordinates>{p['lon']},{p['lat']},0</coordinates></Point>
</Placemark>
""")
        kml.write('</Document>\n</kml>\n')
    print(f"Created {output_path}")

if __name__ == "__main__":
    main()
