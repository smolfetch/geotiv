// main.cpp

#include <iostream>

#include "concord/concord.hpp" // Datum, Euler
#include "geotiv/geotiv.hpp"   // RasterCollection, Layer

int main() {
    try {
        // --- 1) Create your Grid<uint8_t> (e.g. a 100×50 checkerboard) ---
        size_t rows = 50, cols = 100;
        double cellSize = 2.0;                                 // meters per pixel
        concord::Datum datum{48.0, 11.0, 500.0};               // lat, lon, alt
        concord::Euler heading{0, 0, 0};                       // no rotation
        concord::Pose shift{concord::Point{0, 0, 0}, heading}; // center grid at datum
        concord::Grid<uint8_t> grid(rows, cols, cellSize, datum,
                                    /*centered=*/true, shift);
        for (size_t r = 0; r < rows; ++r)
            for (size_t c = 0; c < cols; ++c)
                grid(r, c).second = ((r / 5 + c / 5) % 2) ? 255 : 0;

        // --- 2) Build a RasterCollection around it ---
        geotiv::RasterCollection rc;
        rc.crs = geotiv::CRS::WGS;
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        geotiv::Layer L;
        L.grid = std::move(grid);
        L.width = static_cast<uint32_t>(cols);
        L.height = static_cast<uint32_t>(rows);
        L.samplesPerPixel = 1; // single‐band
        L.planarConfig = 1;    // chunky
        // Set per-layer metadata
        L.crs = geotiv::CRS::WGS;
        L.datum = datum;
        L.heading = heading;
        L.resolution = cellSize;

        rc.layers.clear();
        rc.layers.push_back(std::move(L));

        // --- 3) Write out the GeoTIFF in one call ---
        geotiv::WriteRasterCollection(rc, "output.tif");
        std::cout << "Wrote GeoTIFF " << cols << "×" << rows << " at " << cellSize << "m/px → output.tif\n";
    } catch (std::exception &e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
