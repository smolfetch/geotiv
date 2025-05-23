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
    /// from the first layer of rc and chunky pixelData.
    /// Includes ModelPixelScaleTag for rc.resolution.
    inline std::vector<uint8_t> toTiffBytes(RasterCollection const &rc, std::vector<uint8_t> const &pixelData) {
        if (rc.layers.empty())
            throw std::runtime_error("toTiffBytes(): no layers in RasterCollection");

        auto const &L = rc.layers[0];
        uint32_t width = L.width;
        uint32_t height = L.height;
        uint32_t spp = L.samplesPerPixel;
        uint32_t pcfg = L.planarConfig;
        uint32_t pdSz = static_cast<uint32_t>(pixelData.size());

        // Header (8 bytes) + pixel data
        uint32_t ifdOffset = 8 + pdSz;

        // Build ImageDescription
        std::string desc = "CRS ";
        desc += (rc.crs == concord::CRS::WGS ? "WGS" : "ENU");
        desc += " DATUM " + std::to_string(rc.datum.lat) + " " + std::to_string(rc.datum.lon) + " " +
                std::to_string(rc.datum.alt) + " HEADING " + std::to_string(rc.heading.yaw);
        uint32_t descLen = static_cast<uint32_t>(desc.size() + 1);

        // We'll write 8 tags now:
        // 1: ImageWidth, 2: ImageLength, 3: SamplesPerPixel,
        // 4: PlanarConfiguration, 5: StripOffsets, 6: StripByteCounts,
        // 7: ImageDescription, 8: ModelPixelScaleTag
        uint16_t entryCount = 8;

        // Description + pixel-scale data go after IFD:
        uint32_t descOffset = ifdOffset + 2     // entryCount
                              + entryCount * 12 // entries
                              + 4;              // next-IFD ptr
        uint32_t scaleOffset = descOffset + descLen;

        std::vector<uint8_t> buf;
        buf.reserve(scaleOffset + /*2 doubles*/ 16);

        // Little-endian appenders
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

        // --- 1) TIFF header ---
        buf.push_back('I');
        buf.push_back('I'); // little-endian
        appendLE16(42);     // magic
        appendLE32(ifdOffset);

        // --- 2) Pixel data strip ---
        buf.insert(buf.end(), pixelData.begin(), pixelData.end());

        // --- 3) IFD ---
        appendLE16(entryCount);

        // Tag 256: ImageWidth
        appendLE16(256);
        appendLE16(4);
        appendLE32(1);
        appendLE32(width);
        // Tag 257: ImageLength
        appendLE16(257);
        appendLE16(4);
        appendLE32(1);
        appendLE32(height);
        // Tag 277: SamplesPerPixel
        appendLE16(277);
        appendLE16(3);
        appendLE32(1);
        appendLE32(spp);
        // Tag 284: PlanarConfiguration
        appendLE16(284);
        appendLE16(3);
        appendLE32(1);
        appendLE32(pcfg);
        // Tag 273: StripOffsets
        appendLE16(273);
        appendLE16(4);
        appendLE32(1);
        appendLE32(8);
        // Tag 279: StripByteCounts
        appendLE16(279);
        appendLE16(4);
        appendLE32(1);
        appendLE32(pdSz);
        // Tag 270: ImageDescription
        appendLE16(270);
        appendLE16(2);
        appendLE32(descLen);
        appendLE32(descOffset);
        // Tag 33550: ModelPixelScaleTag (DOUBLE)
        appendLE16(33550);
        appendLE16(12);          // type=DOUBLE
        appendLE32(2);           // count = 2 (scaleX, scaleY)
        appendLE32(scaleOffset); // offset to doubles

        // next IFD pointer = 0
        appendLE32(0);

        // --- 4) Description text ---
        buf.insert(buf.end(), desc.begin(), desc.end());
        buf.push_back('\0');

        // --- 5) PixelScale doubles ---
        // two values: scaleX and scaleY; ignore Z
        appendDouble(rc.resolution);
        appendDouble(rc.resolution);

        return buf;
    }

    /// Write out the bytes
    inline void WriteRasterCollection(RasterCollection const &rc, std::vector<uint8_t> const &pixelData,
                                      fs::path const &outPath) {
        auto bytes = toTiffBytes(rc, pixelData);
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("WriteRasterCollection(): cannot open " + outPath.string());
        ofs.write(reinterpret_cast<char *>(bytes.data()), bytes.size());
    }

} // namespace geotiv
