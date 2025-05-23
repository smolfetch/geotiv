#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "concord/types_basic.hpp"
#include "concord/types_grid.hpp"

namespace geotiv {

    using std::uint16_t;

    struct Layer {
        // **New**: where in the file this IFD lived
        uint32_t ifdOffset = 0;

        // dims & layout
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t samplesPerPixel = 0;
        uint32_t planarConfig = 0;

        // strip info
        std::vector<uint32_t> stripOffsets;
        std::vector<uint32_t> stripByteCounts;

        // the actual samples, geo-gridded
        concord::Grid<uint8_t> grid;
    };

    struct RasterCollection {
        std::vector<Layer> layers; // one entry per IFD

        concord::CRS crs = concord::CRS::ENU;
        concord::Datum datum;   // lat=lon=alt=0
        concord::Euler heading; // roll=pitch=0, yaw=0
        double resolution;      // the representation of one pixel in meters
    };

} // namespace geotiv
