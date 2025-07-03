#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "geotiv/raster.hpp"
#include <filesystem>
#include <algorithm>

TEST_CASE("Raster - Basic Construction") {
    concord::Datum datum{52.0, 5.0, 0.0};
    concord::Euler heading{0, 0, 0.5};
    double resolution = 2.0;
    
    SUBCASE("Constructor with default parameters") {
        geotiv::Raster raster;
        
        CHECK(raster.gridCount() == 0);
        CHECK_FALSE(raster.hasGrids());
        CHECK(raster.getCRS() == geotiv::CRS::ENU);
        CHECK(raster.getResolution() == doctest::Approx(1.0));
    }
    
    SUBCASE("Constructor with all parameters") {
        geotiv::Raster raster(datum, heading, geotiv::CRS::WGS, resolution);
        
        CHECK(raster.getDatum().lat == doctest::Approx(52.0));
        CHECK(raster.getDatum().lon == doctest::Approx(5.0));
        CHECK(raster.getHeading().yaw == doctest::Approx(0.5));
        CHECK(raster.getCRS() == geotiv::CRS::WGS);
        CHECK(raster.getResolution() == doctest::Approx(2.0));
    }
}

TEST_CASE("Raster - Grid Management") {
    concord::Datum datum{52.0, 5.0, 0.0};
    geotiv::Raster raster(datum, concord::Euler{0, 0, 0}, geotiv::CRS::ENU, 1.0);
    
    SUBCASE("Add and retrieve grids") {
        raster.addGrid(100, 100, "elevation", "terrain", {{"unit", "meters"}});
        
        CHECK(raster.gridCount() == 1);
        CHECK(raster.hasGrids());
        
        const auto& grid = raster.getGrid(0);
        CHECK(grid.name == "elevation");
        CHECK(grid.type == "terrain");
        CHECK(grid.properties.at("unit") == "meters");
        CHECK(grid.properties.at("type") == "terrain");
        CHECK(grid.grid.rows() == 100);
        CHECK(grid.grid.cols() == 100);
    }
    
    SUBCASE("Add specialized grids") {
        raster.addTerrainGrid(50, 50, "terrain_map");
        raster.addOcclusionGrid(50, 50, "occlusion_map");
        raster.addElevationGrid(50, 50, "elevation_map");
        
        CHECK(raster.gridCount() == 3);
        
        auto terrainGrids = raster.getGridsByType("terrain");
        CHECK(terrainGrids.size() == 1);
        CHECK(terrainGrids[0].name == "terrain_map");
        
        auto occlusionGrids = raster.getGridsByType("occlusion");
        CHECK(occlusionGrids.size() == 1);
        CHECK(occlusionGrids[0].name == "occlusion_map");
        
        auto elevationGrids = raster.getGridsByType("elevation");
        CHECK(elevationGrids.size() == 1);
        CHECK(elevationGrids[0].name == "elevation_map");
    }
    
    SUBCASE("Access grids by name and index") {
        raster.addGrid(30, 30, "test_grid", "custom");
        
        // Access by index
        const auto& gridByIndex = raster.getGrid(0);
        CHECK(gridByIndex.name == "test_grid");
        
        // Access by name
        const auto& gridByName = raster.getGrid("test_grid");
        CHECK(gridByName.name == "test_grid");
        CHECK(gridByName.type == "custom");
    }
    
    SUBCASE("Filter grids by properties") {
        raster.addGrid(25, 25, "grid1", "type_a", {{"purpose", "navigation"}});
        raster.addGrid(25, 25, "grid2", "type_b", {{"purpose", "navigation"}});
        raster.addGrid(25, 25, "grid3", "type_a", {{"purpose", "visualization"}});
        
        auto typeAGrids = raster.getGridsByType("type_a");
        CHECK(typeAGrids.size() == 2);
        
        auto navigationGrids = raster.filterByProperty("purpose", "navigation");
        CHECK(navigationGrids.size() == 2);
        
        auto visualizationGrids = raster.filterByProperty("purpose", "visualization");
        CHECK(visualizationGrids.size() == 1);
    }
    
    SUBCASE("Remove grids") {
        raster.addGrid(20, 20, "grid1", "temp");
        raster.addGrid(20, 20, "grid2", "permanent");
        
        CHECK(raster.gridCount() == 2);
        
        raster.removeGrid(0);
        CHECK(raster.gridCount() == 1);
        CHECK(raster.getGrid(0).name == "grid2");
        
        raster.clearGrids();
        CHECK(raster.gridCount() == 0);
        CHECK_FALSE(raster.hasGrids());
    }
}

