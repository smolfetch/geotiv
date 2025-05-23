// main.cpp

#include <iostream>
#include <vector>

#include "concord/types_basic.hpp"
#include "concord/types_grid.hpp"

#include "geotiv/types.hpp" // for geotiv::RasterCollection, Layer
#include "geotiv/writter.hpp"

int main() {
    try {
        // --- 1) Create or obtain your Grid<uint8_t> somehow ---
        // For demo, let’s make a 100×50 checkerboard:
        size_t rows = 50, cols = 100;
        double cellSize = 2.0;                   // meters per pixel
        concord::Datum datum{48.0, 11.0, 500.0}; // lat, lon, alt
        concord::Euler heading{0, 0, 0};         // no rotation
        // center grid at datum:
        concord::Pose shift{concord::Point{0, 0, 0}, heading};
        concord::Grid<uint8_t> grid(rows, cols, // rows×cols
                                    cellSize,   // diameter = resolution
                                    datum,
                                    /*centered=*/true, shift);
        // fill with a checkerboard
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                grid(r, c).second = ((r / 5 + c / 5) % 2) ? 255 : 0;
            }
        }

        // --- 2) Build a RasterCollection around it ---
        geotiv::RasterCollection rc;
        // pick your CRS/datum/heading/resolution
        rc.crs = concord::CRS::WGS;
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize; // map‐units per pixel

        // move the grid into a Layer and set its TIFF tags
        geotiv::Layer L;
        L.grid = std::move(grid);
        L.width = static_cast<uint32_t>(cols);
        L.height = static_cast<uint32_t>(rows);
        L.samplesPerPixel = 1; // single‐band
        L.planarConfig = 1;    // chunky
        rc.layers.clear();
        rc.layers.push_back(std::move(L));

        // --- 3) Flatten the grid’s samples into a chunky pixel buffer ---
        auto &g = rc.layers[0].grid;
        std::vector<uint8_t> pixelData;
        pixelData.reserve(rows * cols);
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                pixelData.push_back(g(r, c).second);
            }
        }

        // --- 4) Write out the GeoTIFF ---
        geotiv::WriteRasterCollection(rc, pixelData, "output.tif");
        std::cout << "Wrote GeoTIFF with " << cols << "×" << rows << " pixels at " << cellSize << "m/px → output.tif\n";
    } catch (std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
