#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/types_basic.hpp" // for concord::CRS, Datum, Euler
#include "geotiv/types.hpp"        // for geotiv::RasterCollection

namespace geotiv {

    namespace fs = std::filesystem;

    /// Build an in‐memory GeoTIFF as little‐endian, single‐IFD, single‐strip,
    /// with one multi‐band layer and an ImageDescription containing CRS, DATUM, HEADING.
    /// pixelData must be the raw interleaved (chunky) sample bytes for layer 0.
    inline std::vector<uint8_t> toTiffBytes(geotiv::RasterCollection const &rc, std::vector<uint8_t> const &pixelData) {
        if (rc.layers.empty())
            throw std::runtime_error("toTiffBytes(): no layers in RasterCollection");

        auto const &L = rc.layers[0];
        uint32_t width = L.width;
        uint32_t height = L.height;
        uint32_t spp = L.samplesPerPixel;
        uint32_t pcfg = L.planarConfig;
        uint32_t pdSz = static_cast<uint32_t>(pixelData.size());

        // IFD will be placed immediately after header (8) + pixelData
        uint32_t ifdOffset = 8 + pdSz;

        // Build ImageDescription string
        std::string desc = std::string("CRS ") + (rc.crs == concord::CRS::WGS ? "WGS" : "ENU") + " DATUM " +
                           std::to_string(rc.datum.lat) + " " + std::to_string(rc.datum.lon) + " " +
                           std::to_string(rc.datum.alt) + " HEADING " + std::to_string(rc.heading.yaw);
        uint32_t descLen = static_cast<uint32_t>(desc.size() + 1); // include NUL

        // We will write 7 tags: Width, Height, SPP, PlanarConfig,
        // StripOffsets, StripByteCounts, ImageDescription
        uint16_t entryCount = 7;

        // Location where the ImageDescription data will live
        uint32_t descOffset = ifdOffset + 2     // count
                              + entryCount * 12 // entries
                              + 4;              // next-IFD pointer

        std::vector<uint8_t> buf;
        buf.reserve(descOffset + descLen);

        auto appendLE16 = [&](uint16_t v) {
            buf.push_back(uint8_t(v & 0xFF));
            buf.push_back(uint8_t((v >> 8) & 0xFF));
        };
        auto appendLE32 = [&](uint32_t v) {
            buf.push_back(uint8_t((v) & 0xFF));
            buf.push_back(uint8_t((v >> 8) & 0xFF));
            buf.push_back(uint8_t((v >> 16) & 0xFF));
            buf.push_back(uint8_t((v >> 24) & 0xFF));
        };

        // --- 1) TIFF header ---
        buf.push_back('I');
        buf.push_back('I');    // little-endian
        appendLE16(42);        // magic number
        appendLE32(ifdOffset); // offset to IFD

        // --- 2) Pixel data strip (one chunky strip) ---
        buf.insert(buf.end(), pixelData.begin(), pixelData.end());

        // --- 3) IFD ---
        appendLE16(entryCount);

        // Tag 256 = ImageWidth (LONG)
        appendLE16(256);
        appendLE16(4);
        appendLE32(1);
        appendLE32(width);

        // Tag 257 = ImageLength (LONG)
        appendLE16(257);
        appendLE16(4);
        appendLE32(1);
        appendLE32(height);

        // Tag 277 = SamplesPerPixel (SHORT)
        appendLE16(277);
        appendLE16(3);
        appendLE32(1);
        appendLE32(spp);

        // Tag 284 = PlanarConfiguration (SHORT)
        appendLE16(284);
        appendLE16(3);
        appendLE32(1);
        appendLE32(pcfg);

        // Tag 273 = StripOffsets (LONG), point to byte 8 (start of strip)
        appendLE16(273);
        appendLE16(4);
        appendLE32(1);
        appendLE32(8);

        // Tag 279 = StripByteCounts (LONG)
        appendLE16(279);
        appendLE16(4);
        appendLE32(1);
        appendLE32(pdSz);

        // Tag 270 = ImageDescription (ASCII)
        appendLE16(270);
        appendLE16(2);
        appendLE32(descLen);
        appendLE32(descOffset);

        // Next IFD pointer = 0 (none)
        appendLE32(0);

        // --- 4) Description data ---
        buf.insert(buf.end(), desc.begin(), desc.end());
        buf.push_back('\0');

        return buf;
    }

    /// Write a GeoTIFF file to disk. pixelData must be chunky interleaved
    /// (R1G1B1R2G2B2… for SPP=3). Only layer 0 is written.
    inline void WriteRasterCollection(geotiv::RasterCollection const &rc, std::vector<uint8_t> const &pixelData,
                                      fs::path const &outPath) {
        auto bytes = toTiffBytes(rc, pixelData);
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("geotiff::WriteRasterCollection(): cannot open \"" + outPath.string() + "\"");
        ofs.write(reinterpret_cast<char *>(bytes.data()), bytes.size());
    }

} // namespace geotiv
