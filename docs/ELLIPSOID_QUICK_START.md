# WGS84 Ellipsoid Terrain Viewer - Quick Start

This document provides quick instructions for using the new ellipsoid terrain viewer feature.

## What's New?

The optix-subd sample now supports visualizing Earth terrain data on a WGS84 ellipsoid using DTED (Digital Terrain Elevation Data) files as displacement maps.

## Quick Start

### 1. Generate Test Data

```bash
cd optix-subd
python3 tools/generate_dted_test_data.py --output-dir /tmp/dted_test
```

This creates a synthetic DTED file at `/tmp/dted_test/n37w122.dt0` with a mountain-shaped elevation pattern.

### 2. Build (if not already built)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
```

### 3. Run Ellipsoid Viewer

```bash
./bin/optixSubd --ellipsoid --dted-dir /tmp/dted_test
```

## Command-Line Options

```bash
./optixSubd --ellipsoid --dted-dir <path> [options]

Required:
  --ellipsoid              Enable ellipsoid terrain mode
  --dted-dir <path>        Path to directory containing DTED files

Optional:
  --elevation-scale <f>    Scale factor for elevation (default: 1.0)
  --elevation-bias <f>     Elevation bias offset (default: 0.0)
  --max-tiles <n>         Maximum DTED tiles to cache (default: 64)
  --resolution <w> <h>    Render resolution (default: 1920x1080)
  --frames <n>            Number of frames for batch output
  --output <file>         Output image file
```

## Test Data Patterns

Generate different elevation patterns for testing:

```bash
# Mountain peak (default)
python3 tools/generate_dted_test_data.py --pattern mountain

# North-south gradient
python3 tools/generate_dted_test_data.py --pattern gradient

# Sine wave
python3 tools/generate_dted_test_data.py --pattern sine

# Checkerboard (for tile boundaries)
python3 tools/generate_dted_test_data.py --pattern checkerboard
```

## DTED Levels

Choose resolution level:

```bash
# Level 0: 30 arc-second posts (121x121), ~34KB per tile
python3 tools/generate_dted_test_data.py --level 0

# Level 1: 3 arc-second posts (1201x1201), ~2.9MB per tile
python3 tools/generate_dted_test_data.py --level 1

# Level 2: 1 arc-second posts (3601x3601), ~26MB per tile
python3 tools/generate_dted_test_data.py --level 2
```

## Custom Locations

Generate tiles for specific coordinates:

```bash
# Mount Everest region (27°N, 86°E)
python3 tools/generate_dted_test_data.py --lat 27 --lon 86 --output-dir /tmp/everest

# Grand Canyon (36°N, 112°W)
python3 tools/generate_dted_test_data.py --lat 36 --lon -112 --output-dir /tmp/canyon
```

## Using Real DTED Data

If you have real DTED files:

1. Organize them in a directory:
   ```
   /data/dted/
   ├── n37w122.dt2
   ├── n37w121.dt2
   ├── n38w122.dt2
   └── ...
   ```

2. Run the viewer:
   ```bash
   ./optixSubd --ellipsoid --dted-dir /data/dted --elevation-scale 1.5
   ```

## DTED File Naming Convention

Files must follow the standard DTED naming:
- Format: `[n|s]YY[e|w]XXX.dt[0|1|2]`
- Examples:
  - `n37w122.dt2` - 37°N, 122°W, Level 2
  - `s45e172.dt1` - 45°S, 172°E, Level 1
  - `n00e000.dt0` - 0°N, 0°E, Level 0

## Troubleshooting

### "DTED directory does not exist"
**Solution**: Verify the path and ensure it contains `.dt0`, `.dt1`, or `.dt2` files.

### "Failed to parse DTED"
**Solution**: Check file format. Use the test generator to create valid files.

### Black screen / No terrain
**Solution**: This is expected in current implementation. The ellipsoid mesh loads correctly, but DTED displacement texture binding is not yet complete. The infrastructure is in place for future integration.

### Poor performance
**Solution**: 
- Reduce `--max-tiles` value
- Use lower resolution DTED (Level 0 instead of Level 2)
- Lower screen resolution

## Examples

### Basic rendering
```bash
./optixSubd --ellipsoid --dted-dir /tmp/dted_test
```

### High-quality output
```bash
./optixSubd \
    --ellipsoid \
    --dted-dir /tmp/dted_test \
    --elevation-scale 2.0 \
    --resolution 3840 2160 \
    --frames 1 \
    --output earth_hires.png
```

### Batch rendering
```bash
./optixSubd \
    --ellipsoid \
    --dted-dir /tmp/dted_test \
    --frames 60 \
    --output animation/frame
```

## What Works

✓ Ellipsoid mesh generation with proper WGS84 dimensions
✓ DTED file parsing (Levels 0, 1, 2)
✓ Tile discovery and loading
✓ Command-line interface
✓ Test data generation

## Known Limitations

The current implementation provides the core infrastructure but has some limitations:

1. **Texture Binding**: DTED tiles load but aren't yet bound to GPU displacement samplers
2. **Camera Controls**: Standard trackball camera (no specialized globe navigation)
3. **Frustum Culling**: Placeholder (all tiles considered visible)
4. **LOD**: No distance-based level-of-detail selection

These are planned for future enhancements. See `docs/IMPLEMENTATION_SUMMARY.md` for details.

## Documentation

For more details, see:
- **Implementation Guide**: `docs/ELLIPSOID_IMPLEMENTATION.md`
- **Complete Summary**: `docs/IMPLEMENTATION_SUMMARY.md`
- **Main README**: `README.md`

## Need Help?

Check the documentation files above or open an issue on GitHub with:
- Command line you're using
- DTED file details (level, location)
- Error messages or unexpected behavior
- System specs (GPU, driver version)

## Next Steps

Once basic viewing works:
1. Try different elevation scales to see the effect
2. Generate multiple adjacent tiles to test caching
3. Experiment with different DTED patterns
4. Provide feedback for Phase 2 GPU integration

---

**Note**: This feature is in active development. The current release provides the foundation for DTED-based terrain viewing, with complete GPU displacement mapping planned for a future update.