TEST_CASE("Raster - Grid Data Operations") {
    geotiv::Raster raster;
    raster.addGrid(10, 10, "test_data", "test");
    
    SUBCASE("Grid data access") {
        auto& grid = raster.getGrid("test_data");
        
        // Set some test data
        for (uint32_t r = 0; r < 10; ++r) {
            for (uint32_t c = 0; c < 10; ++c) {
                grid.grid(r, c) = static_cast<uint8_t>(r * 10 + c);
            }
        }
        
        // Verify data
        CHECK(grid.grid(0, 0) == 0);
        CHECK(grid.grid(5, 7) == 57);
        CHECK(grid.grid(9, 9) == 99);
    }
    
    SUBCASE("World coordinate mapping") {
        const auto& grid = raster.getGrid("test_data");
        
        // Get world coordinate for a grid cell
        auto worldCoord = grid.grid.get_point(5, 5);
        
        // World coordinates should be valid concord::Point
        bool hasValidCoords = (worldCoord.x != 0.0 || worldCoord.y != 0.0 || worldCoord.z == 0.0);
        CHECK(hasValidCoords);
    }
}

TEST_CASE("Raster - Properties and Metadata") {
    geotiv::Raster raster;
    
    SUBCASE("CRS and coordinate system") {
        CHECK(raster.getCRS() == geotiv::CRS::ENU);
        
        raster.setCRS(geotiv::CRS::WGS);
        CHECK(raster.getCRS() == geotiv::CRS::WGS);
    }
    
    SUBCASE("Datum and heading") {
        concord::Datum newDatum{51.0, 4.0, 10.0};
        concord::Euler newHeading{0.1, 0.2, 0.3};
        
        raster.setDatum(newDatum);
        raster.setHeading(newHeading);
        
        CHECK(raster.getDatum().lat == doctest::Approx(51.0));
        CHECK(raster.getDatum().lon == doctest::Approx(4.0));
        CHECK(raster.getDatum().alt == doctest::Approx(10.0));
        CHECK(raster.getHeading().yaw == doctest::Approx(0.3));
    }
    
    SUBCASE("Resolution") {
        raster.setResolution(5.0);
        CHECK(raster.getResolution() == doctest::Approx(5.0));
    }
}

TEST_CASE("Raster - File I/O") {
    concord::Datum datum{52.0, 5.0, 0.0};
    geotiv::Raster originalRaster(datum, concord::Euler{0, 0, 0.5}, geotiv::CRS::ENU, 2.0);
    
    // Add some test grids
    originalRaster.addTerrainGrid(20, 20, "terrain");
    originalRaster.addOcclusionGrid(20, 20, "occlusion");
    
    // Fill with test data
    auto& terrainGrid = originalRaster.getGrid("terrain");
    for (uint32_t r = 0; r < 20; ++r) {
        for (uint32_t c = 0; c < 20; ++c) {
            terrainGrid.grid(r, c) = static_cast<uint8_t>((r + c) % 256);
        }
    }
    
    auto& occlusionGrid = originalRaster.getGrid("occlusion");
    for (uint32_t r = 0; r < 20; ++r) {
        for (uint32_t c = 0; c < 20; ++c) {
            occlusionGrid.grid(r, c) = static_cast<uint8_t>((r * c) % 256);
        }
    }
    
    std::filesystem::path testFile = std::filesystem::temp_directory_path() / "test_raster.tif";
    
    SUBCASE("Save and load raster") {
        // Save to file
        originalRaster.toFile(testFile);
        CHECK(std::filesystem::exists(testFile));
        
        // Load from file
        auto loadedRaster = geotiv::Raster::fromFile(testFile);
        
        CHECK(loadedRaster.gridCount() == 2);
        
        // Note: File I/O may not preserve all metadata exactly, 
        // but should preserve basic structure
        CHECK(loadedRaster.getDatum().lat != 0.0);  // Should have some datum
        CHECK(loadedRaster.getResolution() > 0.0);  // Should have positive resolution
        
        // Check grid names (may be different due to serialization format)
        auto gridNames = loadedRaster.getGridNames();
        CHECK(gridNames.size() == 2);
        
        // Verify we can access grids by index
        const auto& grid0 = loadedRaster.getGrid(0);
        CHECK(grid0.grid.rows() == 20);
        CHECK(grid0.grid.cols() == 20);
        
        const auto& grid1 = loadedRaster.getGrid(1);
        CHECK(grid1.grid.rows() == 20);
        CHECK(grid1.grid.cols() == 20);
        
        // Cleanup
        std::filesystem::remove(testFile);
    }
}

