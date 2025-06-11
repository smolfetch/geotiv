
<img align="right" width="26%" src="./misc/logo.png">

# Geotiv

A modern C++20 library for reading and writing GeoTIFF files with integrated geospatial coordinate system support.

## 🌟 Features

- **Complete GeoTIFF Support**: Read and write GeoTIFF files following the [GeoTIFF Specification](http://geotiff.maptools.org/spec/geotiff6.html)
- **Multi-layer/Multi-IFD Support**: Handle complex GeoTIFF files with multiple image layers
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
auto rasterCollection = geotiff::ReadRasterCollection("input.tif");

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
rc.crs = concord::CRS::WGS;
rc.datum = datum;
rc.resolution = cellSize;

geotiv::Layer layer;
layer.grid = std::move(grid);
layer.width = cols;
layer.height = rows;
layer.samplesPerPixel = 1;
rc.layers.push_back(std::move(layer));

// Write to GeoTIFF
geotiv::WriteRasterCollection(rc, "generated.tif");
```

## 🔧 Core Components

### Data Structures

- **`geotiv::RasterCollection`**: Container for one or more georeferenced raster layers
- **`geotiv::Layer`**: Individual raster layer with pixel data and metadata
- **`concord::Grid<uint8_t>`**: Georeferenced grid with coordinate system integration

### I/O Functions

- **`geotiff::ReadRasterCollection()`**: Parse GeoTIFF files into structured data
- **`geotiv::WriteRasterCollection()`**: Export raster collections to GeoTIFF format
- **`geotiv::toTiffBytes()`**: Generate raw TIFF byte data for custom handling

### Coordinate System Support

- **WGS84**: World Geodetic System 1984 (geographic coordinates)
- **ENU**: East-North-Up local coordinate system
- **Automatic transformation**: Between pixel coordinates and real-world positions

## 📊 Supported Features

| Feature | Support |
|---------|---------|
| Reading GeoTIFF | ✅ Full |
| Writing GeoTIFF | ✅ Full |
| Multi-layer files | ✅ Yes |
| 8-bit grayscale | ✅ Yes |
| WGS84 coordinates | ✅ Yes |
| ENU coordinates | ✅ Yes |
| Pixel scaling | ✅ Yes |
| Strip-based TIFF | ✅ Yes |
| Little/Big endian | ✅ Both |

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

- **Geospatial Analysis**: Process satellite imagery and aerial photography
- **Terrain Modeling**: Work with Digital Elevation Models (DEMs)
- **Environmental Monitoring**: Handle time-series raster data
- **GIS Applications**: Integrate with geographic information systems
- **Scientific Computing**: Process research datasets with spatial components

## 🤝 Contributing

Contributions are welcome! Please see our contributing guidelines and ensure all tests pass before submitting pull requests.
