<img align="right" width="26%" src="./misc/logo.png">

# Geotiv

A modern C++20 library for reading and writing GeoTIFF files with integrated geospatial coordinate system support.

## 🌟 Features

- **Complete GeoTIFF Support**: Read and write GeoTIFF files following the [GeoTIFF Specification](http://geotiff.maptools.org/spec/geotiff6.html)
- **Multi-layer/Multi-IFD Support**: Handle complex GeoTIFF files with multiple image layers, each IFD with its own tags
- **Per-IFD Geospatial Metadata**: Each layer can have independent coordinate systems, datums, and resolutions
- **Comprehensive Tag Support**: Full TIFF tag support including custom tags per IFD
- **Coordinate System Integration**: Built-in support for WGS84 and ENU coordinate systems via Concord
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
uint8_t pixelValue = grid(row, col).second;

// Write modified data back to GeoTIFF
geotiv::WriteRasterCollection(rasterCollection, "output.tif");
```

### Creating GeoTIFF from Scratch

```cpp
// Create a georeferenced grid
size_t rows = 100, cols = 200;
double cellSize = 2.0; // meters per pixel
concord::Datum datum{48.0, 11.0, 500.0}; // lat, lon, alt
concord::Euler heading{0, 0, 0}; // no rotation

concord::Grid<uint8_t> grid(rows, cols, cellSize, datum, true);

// Fill with data (e.g., elevation, classification, etc.)
for (size_t r = 0; r < rows; ++r) {
    for (size_t c = 0; c < cols; ++c) {
        grid(r, c).second = computePixelValue(r, c);
    }
}

// Package into RasterCollection
geotiv::RasterCollection rc;

geotiv::Layer layer;
layer.grid = std::move(grid);
layer.width = cols;
layer.height = rows;
layer.samplesPerPixel = 1;
// Set per-layer geospatial metadata
layer.crs = geotiv::CRS::WGS;
layer.datum = datum;
layer.heading = heading;
layer.resolution = cellSize;

rc.layers.push_back(std::move(layer));

// Write to GeoTIFF
geotiv::WriteRasterCollection(rc, "generated.tif");
```

## 🔧 Core Components

### Data Structures

- **`geotiv::RasterCollection`**: Container for one or more georeferenced raster layers
- **`geotiv::Layer`**: Individual raster layer with pixel data and metadata
- **`concord::Grid<uint8_t>`**: Georeferenced grid with coordinate system integration

### Multi-IFD (Image File Directory) Support

Geotiv provides comprehensive support for multi-IFD GeoTIFF files, where each IFD represents a separate image layer with its own metadata and geospatial properties:

#### IFD-Specific Features:
- **Independent Geospatial Parameters**: Each layer can have its own coordinate system (CRS), datum, heading, and resolution
- **Per-IFD Tag Storage**: Every IFD maintains its own set of TIFF tags including:
  - Standard TIFF tags (ImageWidth, ImageLength, BitsPerSample, etc.)
  - GeoTIFF-specific tags (ModelPixelScaleTag, GeoKeyDirectoryTag, etc.)
  - Custom application-specific tags via `customTags` map
- **IFD Chain Management**: Proper linking and traversal of IFD chains in multi-layer files
- **Offset Tracking**: Each layer stores its original IFD offset for reference

#### Per-Layer Metadata:
```cpp
geotiv::Layer layer;
layer.crs = geotiv::CRS::WGS;           // Coordinate reference system
layer.datum = {47.5, 8.5, 200.0};       // Lat, lon, altitude
layer.heading = {0, 0, 30};              // Roll, pitch, yaw rotation
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
    layer.crs = (i == 0) ? geotiv::CRS::WGS : geotiv::CRS::ENU;
    layer.datum = {47.0 + i * 0.1, 8.0 + i * 0.1, 100.0 + i * 50};
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

Geotiv supports two coordinate system flavors for GeoTIFF files:

#### Coordinate Reference Systems (CRS)
- **`geotiv::CRS::WGS`**: World Geodetic System 1984 (geographic coordinates)
  - Data stored in latitude/longitude coordinates
  - Converted to local ENU space during parsing for grid operations
- **`geotiv::CRS::ENU`**: East-North-Up local coordinate system
  - Data already in local space coordinates
  - No conversion needed during parsing

#### Key Features:
- **Datum always required**: Provides the reference point for coordinate transformations
- **Per-layer CRS**: Each IFD can have its own coordinate system
- **Automatic conversion**: Proper handling of coordinate transformations during I/O
- **Flexible workflows**: Mix WGS and ENU layers in the same file

```cpp
// WGS flavor - geographic coordinates
layer.crs = geotiv::CRS::WGS;
layer.datum = {47.5, 8.5, 200.0}; // lat, lon, alt reference

// ENU flavor - local coordinates  
layer.crs = geotiv::CRS::ENU;
layer.datum = {47.5, 8.5, 200.0}; // still need datum as reference
```

## 📊 Supported Features

| Feature | Support | Per-IFD |
|---------|---------|---------|
| Reading GeoTIFF | ✅ Full | ✅ Yes |
| Writing GeoTIFF | ✅ Full | ✅ Yes |
| Multi-layer files | ✅ Yes | ✅ Independent |
| 8-bit grayscale | ✅ Yes | ✅ Per layer |
| WGS84 coordinates | ✅ Yes | ✅ Per layer |
| ENU coordinates | ✅ Yes | ✅ Per layer |
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
- **34735**: GeoKeyDirectoryTag (coordinate system keys)
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
- **Heterogeneous Coordinate Systems**: Mix WGS84 and local coordinate systems in the same file
- **Metadata Preservation**: Maintain application-specific tags and metadata per image layer

### Example: Time-Series Multi-IFD GeoTIFF
```cpp
geotiv::RasterCollection timeSeries;

for (const auto& timestamp : timestamps) {
    geotiv::Layer layer;
    layer.crs = geotiv::CRS::WGS;
    layer.datum = surveyLocation;
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

### Traditional Use Cases

- **Geospatial Analysis**: Process satellite imagery and aerial photography
- **Terrain Modeling**: Work with Digital Elevation Models (DEMs)
- **Environmental Monitoring**: Handle time-series raster data
- **GIS Applications**: Integrate with geographic information systems
- **Scientific Computing**: Process research datasets with spatial components

## 🤝 Contributing

Contributions are welcome! Please see our contributing guidelines and ensure all tests pass before submitting pull requests.