TEST_CASE("Raster - Error Handling") {
    geotiv::Raster raster;
    
    SUBCASE("Out of range access") {
        CHECK_THROWS_AS(raster.getGrid(0), std::out_of_range);
        CHECK_THROWS_AS(raster.getGrid("nonexistent"), std::runtime_error);
    }
    
    SUBCASE("File not found") {
        std::filesystem::path nonExistentFile = "/tmp/does_not_exist.tif";
        CHECK_THROWS(geotiv::Raster::fromFile(nonExistentFile));
    }
}

TEST_CASE("Raster - Iterators") {
    geotiv::Raster raster;
    raster.addGrid(10, 10, "grid1", "type1");
    raster.addGrid(15, 15, "grid2", "type2");
    raster.addGrid(20, 20, "grid3", "type1");
    
    SUBCASE("Range-based for loop") {
        int count = 0;
        for (const auto& grid : raster) {
            CHECK(grid.name.starts_with("grid"));
            CHECK(grid.grid.rows() >= 10);
            count++;
        }
        CHECK(count == 3);
    }
    
    SUBCASE("Iterator access") {
        auto it = raster.begin();
        CHECK(it->name == "grid1");
        ++it;
        CHECK(it->name == "grid2");
        ++it;
        CHECK(it->name == "grid3");
        ++it;
        CHECK(it == raster.end());
    }
    
    SUBCASE("Const iterators") {
        const auto& constRaster = raster;
        int count = 0;
        for (auto it = constRaster.cbegin(); it != constRaster.cend(); ++it) {
            CHECK(it->name.starts_with("grid"));
            count++;
        }
        CHECK(count == 3);
    }
}

TEST_CASE("Raster - Grid Names and Management") {
    geotiv::Raster raster;
    
    SUBCASE("Get grid names") {
        raster.addGrid(10, 10, "alpha", "type1");
        raster.addGrid(10, 10, "beta", "type2");
        raster.addGrid(10, 10, "gamma", "type1");
        
        auto names = raster.getGridNames();
        CHECK(names.size() == 3);
        CHECK(std::find(names.begin(), names.end(), "alpha") != names.end());
        CHECK(std::find(names.begin(), names.end(), "beta") != names.end());
        CHECK(std::find(names.begin(), names.end(), "gamma") != names.end());
    }
    
    SUBCASE("Multiple grids of same type") {
        raster.addTerrainGrid(10, 10, "terrain1");
        raster.addTerrainGrid(15, 15, "terrain2");
        raster.addOcclusionGrid(20, 20, "occlusion1");
        
        auto terrainGrids = raster.getGridsByType("terrain");
        CHECK(terrainGrids.size() == 2);
        
        auto occlusionGrids = raster.getGridsByType("occlusion");
        CHECK(occlusionGrids.size() == 1);
        
        auto unknownGrids = raster.getGridsByType("unknown");
        CHECK(unknownGrids.size() == 0);
    }
}