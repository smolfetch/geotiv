#include "concord/concord.hpp"
#include "geotiv/geotiv.hpp"
#include <doctest/doctest.h>
#include <filesystem>

TEST_CASE("Advanced Multi-IFD with Per-Layer Tags and Metadata") {
    SUBCASE("Multi-layer with different coordinate systems and custom tags") {
        geotiv::RasterCollection rc;

        // Create 3 layers with different properties
        for (int i = 0; i < 3; ++i) {
            size_t rows = 10 + i * 5, cols = 15 + i * 5;
            double cellSize = 1.0 + i * 0.5;
            concord::Datum datum{47.0 + i * 0.1, 8.0 + i * 0.1, 100.0 + i * 50};
            concord::Euler heading{0, 0, i * 15.0}; // Different rotation per layer
            concord::Pose shift{concord::Point{0, 0, 0}, heading};

            concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

            // Fill with layer-specific pattern
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    grid(r, c) = static_cast<uint8_t>((i * 50 + r + c) % 256);
                }
            }

            geotiv::Layer layer;
            layer.grid = std::move(grid);
            layer.width = static_cast<uint32_t>(cols);
            layer.height = static_cast<uint32_t>(rows);
            layer.samplesPerPixel = 1;
            layer.planarConfig = 1;

            // Set different coordinate systems per layer
            layer.crs = (i == 0) ? geotiv::CRS::WGS : geotiv::CRS::ENU;
            layer.datum = datum;
            layer.heading = heading;
            layer.resolution = cellSize;
            // Don't set custom imageDescription - let the writer generate the proper geospatial one

            // Add custom tags specific to each layer
            layer.customTags[50000 + i] = {static_cast<uint32_t>(i * 1000)};          // Layer ID
            layer.customTags[50100] = {static_cast<uint32_t>(1735689600 + i * 3600)}; // Timestamp
            layer.customTags[50200 + i] = {42u, static_cast<uint32_t>(100 + i),
                                           static_cast<uint32_t>(200 + i)}; // Multi-value custom tag

            rc.layers.push_back(std::move(layer));
        }

        // Set collection defaults from first layer
        rc.crs = rc.layers[0].crs;
        rc.datum = rc.layers[0].datum;
        rc.heading = rc.layers[0].heading;
        rc.resolution = rc.layers[0].resolution;

        // Write multi-IFD GeoTIFF
        std::string testFile = "multi_ifd_advanced.tif";
        REQUIRE_NOTHROW(geotiv::WriteRasterCollection(rc, testFile));
        CHECK(std::filesystem::exists(testFile));

        // Read back and verify per-IFD metadata preservation
        geotiv::RasterCollection readRc;
        REQUIRE_NOTHROW(readRc = geotiv::ReadRasterCollection(testFile));

        // Verify we have all layers
        CHECK(readRc.layers.size() == 3);

        // Verify each layer has its own metadata
        for (size_t i = 0; i < 3; ++i) {
            const auto &layer = readRc.layers[i];

            // Check dimensions
            CHECK(layer.width == (15 + i * 5));
            CHECK(layer.height == (10 + i * 5));

            // Check coordinate system
            geotiv::CRS expectedCRS = (i == 0) ? geotiv::CRS::WGS : geotiv::CRS::ENU;
            CHECK(layer.crs == expectedCRS);

            // Check datum (with some tolerance for floating point precision)
            CHECK(layer.datum.lat == doctest::Approx(47.0 + i * 0.1).epsilon(0.001));
            CHECK(layer.datum.lon == doctest::Approx(8.0 + i * 0.1).epsilon(0.001));
            CHECK(layer.datum.alt == doctest::Approx(100.0 + i * 50).epsilon(0.1));

            // Check resolution
            CHECK(layer.resolution == doctest::Approx(1.0 + i * 0.5).epsilon(0.001));

            // Check heading
            CHECK(layer.heading.yaw == doctest::Approx(i * 15.0).epsilon(0.1));

            // Check custom tags were preserved
            auto it = layer.customTags.find(50000 + i);
            CHECK(it != layer.customTags.end());
            if (it != layer.customTags.end()) {
                CHECK(it->second.size() >= 1);
                CHECK(it->second[0] == static_cast<uint32_t>(i * 1000));
            }

            // Verify grid data integrity
            const auto &grid = layer.grid;
            CHECK(grid.rows() == (10 + i * 5));
            CHECK(grid.cols() == (15 + i * 5));

            // Check some pixel values
            uint8_t expectedFirst = static_cast<uint8_t>((i * 50) % 256);
            CHECK(grid(0, 0) == expectedFirst);
        }

        // Clean up
        std::filesystem::remove(testFile);
    }

    SUBCASE("Time-series data with timestamps in custom tags") {
        geotiv::RasterCollection timeSeries;

        // Simulate a time series with 4 time points
        std::vector<uint32_t> timestamps = {1735689600, 1735693200, 1735696800, 1735700400}; // 1-hour intervals

        for (size_t t = 0; t < timestamps.size(); ++t) {
            size_t rows = 20, cols = 30;
            double cellSize = 0.5;                                 // 50cm resolution
            concord::Datum surveyLocation{46.5204, 6.6234, 372.0}; // Geneva coordinates
            concord::Pose shift{concord::Point{0, 0, 0}, concord::Euler{0, 0, 0}};

            concord::Grid<uint8_t> grid(rows, cols, cellSize, true, shift);

            // Fill with time-dependent pattern
            for (size_t r = 0; r < rows; ++r) {
                for (size_t c = 0; c < cols; ++c) {
                    grid(r, c) = static_cast<uint8_t>((t * 40 + r + c) % 256);
                }
            }

            geotiv::Layer layer;
            layer.grid = std::move(grid);
            layer.width = static_cast<uint32_t>(cols);
            layer.height = static_cast<uint32_t>(rows);
            layer.samplesPerPixel = 1;
            layer.planarConfig = 1;
            layer.crs = geotiv::CRS::WGS;
            layer.datum = surveyLocation;
            layer.heading = {0, 0, 0};
            layer.resolution = cellSize;
            layer.imageDescription = "Time-series data point " + std::to_string(t);

            // Store timestamp and sequence number in custom tags
            layer.customTags[50100] = {timestamps[t]};                                            // Timestamp
            layer.customTags[50101] = {static_cast<uint32_t>(t)};                                 // Sequence number
            layer.customTags[50102] = {static_cast<uint32_t>(rows), static_cast<uint32_t>(cols)}; // Dimensions

            timeSeries.layers.push_back(std::move(layer));
        }

        // Set collection metadata from first layer
        timeSeries.crs = timeSeries.layers[0].crs;
        timeSeries.datum = timeSeries.layers[0].datum;
        timeSeries.heading = timeSeries.layers[0].heading;
        timeSeries.resolution = timeSeries.layers[0].resolution;

        // Write time-series GeoTIFF
        std::string timeSeriesFile = "time_series.tif";
        REQUIRE_NOTHROW(geotiv::WriteRasterCollection(timeSeries, timeSeriesFile));

        // Read back and verify temporal metadata
        geotiv::RasterCollection readTimeSeries;
        REQUIRE_NOTHROW(readTimeSeries = geotiv::ReadRasterCollection(timeSeriesFile));

        CHECK(readTimeSeries.layers.size() == 4);

        // Verify timestamps are preserved in order
        for (size_t t = 0; t < timestamps.size(); ++t) {
            const auto &layer = readTimeSeries.layers[t];

            // Check timestamp tag
            auto timestampIt = layer.customTags.find(50100);
            CHECK(timestampIt != layer.customTags.end());
            if (timestampIt != layer.customTags.end()) {
                CHECK(timestampIt->second[0] == timestamps[t]);
            }

            // Check sequence number
            auto seqIt = layer.customTags.find(50101);
            CHECK(seqIt != layer.customTags.end());
            if (seqIt != layer.customTags.end()) {
                CHECK(seqIt->second[0] == static_cast<uint32_t>(t));
            }

            // Check dimensions tag
            auto dimIt = layer.customTags.find(50102);
            CHECK(dimIt != layer.customTags.end());
            if (dimIt != layer.customTags.end() && dimIt->second.size() >= 2) {
                CHECK(dimIt->second[0] == 20); // rows
                CHECK(dimIt->second[1] == 30); // cols
            }
        }

        // Clean up
        std::filesystem::remove(timeSeriesFile);
    }
}
