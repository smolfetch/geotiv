#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "concord/concord.hpp"

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

        // Per-IFD geospatial metadata (allows different coordinate systems per layer)
        concord::CRS crs = concord::CRS::WGS;
        concord::Datum datum;    // lat=lon=alt=0
        concord::Euler heading;  // roll=pitch=0, yaw=0
        double resolution = 1.0; // the representation of one pixel in meters

        // Additional GeoTIFF tags per IFD
        std::string imageDescription;
        std::map<uint16_t, std::vector<uint32_t>> customTags; // For additional TIFF tags

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
