// generate_random_image.cpp
// Creates a 640x640 GeoTIFF with random pixel values for testing image viewer compatibility

#include <chrono>
#include <iostream>
#include <random>

#include "concord/concord.hpp"
#include "geotiv/geotiv.hpp"

int main() {
    try {
        std::cout << "Generating 640x640 random GeoTIFF image...\n";

        // Image dimensions
        size_t rows = 640, cols = 640;
        double cellSize = 1.0; // 1 meter per pixel

        // Use a real-world location (somewhere in Switzerland)
        concord::Datum datum{46.8182, 8.2275, 1000.0}; // lat, lon, alt
        concord::Euler heading{0, 0, 0};               // no rotation
        concord::Pose shift{concord::Point{0, 0, 0}, heading};

        // Create the grid
        concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

        // Initialize random number generator
        auto seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::mt19937 gen(static_cast<unsigned int>(seed));
        std::uniform_int_distribution<int> dis(0, 255);

        std::cout << "Filling with random values...\n";

        // Fill with random values
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                grid(r, c) = static_cast<uint8_t>(dis(gen));
            }
        }

        // Create RasterCollection with proper metadata
        geotiv::RasterCollection rc;
        rc.crs = geotiv::CRS::WGS;
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        // Create layer with the same metadata
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
        std::string filename = "random_640x640.tif";
        geotiv::WriteRasterCollection(rc, filename);

        std::cout << "✅ Successfully created: " << filename << "\n";
        std::cout << "   Size: 640x640 pixels\n";
        std::cout << "   Type: 8-bit grayscale\n";
        std::cout << "   Format: GeoTIFF with WGS84 coordinates\n";
        std::cout << "\nTry opening with your image viewer:\n";
        std::cout << "   - GIMP: gimp " << filename << "\n";
        std::cout << "   - ImageMagick: display " << filename << "\n";
        std::cout << "   - QGIS: qgis " << filename << "\n";
        std::cout << "   - Or any other TIFF-compatible viewer\n";

    } catch (const std::exception &e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
