#include "concord/concord.hpp"
#include "geotiv/geotiv.hpp"
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

TEST_CASE("GeoTIFF Writer functionality") {
    SUBCASE("Create simple raster collection and convert to bytes") {
        // Create a simple 3x2 grid with checkerboard pattern
        size_t rows = 2, cols = 3;
        double cellSize = 1.0;
        concord::Datum datum{0.0, 0.0, 0.0};
        concord::Euler heading{0, 0, 0};
        concord::Pose shift{concord::Point{0, 0, 0}, heading};

        concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

        // Fill with simple pattern
        grid(0, 0) = 255;
        grid(0, 1) = 0;
        grid(0, 2) = 255;
        grid(1, 0) = 0;
        grid(1, 1) = 255;
        grid(1, 2) = 0;

        // Build RasterCollection
        geotiv::RasterCollection rc;
        // CRS is always WGS84
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        geotiv::Layer layer;
        layer.grid = std::move(grid);
        layer.width = static_cast<uint32_t>(cols);
        layer.height = static_cast<uint32_t>(rows);
        layer.samplesPerPixel = 1;
        layer.planarConfig = 1;
        // Set per-layer metadata
        // CRS is always WGS84
        layer.datum = datum;
        layer.heading = heading;
        layer.resolution = cellSize;

        rc.layers.push_back(std::move(layer));

        // Convert to TIFF bytes
        REQUIRE_NOTHROW(geotiv::toTiffBytes(rc));

        auto bytes = geotiv::toTiffBytes(rc);
        CHECK(bytes.size() > 0);
        CHECK(bytes.size() > 8); // At least the TIFF header
    }

    SUBCASE("Empty RasterCollection should throw") {
        geotiv::RasterCollection rc;
        CHECK_THROWS_WITH(geotiv::toTiffBytes(rc), "toTiffBytes(): no layers");
    }

    SUBCASE("Write and verify file creation") {
        // Create a simple raster collection
        size_t rows = 5, cols = 5;
        double cellSize = 1.0;
        concord::Datum datum{0.0, 0.0, 0.0};
        concord::Euler heading{0, 0, 0};
        concord::Pose shift{concord::Point{0, 0, 0}, heading};

        concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

        // Fill with gradient
        for (size_t r = 0; r < rows; ++r) {
            for (size_t c = 0; c < cols; ++c) {
                grid(r, c) = static_cast<uint8_t>((r * cols + c) * 10);
            }
        }

        geotiv::RasterCollection rc;
        // CRS is always WGS84
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        geotiv::Layer layer;
        layer.grid = std::move(grid);
        layer.width = static_cast<uint32_t>(cols);
        layer.height = static_cast<uint32_t>(rows);
        layer.samplesPerPixel = 1;
        layer.planarConfig = 1;
        // Set per-layer metadata
        // CRS is always WGS84
        layer.datum = datum;
        layer.heading = heading;
        layer.resolution = cellSize;

        rc.layers.push_back(std::move(layer));

        // Write to file
        std::string testFile = "test_output.tif";
        REQUIRE_NOTHROW(geotiv::WriteRasterCollection(rc, testFile));

        // Verify file exists and has content
        CHECK(std::filesystem::exists(testFile));
        CHECK(std::filesystem::file_size(testFile) > 0);

        // Clean up
        std::filesystem::remove(testFile);
    }

    SUBCASE("Multi-layer GeoTIFF") {
        size_t rows = 3, cols = 3;
        double cellSize = 2.0;
        concord::Datum datum{45.0, 9.0, 100.0};
        concord::Euler heading{0, 0, 0};
        concord::Pose shift{concord::Point{0, 0, 0}, heading};

        geotiv::RasterCollection rc;
        // CRS is always WGS84
        rc.datum = datum;
        rc.heading = heading;
        rc.resolution = cellSize;

        // Create two layers with different patterns
        for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
            concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    grid(r, c) = static_cast<uint8_t>((layerIdx + 1) * 50 + r * 10 + c);
                }
            }

            geotiv::Layer layer;
            layer.grid = std::move(grid);
            layer.width = static_cast<uint32_t>(cols);
            layer.height = static_cast<uint32_t>(rows);
            layer.samplesPerPixel = 1;
            layer.planarConfig = 1;
            // Set per-layer metadata - different for each layer
            // CRS is always WGS84
            layer.datum = {datum.lat + layerIdx * 0.01, datum.lon + layerIdx * 0.01, datum.alt + layerIdx * 10};
            layer.heading = heading;
            layer.resolution = cellSize + layerIdx * 0.1;

            rc.layers.push_back(std::move(layer));
        }

        // Should be able to convert multi-layer to bytes
        REQUIRE_NOTHROW(geotiv::toTiffBytes(rc));
        auto bytes = geotiv::toTiffBytes(rc);
        CHECK(bytes.size() > 0);

        // Write to file
        std::string testFile = "test_multilayer.tif";
        REQUIRE_NOTHROW(geotiv::WriteRasterCollection(rc, testFile));

        CHECK(std::filesystem::exists(testFile));
        CHECK(std::filesystem::file_size(testFile) > 0);

        // Clean up
        std::filesystem::remove(testFile);
    }
}
