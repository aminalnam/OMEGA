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


def default_output_prefix(input_path):
    base, _ = os.path.splitext(input_path)
    return f"{base}_overlay"


def format_metadata_description(point):
    """Generate a description for a KML placemark with all sensor metadata."""
    # Use simple text format without HTML for better Google Earth compatibility
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
    
    # Position
    lines.append("")
    lines.append("Position:")
    lines.append(f"  Latitude: {point['lat']:.6f}")
    lines.append(f"  Longitude: {point['lon']:.6f}")
    
    return "\n".join(lines)



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
    <Style id="measurementPointStyle">
      <IconStyle>
        <Icon>
          <href>http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png</href>
        </Icon>
        <scale>0.6</scale>
      </IconStyle>
    </Style>
    <GroundOverlay>
      <name>Bathymetric Interpolated Surface</name>
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
    <Folder>
      <name>Measurement Points</name>
      <visibility>1</visibility>
'''
        # Add placemarks for each measurement point
        for idx, point in enumerate(points, 1):
            timestamp = f"{point.get('date_utc', '')} {point.get('time_utc', '')}".strip() or f"Point {idx}"
            description = format_metadata_description(point)
            
            kml += f'''      <Placemark>
        <name>{timestamp}</name>
        <description><![CDATA[{description}]]></description>
        <styleUrl>#measurementPointStyle</styleUrl>
        <Point>
          <coordinates>{point['lon']},{point['lat']},0</coordinates>
        </Point>
      </Placemark>
'''
        
        kml += '''    </Folder>
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
