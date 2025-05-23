// geotiv/writer.hpp
#pragma once

#include <cstdint>
#include <cstring> // for std::memcpy
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/types_basic.hpp" // for concord::CRS, Datum, Euler
#include "geotiv/types.hpp"        // for RasterCollection

namespace geotiv {
    namespace fs = std::filesystem;

    /// Assemble a little-endian, single-IFD, single-strip GeoTIFF
    /// directly from rc.layers[0].grid (no extra buffer argument needed).
    inline std::vector<uint8_t> toTiffBytes(RasterCollection const &rc) {
        if (rc.layers.empty())
            throw std::runtime_error("toTiffBytes(): no layers in RasterCollection");

        // --- Flatten the grid into pixelData ---
        auto const &grid = rc.layers[0].grid;
        uint32_t width = rc.layers[0].width;
        uint32_t height = rc.layers[0].height;
        uint32_t spp = rc.layers[0].samplesPerPixel;
        uint32_t pcfg = rc.layers[0].planarConfig;

        std::vector<uint8_t> pixelData;
        pixelData.reserve(size_t(width) * height * spp);
        for (uint32_t r = 0; r < height; ++r) {
            for (uint32_t c = 0; c < width; ++c) {
                // if spp>1, you'd pack multiple channels here
                pixelData.push_back(grid(r, c).second);
            }
        }

        // Now proceed exactly as before, using pixelData...
        uint32_t pdSz = static_cast<uint32_t>(pixelData.size());
        uint32_t ifdOffset = 8 + pdSz;

        // Build ImageDescription
        std::string desc = "CRS ";
        desc += (rc.crs == concord::CRS::WGS ? "WGS" : "ENU");
        desc += " DATUM " + std::to_string(rc.datum.lat) + " " + std::to_string(rc.datum.lon) + " " +
                std::to_string(rc.datum.alt) + " HEADING " + std::to_string(rc.heading.yaw);
        uint32_t descLen = static_cast<uint32_t>(desc.size() + 1);

        // 8 IFD tags as before (including ModelPixelScaleTag)
        uint16_t entryCount = 8;
        uint32_t descOffset = ifdOffset + 2 + entryCount * 12 + 4;
        uint32_t scaleOffset = descOffset + descLen;

        std::vector<uint8_t> buf;
        buf.reserve(scaleOffset + 16);

        auto appendLE16 = [&](uint16_t v) {
            buf.push_back(uint8_t(v & 0xFF));
            buf.push_back(uint8_t(v >> 8));
        };
        auto appendLE32 = [&](uint32_t v) {
            buf.push_back(uint8_t(v & 0xFF));
            buf.push_back(uint8_t((v >> 8) & 0xFF));
            buf.push_back(uint8_t((v >> 16) & 0xFF));
            buf.push_back(uint8_t((v >> 24) & 0xFF));
        };
        auto appendDouble = [&](double d) {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(d));
            for (int i = 0; i < 8; ++i)
                buf.push_back(uint8_t((bits >> (8 * i)) & 0xFF));
        };

        // 1) TIFF header
        buf.push_back('I');
        buf.push_back('I');
        appendLE16(42);
        appendLE32(ifdOffset);

        // 2) Pixel data
        buf.insert(buf.end(), pixelData.begin(), pixelData.end());

        // 3) IFD entries
        appendLE16(entryCount);
        // -- ImageWidth
        appendLE16(256);
        appendLE16(4);
        appendLE32(1);
        appendLE32(width);
        // -- ImageLength
        appendLE16(257);
        appendLE16(4);
        appendLE32(1);
        appendLE32(height);
        // -- SamplesPerPixel
        appendLE16(277);
        appendLE16(3);
        appendLE32(1);
        appendLE32(spp);
        // -- PlanarConfiguration
        appendLE16(284);
        appendLE16(3);
        appendLE32(1);
        appendLE32(pcfg);
        // -- StripOffsets
        appendLE16(273);
        appendLE16(4);
        appendLE32(1);
        appendLE32(8);
        // -- StripByteCounts
        appendLE16(279);
        appendLE16(4);
        appendLE32(1);
        appendLE32(pdSz);
        // -- ImageDescription
        appendLE16(270);
        appendLE16(2);
        appendLE32(descLen);
        appendLE32(descOffset);
        // -- ModelPixelScaleTag
        appendLE16(33550);
        appendLE16(12);
        appendLE32(2);
        appendLE32(scaleOffset);
        // -- next IFD = 0
        appendLE32(0);

        // 4) Description text
        buf.insert(buf.end(), desc.begin(), desc.end());
        buf.push_back('\0');

        // 5) PixelScale doubles
        appendDouble(rc.resolution);
        appendDouble(rc.resolution);

        return buf;
    }

    /// Write to disk
    inline void WriteRasterCollection(RasterCollection const &rc, fs::path const &outPath) {
        auto bytes = toTiffBytes(rc);
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("cannot open " + outPath.string());
        ofs.write(reinterpret_cast<char *>(bytes.data()), bytes.size());
    }

} // namespace geotiv
