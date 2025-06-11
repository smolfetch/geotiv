#pragma once

#include <cstdint>
#include <cstring> // for std::memcpy
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/concord.hpp" // concord::CRS, Datum, Euler
#include "geotiv/types.hpp"    // RasterCollection

namespace geotiv {
    namespace fs = std::filesystem;

    /// Write out all layers in rc as a chained‐IFD GeoTIFF.
    /// All IFDs share the same CRS/DATUM/HEADING/PixelScale (only in the first IFD).
    inline std::vector<uint8_t> toTiffBytes(RasterCollection const &rc) {
        size_t N = rc.layers.size();
        if (N == 0)
            throw std::runtime_error("toTiffBytes(): no layers");

        // --- 1) Flatten each layer's grid into its own chunky strip ---
        std::vector<std::vector<uint8_t>> strips(N);
        std::vector<uint32_t> stripCounts(N), stripOffsets(N);
        for (size_t i = 0; i < N; ++i) {
            auto const &layer = rc.layers[i];
            auto const &g = layer.grid;
            uint32_t W = static_cast<uint32_t>(g.cols());
            uint32_t H = static_cast<uint32_t>(g.rows());
            uint32_t S = layer.samplesPerPixel;
            size_t sz = size_t(W) * H * S;
            strips[i].resize(sz);
            stripCounts[i] = uint32_t(sz);
            // fill chunky: band0,band1,... per pixel
            size_t idx = 0;
            for (uint32_t r = 0; r < H; ++r) {
                for (uint32_t c = 0; c < W; ++c) {
                    uint8_t v = g(r, c).second;
                    for (uint32_t s = 0; s < S; ++s) {
                        strips[i][idx++] = v;
                    }
                }
            }
        }

        // --- 2) Compute strip offsets (right after the 8‐byte TIFF header) ---
        uint32_t p = 8;
        for (size_t i = 0; i < N; ++i) {
            stripOffsets[i] = p;
            p += stripCounts[i];
        }
        uint32_t firstIFD = p;

        // --- 3) Compute IFD offsets ---
        uint16_t entryCount0 = 13; // first IFD: basic tags + GeoTIFF tags
        uint16_t entryCount1 = 9;  // subsequent IFDs: basic tags only
        uint32_t ifdSize0 = 2 + entryCount0 * 12 + 4;
        uint32_t ifdSize1 = 2 + entryCount1 * 12 + 4;

        std::vector<uint32_t> ifdOffsets(N);
        p = firstIFD;
        for (size_t i = 0; i < N; ++i) {
            ifdOffsets[i] = p;
            p += (i == 0 ? ifdSize0 : ifdSize1);
        }

        // --- 4) Build shared data after IFDs ---
        // ImageDescription
        std::string desc = "CRS " + std::string(rc.crs == concord::CRS::WGS ? "WGS" : "ENU") + " DATUM " +
                           std::to_string(rc.datum.lat) + " " + std::to_string(rc.datum.lon) + " " +
                           std::to_string(rc.datum.alt) + " HEADING " + std::to_string(rc.heading.yaw);
        uint32_t descLen = uint32_t(desc.size() + 1);
        uint32_t descOffset = p;

        // PixelScale (3 doubles: X, Y, Z)
        uint32_t scaleOffset = descOffset + descLen;

        // GeoKeyDirectory (simple version - just set coordinate system)
        uint32_t geoKeyOffset = scaleOffset + 24; // 3 doubles = 24 bytes
        uint32_t geoKeySize = 32;                 // 8 entries * 4 bytes = 32 bytes

        uint32_t totalSize = geoKeyOffset + geoKeySize;

        // --- 5) Allocate final buffer ---
        std::vector<uint8_t> buf(totalSize);
        size_t writePos = 0;

        // LE writers
        auto writeLE16 = [&](uint16_t v) {
            buf[writePos++] = uint8_t(v & 0xFF);
            buf[writePos++] = uint8_t(v >> 8);
        };
        auto writeLE32 = [&](uint32_t v) {
            buf[writePos++] = uint8_t(v & 0xFF);
            buf[writePos++] = uint8_t((v >> 8) & 0xFF);
            buf[writePos++] = uint8_t((v >> 16) & 0xFF);
            buf[writePos++] = uint8_t((v >> 24) & 0xFF);
        };
        auto writeDouble = [&](double d) {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof(d));
            for (int i = 0; i < 8; ++i) {
                buf[writePos++] = uint8_t((bits >> (8 * i)) & 0xFF);
            }
        };

        // --- 6) TIFF header ---
        buf[writePos++] = 'I';
        buf[writePos++] = 'I'; // little‐endian
        writeLE16(42);         // magic
        writeLE32(firstIFD);   // offset to first IFD

