// geotiv/writer.hpp
#pragma once

#include <cstdint>
#include <cstring> // memcpy
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/types_basic.hpp" // concord::CRS, Datum, Euler
#include "concord/types_grid.hpp"  // concord::Grid<T>
#include "geotiv/types.hpp"        // RasterCollection

namespace geotiv {
    namespace fs = std::filesystem;

    // Write out the first layer of rc as a GeoTIFF, flattening its Grid<uint8_t> internally.
    inline std::vector<uint8_t> toTiffBytes(RasterCollection const &rc) {
        if (rc.layers.empty())
            throw std::runtime_error("toTiffBytes(): no layers in RasterCollection");

        // 1) Flatten the grid into a chunky pixel buffer
        auto const &layer = rc.layers[0];
        auto const &grid = layer.grid;
        uint32_t width = static_cast<uint32_t>(grid.cols());
        uint32_t height = static_cast<uint32_t>(grid.rows());
        uint32_t spp = layer.samplesPerPixel;
        uint32_t pcfg = layer.planarConfig;

        // assume single‐band chunky
        size_t pdSz = size_t(width) * height * spp;
        std::vector<uint8_t> pixelData(pdSz);
        size_t idx = 0;
        for (uint32_t r = 0; r < height; ++r) {
            for (uint32_t c = 0; c < width; ++c) {
                pixelData[idx++] = grid(r, c).second;
            }
        }

        // 2) Compute offsets & sizes
        uint32_t pdSz32 = static_cast<uint32_t>(pdSz);
        uint32_t ifdOffset = 8 + pdSz32; // header + pixelData
        std::string desc = "CRS " + std::string(rc.crs == concord::CRS::WGS ? "WGS" : "ENU") + " DATUM " +
                           std::to_string(rc.datum.lat) + " " + std::to_string(rc.datum.lon) + " " +
                           std::to_string(rc.datum.alt) + " HEADING " + std::to_string(rc.heading.yaw);
        uint32_t descLen = static_cast<uint32_t>(desc.size() + 1);
        uint16_t entryCount = 8; // we write 8 tags

        uint32_t descOffset = ifdOffset + 2 + entryCount * 12 + 4;
        uint32_t scaleOffset = descOffset + descLen;
        uint32_t totalSize = scaleOffset + 16; // 2 doubles = 16 bytes

        // 3) Allocate final buffer
        std::vector<uint8_t> buf(totalSize);
        size_t p = 0;

        // Helper lambdas for LE writes:
        auto writeLE16 = [&](uint16_t v) {
            buf[p++] = uint8_t(v & 0xFF);
            buf[p++] = uint8_t(v >> 8);
        };
        auto writeLE32 = [&](uint32_t v) {
            buf[p++] = uint8_t(v & 0xFF);
            buf[p++] = uint8_t((v >> 8) & 0xFF);
            buf[p++] = uint8_t((v >> 16) & 0xFF);
            buf[p++] = uint8_t((v >> 24) & 0xFF);
        };
        auto writeDouble = [&](double d) {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(d));
            for (int i = 0; i < 8; ++i)
                buf[p++] = uint8_t((bits >> (8 * i)) & 0xFF);
        };

        // --- 1) TIFF header ---
        buf[p++] = 'I';
        buf[p++] = 'I';       // little‐endian
        writeLE16(42);        // magic
        writeLE32(ifdOffset); // IFD offset

        // --- 2) Pixel data ---
        std::memcpy(&buf[p], pixelData.data(), pdSz);
        p += pdSz;

        // --- 3) IFD entries ---
        writeLE16(entryCount);

        // Tag 256: ImageWidth
        writeLE16(256);
        writeLE16(4);
        writeLE32(1);
        writeLE32(width);
        // Tag 257: ImageLength
        writeLE16(257);
        writeLE16(4);
        writeLE32(1);
        writeLE32(height);
        // Tag 277: SamplesPerPixel
        writeLE16(277);
        writeLE16(3);
        writeLE32(1);
        writeLE32(spp);
        // Tag 284: PlanarConfiguration
        writeLE16(284);
        writeLE16(3);
        writeLE32(1);
        writeLE32(pcfg);
        // Tag 273: StripOffsets
        writeLE16(273);
        writeLE16(4);
        writeLE32(1);
        writeLE32(8);
        // Tag 279: StripByteCounts
        writeLE16(279);
        writeLE16(4);
        writeLE32(1);
        writeLE32(pdSz32);
        // Tag 270: ImageDescription
        writeLE16(270);
        writeLE16(2);
        writeLE32(descLen);
        writeLE32(descOffset);
        // Tag 33550: ModelPixelScaleTag
        writeLE16(33550);
        writeLE16(12);
        writeLE32(2);
        writeLE32(scaleOffset);
        // Next IFD pointer = 0
        writeLE32(0);

        // --- 4) Description text + NUL ---
        std::memcpy(&buf[p], desc.data(), descLen);
        p += descLen;

        // --- 5) Pixel‐scale doubles X, Y ---
        writeDouble(rc.resolution);
        writeDouble(rc.resolution);

        return buf;
    }

    /// Write out the bytes to disk
    inline void WriteRasterCollection(RasterCollection const &rc, fs::path const &outPath) {
        auto bytes = toTiffBytes(rc);
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("WriteRasterCollection(): cannot open " + outPath.string());
        ofs.write(reinterpret_cast<char *>(bytes.data()), bytes.size());
    }

} // namespace geotiv
