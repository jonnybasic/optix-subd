# WGS84 Ellipsoid Terrain Viewer Implementation

This document describes the implementation of a WGS84 ellipsoid terrain viewer using DTED (Digital Terrain Elevation Data) files with the OptiX subdivision surface sample.

## Overview

The implementation extends the optix-subd sample to visualize Earth terrain data on a WGS84 ellipsoid, using DTED files as displacement maps for dynamic terrain rendering. The system uses Catmull-Clark subdivision surfaces for smooth ellipsoid geometry and implements a tile management system for efficient DTED loading.

## Architecture

### 1. Ellipsoid Mesh Generation (`scene/ellipsoid.h`, `scene/ellipsoid.cpp`)

**Purpose**: Generate a WGS84 ellipsoid base mesh suitable for subdivision surfaces.

**Key Components**:
- **WGS84 Constants**: Semi-major axis (a = 6378137.0m), semi-minor axis (b = 6356752.314245m)
- **`generateEllipsoidMesh()`**: Creates a parametric quad-based mesh
  - UV coordinates map to lat/lon ([0,1] → [-180°, 180°] for U, [-90°, 90°] for V)
  - Generates quad topology optimized for Catmull-Clark subdivision
  - Computes ellipsoidal surface normals (not sphere normals)
- **`geographicToCartesian()`**: Converts lat/lon/height to XYZ coordinates
- **`computeEllipsoidNormal()`**: Calculates proper ellipsoidal normals

**Design Decision**: The mesh is generated with triangles at the poles (unavoidable singularities) and quads elsewhere for optimal subdivision.

### 2. DTED File Reader (`texture/dtedReader.h`, `texture/dtedReader.cpp`)

**Purpose**: Parse DTED Level 0/1/2 terrain elevation files.

**Key Components**:
- **`DTEDHeader`**: Stores metadata (origin, spacing, dimensions, elevation range)
- **`DTEDReader`**: Main parser class
  - `parseUHL()`: User Header Label (80 bytes)
  - `parseDSI()`: Data Set Identification (648 bytes)
  - `parseACC()`: Accuracy record (2700 bytes)
  - `parseDataRecords()`: Column-major elevation data
- **`toTexture()`**: Converts int16 elevations to normalized float32 for GPU

**File Format**: Follows MIL-PRF-89020B specification
- Big-endian int16 elevation values in meters
- Column-major storage order
- Missing data sentinel: -32767

### 3. DTED Tile Manager (`texture/dtedTileManager.h`, `texture/dtedTileManager.cpp`)

**Purpose**: Dynamic loading and caching of DTED tiles based on camera position.

**Key Components**:
- **`DTEDTile`**: Represents a single 1°×1° tile
- **`DTEDTileManager`**: LRU cache manager
  - `update()`: Camera-based tile visibility and loading
  - `loadTile()`, `unloadLRUTile()`: Memory management
  - UDIM mapping: `UDIM = 1001 + (lon + 180) + (lat + 90) * 360`

**UDIM Scheme**: Extended UDIM standard provides unique ID for each degree tile:
- Standard UDIM: U=0-9, V=0-9 → IDs 1001-1100
- Extended: Global coverage → IDs 1001-66401 (360 × 180 tiles)

**Future Enhancement**: The current implementation provides the infrastructure but doesn't fully integrate texture streaming to GPU. A complete implementation would:
1. Use `TextureCache` to upload DTED textures to GPU
2. Bind textures to materials via UDIM indices
3. Update displacement samplers per-frame based on visibility

### 4. Command-Line Arguments (`args.h`, `args.cpp`)

**New Options**:
```bash
--ellipsoid                    # Enable ellipsoid terrain mode
--dted-dir <path>             # Path to DTED files directory
--elevation-scale <float>     # Displacement scale factor (default: 1.0)
--elevation-bias <float>      # Elevation bias offset (default: 0.0)
--max-tiles <int>            # Maximum loaded tiles (default: 64)
```

### 5. Scene Loading (`optixSubdApp.h`, `optixSubdApp.cpp`)

**Key Changes**:
- **`loadEllipsoidScene()`**: New function to initialize ellipsoid mode
  - Generates ellipsoid mesh programmatically
  - Creates DTED tile manager
  - Sets displacement parameters
- **Constructor modification**: Checks `ellipsoidMode` flag and calls appropriate loader

**Implementation Note**: Uses temporary OBJ file export for compatibility with existing Scene infrastructure. A more elegant solution would extend `Scene::create()` to accept programmatic mesh generation.

### 6. CMake Configuration

**Modified Files**:
- `scene/CMakeLists.txt`: Added `ellipsoid.cpp` and `ellipsoid.h`
- `texture/CMakeLists.txt`: Added DTED reader and tile manager sources
- Linked `OptiXToolkit::Gui` to texture library for Camera class

## Usage

### 1. Generate Test Data

```bash
python3 tools/generate_dted_test_data.py --output-dir /tmp/dted_test --level 0
```

This creates synthetic DTED files with various elevation patterns (mountain, gradient, sine, checkerboard).

### 2. Run Ellipsoid Viewer