        // --- 7) Pixel data strips ---
        for (size_t i = 0; i < N; ++i) {
            std::memcpy(&buf[writePos], strips[i].data(), strips[i].size());
            writePos += strips[i].size();
        }

        // --- 8) IFDs ---
        for (size_t i = 0; i < N; ++i) {
            // Seek to the correct IFD position
            writePos = ifdOffsets[i];

            bool first = (i == 0);
            uint16_t ec = first ? entryCount0 : entryCount1;
            writeLE16(ec);

            auto const &layer = rc.layers[i];
            auto const &g = layer.grid;
            uint32_t W = uint32_t(g.cols());
            uint32_t H = uint32_t(g.rows());
            uint32_t S = layer.samplesPerPixel;
            uint32_t PC = layer.planarConfig;

            // Tag 256: ImageWidth
            writeLE16(256);
            writeLE16(4);
            writeLE32(1);
            writeLE32(W);

            // Tag 257: ImageLength
            writeLE16(257);
            writeLE16(4);
            writeLE32(1);
            writeLE32(H);

            // Tag 258: BitsPerSample
            writeLE16(258);
            writeLE16(3);
            writeLE32(1);
            writeLE32(8);

            // Tag 259: Compression (1 = uncompressed)
            writeLE16(259);
            writeLE16(3);
            writeLE32(1);
            writeLE32(1);

            // Tag 262: PhotometricInterpretation (1 = BlackIsZero)
            writeLE16(262);
            writeLE16(3);
            writeLE32(1);
            writeLE32(1);

            // Tag 273: StripOffsets
            writeLE16(273);
            writeLE16(4);
            writeLE32(1);
            writeLE32(stripOffsets[i]);

            // Tag 277: SamplesPerPixel
            writeLE16(277);
            writeLE16(3);
            writeLE32(1);
            writeLE32(S);

            // Tag 278: RowsPerStrip
            writeLE16(278);
            writeLE16(4);
            writeLE32(1);
            writeLE32(H);

            // Tag 279: StripByteCounts
            writeLE16(279);
            writeLE16(4);
            writeLE32(1);
            writeLE32(stripCounts[i]);

            if (first) {
                // Tag 270: ImageDescription
                writeLE16(270);
                writeLE16(2);
                writeLE32(descLen);
                writeLE32(descOffset);

                // Tag 284: PlanarConfiguration
                writeLE16(284);
                writeLE16(3);
                writeLE32(1);
                writeLE32(PC);

                // Tag 33550: ModelPixelScaleTag
                writeLE16(33550);
                writeLE16(12);
                writeLE32(3);
                writeLE32(scaleOffset);

                // Tag 34735: GeoKeyDirectoryTag
                writeLE16(34735);
                writeLE16(3);
                writeLE32(8);
                writeLE32(geoKeyOffset);
            } else {
                // Tag 284: PlanarConfiguration
                writeLE16(284);
                writeLE16(3);
                writeLE32(1);
                writeLE32(PC);
            }

            // next IFD pointer
            uint32_t next = (i + 1 < N ? ifdOffsets[i + 1] : 0);
            writeLE32(next);
        }

        // --- 9) Write variable-length data ---

        // Description text + NUL
        writePos = descOffset;
        std::memcpy(&buf[writePos], desc.data(), desc.size());
        buf[writePos + desc.size()] = '\0';

        // PixelScale doubles: X, Y, Z
        writePos = scaleOffset;
        writeDouble(rc.resolution); // X scale
        writeDouble(rc.resolution); // Y scale
        writeDouble(0.0);           // Z scale

        // GeoKeyDirectory (minimal version)
        writePos = geoKeyOffset;
        writeLE16(1); // KeyDirectoryVersion
        writeLE16(1); // KeyRevision
        writeLE16(0); // MinorRevision
        writeLE16(1); // NumberOfKeys

        // One key entry: GTModelTypeGeoKey
        writeLE16(1024);                                // GTModelTypeGeoKey
        writeLE16(0);                                   // TIFFTagLocation (0 means value is in ValueOffset)
        writeLE16(1);                                   // Count
        writeLE16(rc.crs == concord::CRS::WGS ? 2 : 1); // 2=Geographic, 1=Projected

        return buf;
    }

    /// Write a multi‐IFD GeoTIFF to disk
    inline void WriteRasterCollection(RasterCollection const &rc, fs::path const &outPath) {
        auto bytes = toTiffBytes(rc);
        std::ofstream ofs(outPath, std::ios::binary);
        if (!ofs)
            throw std::runtime_error("cannot open " + outPath.string());
        ofs.write(reinterpret_cast<char *>(bytes.data()), bytes.size());
    }

} // namespace geotiv
