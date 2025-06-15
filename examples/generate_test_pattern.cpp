// generate_test_pattern.cpp
// Creates a 640x640 GeoTIFF with a recognizable test pattern for image viewer compatibility testing

#include <cmath>
#include <iostream>

#include "concord/concord.hpp"
#include "geotiv/geotiv.hpp"

int main() {
    try {
        std::cout << "Generating 640x640 test pattern GeoTIFF...\n";

        // Image dimensions
        size_t rows = 640, cols = 640;
        double cellSize = 1.0; // 1 meter per pixel

        // Use a real-world location
        concord::Datum datum{46.8182, 8.2275, 1000.0}; // lat, lon, alt
        concord::Euler heading{0, 0, 0};               // no rotation
        concord::Pose shift{concord::Point{0, 0, 0}, heading};

        // Create the grid
        concord::Grid<uint8_t> grid(rows, cols, cellSize, datum, true, shift);

        std::cout << "Creating test pattern...\n";

        // Create a recognizable test pattern
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                uint8_t value = 0;

                // Create different patterns in different quadrants
                if (r < rows / 2 && c < cols / 2) {
                    // Top-left: Checkerboard pattern
                    value = ((r / 16 + c / 16) % 2) ? 255 : 64;
                } else if (r < rows / 2 && c >= cols / 2) {
                    // Top-right: Horizontal stripes
                    value = (r / 8 % 2) ? 200 : 100;
                } else if (r >= rows / 2 && c < cols / 2) {
                    // Bottom-left: Vertical stripes
                    value = (c / 8 % 2) ? 180 : 80;
                } else {
                    // Bottom-right: Concentric circles
                    double center_r = rows * 0.75;
                    double center_c = cols * 0.75;
                    double dist = std::sqrt((r - center_r) * (r - center_r) + (c - center_c) * (c - center_c));
                    value = static_cast<uint8_t>(128 + 127 * std::sin(dist / 10.0));
                }

                grid(r, c).second = value;
            }
        }

        // Create RasterCollection
        geotiv::RasterCollection rc;
        rc.crs = geotiv::CRS::WGS;
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        // Create layer
        geotiv::Layer layer;
        layer.grid = std::move(grid);
        layer.width = static_cast<uint32_t>(cols);
        layer.height = static_cast<uint32_t>(rows);
        layer.samplesPerPixel = 1; // grayscale
        layer.planarConfig = 1;    // chunky format

        // Set per-layer geospatial metadata
        layer.crs = geotiv::CRS::WGS;
        layer.datum = datum;
        layer.heading = heading;
        layer.resolution = cellSize;

        rc.layers.push_back(std::move(layer));

        // Write the GeoTIFF
        std::string filename = "test_pattern_640x640.tif";
        geotiv::WriteRasterCollection(rc, filename);

        std::cout << "✅ Successfully created: " << filename << "\n";
        std::cout << "   Size: 640x640 pixels\n";
        std::cout << "   Type: 8-bit grayscale\n";
        std::cout << "   Format: Standard TIFF with GeoTIFF tags\n";
        std::cout << "\nPattern layout:\n";
        std::cout << "   ┌─────────────┬─────────────┐\n";
        std::cout << "   │ Checkerboard│ Horizontal  │\n";
        std::cout << "   │   pattern   │   stripes   │\n";
        std::cout << "   ├─────────────┼─────────────┤\n";
        std::cout << "   │  Vertical   │ Concentric  │\n";
        std::cout << "   │   stripes   │   circles   │\n";
        std::cout << "   └─────────────┴─────────────┘\n";
        std::cout << "\nThis should be viewable in any TIFF-compatible image viewer!\n";

    } catch (const std::exception &e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