```bash
./optixSubd --ellipsoid --dted-dir /tmp/dted_test \
    --elevation-scale 2.0 \
    --max-tiles 128 \
    --resolution 1920 1080
```

## Coordinate Systems

### Geographic → Cartesian Conversion

WGS84 ellipsoid with height above surface:
```
N = a / √(1 - e² sin²(lat))
x = (N + h) cos(lat) cos(lon)
y = (N + h) cos(lat) sin(lon)
z = (N(1 - e²) + h) sin(lat)

where:
  a = semi-major axis (6378137.0m)
  e² = first eccentricity squared
  h = height above ellipsoid
```

### UDIM Mapping

Maps geographic tiles to unique texture IDs:
```
UDIM = 1001 + (lon + 180) + (lat + 90) × 360

Examples:
  0°N, 0°E   → UDIM 33481 (1001 + 180 + 90×360)
  37°N, 122°W → UDIM 42859 (1001 + 58 + 127×360)
```

## Testing

### Unit Tests

**Not implemented** - No existing test infrastructure in repository.

Recommended tests:
1. **Ellipsoid Mesh**:
   - Verify vertex count: (latSegs - 1) × lonSegs + 2
   - Check UV range: [0, 1]
   - Validate AABB encompasses Earth radius
2. **DTED Reader**:
   - Parse synthetic files
   - Verify elevation extraction
   - Handle missing data (-32767)
3. **UDIM Conversion**:
   - Round-trip lat/lon → UDIM → lat/lon
   - Test boundary cases (±180°, ±90°)

### Visual Verification

Generate test DTED with known patterns:
```bash
# Mountain peak
python3 tools/generate_dted_test_data.py --pattern mountain

# Gradient
python3 tools/generate_dted_test_data.py --pattern gradient

# Checkerboard (for tile boundaries)
python3 tools/generate_dted_test_data.py --pattern checkerboard
```

## Known Limitations

1. **Texture Streaming**: DTED tiles are loaded but not yet uploaded to GPU for displacement. Requires integration with `TextureCache` and material system.

2. **Camera Control**: No specialized ellipsoid navigation (e.g., terrain-following, lat/lon input). Uses standard trackball camera.

3. **Pole Singularities**: Triangular faces at poles may cause tessellation artifacts.

4. **No LOD**: All tiles loaded at same resolution. Should implement distance-based LOD selection.

5. **Memory Management**: No texture compression. Large tile sets may exhaust GPU memory.

## Future Enhancements

### Phase 2: Complete Integration
- [ ] Bind DTED textures to displacement samplers
- [ ] Implement texture array for multi-tile displacement
- [ ] Add camera frustum culling for tile visibility
- [ ] Implement distance-based LOD

### Phase 3: Optimization
- [ ] Texture compression (BC4/BC5)
- [ ] Asynchronous tile loading
- [ ] Tile prefetching based on camera velocity
- [ ] Normal map generation from elevation

### Phase 4: Features
- [ ] Support for other formats (GeoTIFF, SRTM)
- [ ] Color/imagery overlay (Blue Marble, satellite)
- [ ] Geographic coordinate display
- [ ] Measurement tools (distance, elevation)

## References

- **DTED Specification**: MIL-PRF-89020B
- **WGS84**: NIMA TR8350.2 Third Edition
- **UDIM**: https://docs.pixar.com/display/REN/UDIMs
- **OptiX Programming Guide**: https://raytracing-docs.nvidia.com/optix9/
- **Catmull-Clark**: Original paper by Catmull & Clark (1978)

## Implementation Notes

### Why Temporary OBJ File?

The current Scene loading infrastructure expects file paths, not in-memory meshes. Options considered:

1. **Extend Scene::create()** to accept `std::unique_ptr<Shape>` (cleaner but requires more changes)
2. **Use temporary file** (current approach - minimal changes)
3. **Create EllipsoidScene subclass** (most elegant but largest refactor)

Chose option 2 for minimal code changes as specified in requirements.

### Ellipsoid vs Sphere

Must use proper ellipsoidal math:
- **Normals**: Not radial (sphere), but perpendicular to ellipsoid surface
- **Distance**: Geodesic distance on ellipsoid, not Euclidean
- **Height**: Measured perpendicular to ellipsoid, not from center

### DTED Column-Major Storage

DTED stores elevation posts in longitude columns (not row-major). When converting to row-major texture, must transpose:
```cpp
// DTED: column[lon][lat]
// Texture: row[lat][col]
texture[row * width + col] = dted_column[col].data[row];
```

## Build Requirements

- **CUDA Toolkit 12+**: For GPU texture operations
- **OptiX SDK**: Cluster API support (OptiX 9.0+)
- **C++20**: std::filesystem, concepts
- **CMake 3.25+**: Modern CMake features

## Troubleshooting

### "DTED directory does not exist"
Ensure path is correct and contains .dt0/.dt1/.dt2 files.

### "Failed to parse DTED"
Check file format. Use test generator to create valid files.

### Black/No Displacement
DTED tiles load but don't affect rendering. This is expected in current implementation - texture binding not yet complete.

### Slow Performance
Reduce `--max-tiles` or use lower resolution DTED (Level 0 vs Level 2).
