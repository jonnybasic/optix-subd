#!/usr/bin/env python3
"""
DTED Test Data Generator

Generates synthetic DTED Level 0 files for testing the ellipsoid terrain viewer.
"""

import struct
import os
from pathlib import Path


def create_dted_header(lat, lon, num_lat_points, num_lon_points, lat_interval, lon_interval):
    """Create DTED User Header Label (UHL) record - 80 bytes."""
    uhl = bytearray(80)
    uhl[0:4] = b'UHL1'
    
    # Origin longitude/latitude
    lon_hem = 'E' if lon >= 0 else 'W'
    lat_hem = 'N' if lat >= 0 else 'S'
    uhl[4:12] = f"{abs(lon):03d}0000{lon_hem}".encode('ascii')
    uhl[12:20] = f"{abs(lat):03d}0000{lat_hem}".encode('ascii')
    
    # Intervals
    uhl[20:24] = f"{lon_interval * 10:04d}".encode('ascii')
    uhl[24:28] = f"{lat_interval * 10:04d}".encode('ascii')
    uhl[28:32] = b'0000'
    
    # Counts
    uhl[47:51] = f"{num_lat_points:04d}".encode('ascii')
    uhl[51:55] = f"{num_lon_points:04d}".encode('ascii')
    
    # Fill with spaces
    for i in range(55, 80):
        if uhl[i] == 0:
            uhl[i] = ord(' ')
    return bytes(uhl)


def create_dsi_record():
    """Create DSI record - 648 bytes."""
    dsi = bytearray(648)
    dsi[0:3] = b'DSI'
    for i in range(3, 648):
        dsi[i] = ord(' ')
    return bytes(dsi)


def create_acc_record():
    """Create ACC record - 2700 bytes."""
    acc = bytearray(2700)
    acc[0:3] = b'ACC'
    for i in range(3, 2700):
        acc[i] = ord(' ')
    return bytes(acc)


def generate_elevation_data(num_lat_points, num_lon_points, pattern='mountain'):
    """Generate synthetic elevation data."""
    import math
    elevations = []
    
    for row in range(num_lat_points):
        row_data = []
        for col in range(num_lon_points):
            if pattern == 'mountain':
                center_row, center_col = num_lat_points / 2, num_lon_points / 2
                dist = math.sqrt((row - center_row)**2 + (col - center_col)**2)
                max_dist = math.sqrt(center_row**2 + center_col**2)
                elev = int(3000 * (1 - min(dist / max_dist, 1.0)))
            else:
                elev = int((row / num_lat_points) * 2000)
            row_data.append(max(-32767, min(32767, elev)))
        elevations.append(row_data)
    return elevations


def create_data_records(elevations):
    """Create DTED data records (column-major order)."""
    num_lat_points = len(elevations)
    num_lon_points = len(elevations[0]) if num_lat_points > 0 else 0
    records = bytearray()
    
    for col in range(num_lon_points):
        # 8-byte header
        record_header = bytearray(8)
        record_header[0] = 0xAA
        struct.pack_into('>H', record_header, 1, 0)
        struct.pack_into('>H', record_header, 3, col)
        struct.pack_into('>H', record_header, 5, 0)
        records.extend(record_header)
        
        # Elevation data
        for row in range(num_lat_points):
            records.extend(struct.pack('>h', elevations[row][col]))
        
        # Checksum
        records.extend(bytes(4))
    return bytes(records)


def create_dted_file(output_path, lat, lon, pattern='mountain', level=0):
    """Create a synthetic DTED file."""
    if level == 0:
        lat_interval, lon_interval = 30, 30
        num_lat_points, num_lon_points = 121, 121
    elif level == 1:
        lat_interval, lon_interval = 3, 3
        num_lat_points, num_lon_points = 1201, 1201
    else:
        lat_interval, lon_interval = 1, 1
        num_lat_points, num_lon_points = 3601, 3601
    
    print(f"Generating DTED Level {level}: {output_path}")
    print(f"  Tile: {lat}°, {lon}° ({num_lat_points}x{num_lon_points} posts)")
    
    uhl = create_dted_header(lat, lon, num_lat_points, num_lon_points, lat_interval, lon_interval)
    dsi = create_dsi_record()
    acc = create_acc_record()
    elevations = generate_elevation_data(num_lat_points, num_lon_points, pattern)
    data_records = create_data_records(elevations)
    
    with open(output_path, 'wb') as f:
        f.write(uhl)
        f.write(dsi)
        f.write(acc)
        f.write(data_records)
    
    print(f"  Created: {os.path.getsize(output_path):,} bytes")


def main():
    """Generate sample DTED test files."""
    import argparse
    parser = argparse.ArgumentParser(description='Generate synthetic DTED test files')
    parser.add_argument('--output-dir', default='/tmp/dted_test', help='Output directory')
    parser.add_argument('--lat', type=int, default=37, help='Latitude (degrees)')
    parser.add_argument('--lon', type=int, default=-122, help='Longitude (degrees)')
    parser.add_argument('--pattern', default='mountain', help='Elevation pattern')
    parser.add_argument('--level', type=int, choices=[0, 1, 2], default=0, help='DTED level')
    
    args = parser.parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    lat_hem = 'n' if args.lat >= 0 else 's'
    lon_hem = 'e' if args.lon >= 0 else 'w'
    filename = f"{lat_hem}{abs(args.lat):02d}{lon_hem}{abs(args.lon):03d}.dt{args.level}"
    output_path = output_dir / filename
    
    create_dted_file(output_path, args.lat, args.lon, args.pattern, args.level)
    print(f"\nUse with: ./optixSubd --ellipsoid --dted-dir {output_dir}")


if __name__ == '__main__':
    main()
