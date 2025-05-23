#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "concord/types_basic.hpp"
#include "concord/types_grid.hpp"

namespace geotiv {
    using std::uint16_t;
    struct Layer {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t samplesPerPixel = 0;
        uint32_t planarConfig = 0;
        std::vector<uint32_t> stripOffsets;
        std::vector<uint32_t> stripByteCounts;
    };

    struct RasterCollection {
        std::vector<concord::Grid<uint8_t>> layers;
        concord::CRS crs = concord::CRS::ENU;
        concord::Datum datum;   // lat=lon=alt=0
        concord::Euler heading; // roll=pitch=0, yaw=0
    };
} // namespace geotiv
