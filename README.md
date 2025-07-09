<img align="right" width="26%" src="./misc/logo.png">

# Geotiv

A modern C++20 library for reading and writing GeoTIFF files with integrated geospatial coordinate system support.

## 🌟 Features

- **Complete GeoTIFF Support**: Read and write GeoTIFF files following the [GeoTIFF Specification](http://geotiff.maptools.org/spec/geotiff6.html)
- **Multi-layer/Multi-IFD Support**: Handle complex GeoTIFF files with multiple image layers, each IFD with its own tags
- **Per-IFD Geospatial Metadata**: Each layer can have independent coordinate systems, datums, and resolutions
- **Comprehensive Tag Support**: Full TIFF tag support including custom tags per IFD
- **ENU Shift-Based Positioning**: Use local ENU coordinates for precise spatial positioning with automatic WGS84 conversion
- **Geospatial Grid Conversion**: Automatic conversion between TIFF pixel data and georeferenced coordinate grids
- **8-bit Raster Support**: Optimized for 8-bit grayscale raster data
- **Header-only Library**: Easy integration with CMake FetchContent or as a Git submodule
- **Cross-platform**: Works on Linux, macOS, and Windows

## 🚀 Quick Start

### Basic Usage

```cpp
#include "geotiv/geotiv.hpp"

// Read a GeoTIFF file
auto rasterCollection = geotiv::ReadRasterCollection("input.tif");

// Access georeferenced data
const auto& layer = rasterCollection.layers[0];
const auto& grid = layer.grid;

// Get pixel value at specific grid coordinates
uint8_t pixelValue = grid(row, col);

// Write modified data back to GeoTIFF
geotiv::WriteRasterCollection(rasterCollection, "output.tif");
```

### Creating GeoTIFF from Scratch

```cpp
// Create a georeferenced raster using the high-level API
concord::Datum datum{48.0, 11.0, 500.0};                    // WGS84 reference point
concord::Pose shift{concord::Point{100.0, 200.0, 0.0},     // ENU position: 100m East, 200m North
                    concord::Euler{0, 0, 0}};               // No rotation
double resolution = 2.0;                                    // 2 meters per pixel

geotiv::Raster raster(datum, shift, resolution);

// Add a grid layer
uint32_t width = 200, height = 100;
raster.addGrid(width, height, "elevation", "terrain");

// Fill with data
auto& grid = raster.getGrid("elevation");
for (uint32_t r = 0; r < height; ++r) {
    for (uint32_t c = 0; c < width; ++c) {
        grid.grid(r, c) = computePixelValue(r, c);
    }
}

// Save as GeoTIFF
raster.toFile("generated.tif");
```

## 🔧 Core Components

### Data Structures

- **`geotiv::Raster`**: High-level interface for creating and managing georeferenced raster data
- **`geotiv::RasterCollection`**: Container for one or more georeferenced raster layers
- **`geotiv::Layer`**: Individual raster layer with pixel data and metadata
- **`concord::Grid<uint8_t>`**: Georeferenced grid with ENU shift-based positioning

### Multi-IFD (Image File Directory) Support

Geotiv provides comprehensive support for multi-IFD GeoTIFF files, where each IFD represents a separate image layer with its own metadata and geospatial properties:

#### IFD-Specific Features:
- **Independent Geospatial Parameters**: Each layer can have its own datum, ENU shift, and resolution
- **Per-IFD Tag Storage**: Every IFD maintains its own set of TIFF tags including:
  - Standard TIFF tags (ImageWidth, ImageLength, BitsPerSample, etc.)
  - GeoTIFF-specific tags (ModelPixelScaleTag, GeoKeyDirectoryTag, etc.)
  - Custom application-specific tags via `customTags` map
- **IFD Chain Management**: Proper linking and traversal of IFD chains in multi-layer files
- **Offset Tracking**: Each layer stores its original IFD offset for reference

#### Per-Layer Metadata:
```cpp
geotiv::Layer layer;
layer.datum = {47.5, 8.5, 200.0};       // WGS84 reference point (lat, lon, alt)
layer.shift = {concord::Point{100, 200, 0}, concord::Euler{0, 0, 0.5}}; // ENU position and rotation
layer.resolution = 2.0;                  // Meters per pixel
layer.imageDescription = "Layer 1 data"; // Custom description
layer.customTags[50000] = {42, 100};     // Custom application tags
```

#### Multi-Layer Example:
```cpp
geotiv::RasterCollection rc;

// Create multiple layers with different properties
for (int i = 0; i < 3; ++i) {
    geotiv::Layer layer;
    layer.datum = {47.0 + i * 0.1, 8.0 + i * 0.1, 100.0 + i * 50};
    layer.shift = {concord::Point{i * 50.0, i * 100.0, 0}, concord::Euler{0, 0, i * 0.1}};
    layer.resolution = 1.0 + i * 0.5;
    
    // Each layer gets its own grid and metadata
    layer.grid = createGridForLayer(i);
    layer.width = layer.grid.cols();
    layer.height = layer.grid.rows();
    
    rc.layers.push_back(std::move(layer));
}

// Write multi-IFD GeoTIFF - each layer becomes its own IFD
geotiv::WriteRasterCollection(rc, "multi_layer.tif");
```

### I/O Functions

- **`geotiv::ReadRasterCollection()`**: Parse GeoTIFF files into structured data
- **`geotiv::WriteRasterCollection()`**: Export raster collections to GeoTIFF format
- **`geotiv::toTiffBytes()`**: Generate raw TIFF byte data for custom handling

### Coordinate System Support

Geotiv uses a shift-based coordinate system for maximum flexibility and precision:

#### ENU Shift-Based Positioning
- **WGS84 Reference**: All coordinate systems use WGS84 with EPSG:4326
- **ENU Shift**: Local positioning in East-North-Up coordinates relative to datum
- **Automatic Conversion**: ENU coordinates automatically converted to WGS84 for GeoTIFF storage

#### Key Features:
- **Datum**: WGS84 reference point for coordinate transformations
- **Shift**: ENU position and orientation (concord::Pose) for precise grid placement
- **Automatic georeferencing**: ModelTiepointTag maps image center to calculated WGS84 coordinates
- **QGIS Compatible**: Proper georeferencing tags for seamless GIS integration

```cpp
// Coordinate system setup
concord::Datum datum{47.5, 8.5, 200.0};                    // WGS84 reference point
concord::Pose shift{concord::Point{100.0, 200.0, 0.0},     // ENU position: 100m East, 200m North
                    concord::Euler{0, 0, 0.5}};             // 0.5 radians yaw rotation

// The raster will be positioned at datum + shift in ENU space
geotiv::Raster raster(datum, shift, resolution);
```

## 📊 Supported Features

| Feature | Support | Per-IFD |
|---------|---------|---------|
| Reading GeoTIFF | ✅ Full | ✅ Yes |
| Writing GeoTIFF | ✅ Full | ✅ Yes |
| Multi-layer files | ✅ Yes | ✅ Independent |
| 8-bit grayscale | ✅ Yes | ✅ Per layer |
| WGS84 coordinates | ✅ Yes | ✅ Standard |
| ENU shift positioning | ✅ Yes | ✅ Per layer |
| EPSG:4326 support | ✅ Yes | ✅ Automatic |
| ModelTiepointTag | ✅ Yes | ✅ Per layer |
| QGIS compatibility | ✅ Yes | ✅ Full |
| Pixel scaling | ✅ Yes | ✅ Per layer |
| Strip-based TIFF | ✅ Yes | ✅ Yes |
| Little/Big endian | ✅ Both | ✅ Yes |
| Custom TIFF tags | ✅ Yes | ✅ Per IFD |
| Independent datums | ✅ Yes | ✅ Per layer |

### TIFF Tag Support

The library supports comprehensive TIFF tag handling:

#### Standard TIFF Tags (per IFD):
- **256**: ImageWidth
- **257**: ImageLength  
- **258**: BitsPerSample
- **259**: Compression
- **262**: PhotometricInterpretation
- **270**: ImageDescription (with geospatial metadata)
- **273**: StripOffsets
- **277**: SamplesPerPixel
- **278**: RowsPerStrip
- **279**: StripByteCounts
- **284**: PlanarConfiguration

#### GeoTIFF Tags (per IFD):
- **33550**: ModelPixelScaleTag (pixel scale in X, Y, Z)
- **33922**: ModelTiepointTag (image-to-world coordinate mapping)
- **34735**: GeoKeyDirectoryTag (coordinate system keys including EPSG:4326)
- **34736**: GeoDoubleParamsTag (floating-point parameters)
- **34737**: GeoAsciiParamsTag (string parameters)

#### Custom Tag Support:
```cpp
// Add custom tags to any IFD
layer.customTags[50000] = {42, 100, 255};     // Custom numeric data
layer.customTags[50001] = {timestamp_value};   // Application-specific tags
```

## 🏗️ Building

### CMake Integration

```cmake
include(FetchContent)
FetchContent_Declare(
    geotiv
    GIT_REPOSITORY https://github.com/your-org/geotiv.git
    GIT_TAG main
)
FetchContent_MakeAvailable(geotiv)

target_link_libraries(your_target geotiv::geotiv)
```

### Manual Build

```bash
git clone https://github.com/your-org/geotiv.git
cd geotiv
mkdir build && cd build
cmake -DGEOTIV_BUILD_EXAMPLES=ON -DGEOTIV_ENABLE_TESTS=ON ..
make -j$(nproc)
```

## 🧪 Testing

The library includes comprehensive tests covering:

- Round-trip file I/O operations
- Multi-layer GeoTIFF handling
- Coordinate system transformations
- TIFF format validation
- Error handling scenarios

```bash
make test
```

## 📋 Dependencies

- **[Concord](https://github.com/smolfetch/concord)**: Coordinate system transformations and geodetic calculations
- **[Pigment](https://github.com/smolfetch/pigment)**: Color and pixel data handling
- **C++20 compiler**: Modern C++ features for performance and safety

## 📖 Use Cases

### Advanced Multi-IFD Scenarios

- **Time-Series Data**: Store temporal raster datasets with different timestamps per IFD
- **Multi-Resolution Pyramids**: Create resolution pyramids with different scales per layer
- **Multi-Spectral Imagery**: Handle different spectral bands with independent geospatial parameters
- **Precise Spatial Positioning**: Use ENU shift coordinates for sub-meter accuracy
- **Metadata Preservation**: Maintain application-specific tags and metadata per image layer

### Example: Time-Series Multi-IFD GeoTIFF
```cpp
geotiv::RasterCollection timeSeries;

for (const auto& timestamp : timestamps) {
    geotiv::Layer layer;
    layer.datum = surveyLocation;
    layer.shift = {concord::Point{0, 0, 0}, concord::Euler{0, 0, 0}}; // No spatial offset
    layer.resolution = 0.5; // 50cm resolution
    
    // Store timestamp in custom tag
    layer.customTags[50100] = {static_cast<uint32_t>(timestamp)};
    layer.imageDescription = "Survey data " + formatTimestamp(timestamp);
    
    // Load raster data for this time point
    layer.grid = loadRasterForTime(timestamp);
    layer.width = layer.grid.cols();
    layer.height = layer.grid.rows();
    
    timeSeries.layers.push_back(std::move(layer));
}

geotiv::WriteRasterCollection(timeSeries, "temporal_survey.tif");
```

## 🌐 Shift-Based Coordinate System

Geotiv uses a sophisticated shift-based coordinate system that provides both precision and flexibility:

### **Datum vs Shift**
- **Datum**: WGS84 reference point (lat, lon, alt) for coordinate transformations
- **Shift**: ENU position and orientation relative to datum for precise grid placement

### **Coordinate Flow**
1. **Grid Creation**: Uses ENU shift directly for `concord::Grid` positioning
2. **GeoTIFF Storage**: Converts ENU shift to WGS84 for ModelTiepointTag
3. **GIS Integration**: QGIS reads ModelTiepointTag for proper georeferencing

### **Example: Precise Positioning**
```cpp
// Survey area with known WGS84 reference
concord::Datum surveyDatum{52.1234, 5.6789, 45.0};

// Position grid 150m East, 300m North of datum, rotated 15 degrees
concord::Pose gridShift{concord::Point{150.0, 300.0, 0.0}, 
                        concord::Euler{0, 0, 0.262}};  // 15 degrees in radians

geotiv::Raster raster(surveyDatum, gridShift, 0.5);  // 50cm resolution
```

### **Storage Format**
- **ImageDescription**: `"CRS WGS84 DATUM lat lon alt SHIFT x y z yaw"`
- **ModelTiepointTag**: Maps image center to WGS84 coordinates
- **Automatic Conversion**: ENU coordinates converted to WGS84 using datum

### Traditional Use Cases

- **Geospatial Analysis**: Process satellite imagery and aerial photography
- **Terrain Modeling**: Work with Digital Elevation Models (DEMs)
- **Environmental Monitoring**: Handle time-series raster data
- **GIS Applications**: Integrate with geographic information systems
- **Scientific Computing**: Process research datasets with spatial components

## 🤝 Contributing

Contributions are welcome! Please see our contributing guidelines and ensure all tests pass before submitting pull requests.