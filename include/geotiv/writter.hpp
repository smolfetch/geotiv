#pragma once

#include <cstdint>
#include <cstring> // for std::memcpy
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/concord.hpp" // Datum, Euler
#include "geotiv/types.hpp"    // RasterCollection

namespace geotiv {
    namespace fs = std::filesystem;

    /// Write out all layers in rc as a chained‐IFD GeoTIFF.
    /// Each IFD can have its own CRS/DATUM/HEADING/PixelScale and custom tags.
    ///
    /// CRS Flavor Handling:
    /// - ENU flavor: Grid data is already in local space, datum provides reference
    /// - WGS flavor: Grid data represents WGS coordinates, datum provides reference
    /// The Grid object contains the appropriate coordinate system based on parsing
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
                    uint8_t v = g(r, c);
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

        // --- 3) Prepare per-layer metadata ---
        std::vector<std::string> descriptions(N);
        std::vector<uint32_t> descLengths(N);
        std::vector<uint32_t> descOffsets(N);
        std::vector<uint32_t> scaleOffsets(N);
        std::vector<uint32_t> geoKeyOffsets(N);
        std::vector<uint32_t> tiepointOffsets(N);

        for (size_t i = 0; i < N; ++i) {
            auto const &layer = rc.layers[i];

            // Build ImageDescription for this layer
            if (!layer.imageDescription.empty()) {
                descriptions[i] = layer.imageDescription; // Use custom description if provided
            } else {
                // Generate geospatial description (always WGS84)
                descriptions[i] = "CRS WGS84 DATUM " + std::to_string(layer.datum.lat) + " " +
                                  std::to_string(layer.datum.lon) + " " + std::to_string(layer.datum.alt) + " SHIFT " +
                                  std::to_string(layer.shift.point.x) + " " + std::to_string(layer.shift.point.y) +
                                  " " + std::to_string(layer.shift.point.z) + " " +
                                  std::to_string(layer.shift.angle.yaw);
            }

            descLengths[i] = uint32_t(descriptions[i].size() + 1);
        }

        // --- 4) Compute IFD offsets and sizes ---
        std::vector<uint16_t> entryCounts(N);
        std::vector<uint32_t> customDataOffsets(N);
        std::vector<uint32_t> customDataSizes(N);

        for (size_t i = 0; i < N; ++i) {
            // Base tags: 9 standard + ImageDescription + PlanarConfig + ModelPixelScale + GeoKeyDirectory +
            // ModelTiepointTag + custom tags
            entryCounts[i] = 9 + 1 + 1 + 1 + 1 + 1 + static_cast<uint16_t>(rc.layers[i].customTags.size());

            // Calculate space needed for multi-value custom tag data
            customDataSizes[i] = 0;
            for (const auto &[tag, values] : rc.layers[i].customTags) {
                if (values.size() > 1) {
                    customDataSizes[i] += static_cast<uint32_t>(values.size() * 4); // 4 bytes per uint32_t
                }
            }
        }

        std::vector<uint32_t> ifdSizes(N);
        for (size_t i = 0; i < N; ++i) {
            ifdSizes[i] = 2 + entryCounts[i] * 12 + 4; // entry count + entries + next IFD pointer
        }

        std::vector<uint32_t> ifdOffsets(N);
        p = firstIFD;
        for (size_t i = 0; i < N; ++i) {
            ifdOffsets[i] = p;
            p += ifdSizes[i];
        }

        // --- 5) Compute offsets for variable-length data ---
        for (size_t i = 0; i < N; ++i) {
            descOffsets[i] = p;
            p += descLengths[i];

            scaleOffsets[i] = p;
            p += 24; // 3 doubles = 24 bytes

            geoKeyOffsets[i] = p;
            p += 56; // GeoKeyDirectory = 56 bytes (4 keys for WGS84)

            tiepointOffsets[i] = p;
            p += 48; // ModelTiepointTag = 48 bytes (6 doubles)

            // Custom tag data offset
            customDataOffsets[i] = p;
            p += customDataSizes[i];
        }

        uint32_t totalSize = p;

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

            writeLE16(entryCounts[i]);

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

            // Tag 270: ImageDescription
            writeLE16(270);
            writeLE16(2);
            writeLE32(descLengths[i]);
            writeLE32(descOffsets[i]);

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

            // Tag 284: PlanarConfiguration
            writeLE16(284);
            writeLE16(3);
            writeLE32(1);
            writeLE32(PC);

            // Tag 33550: ModelPixelScaleTag
            writeLE16(33550);
            writeLE16(12);
            writeLE32(3);
            writeLE32(scaleOffsets[i]);

            // Tag 34735: GeoKeyDirectoryTag
            writeLE16(34735);
            writeLE16(3);
            writeLE32(14);
            writeLE32(geoKeyOffsets[i]);

            // Tag 33922: ModelTiepointTag
            writeLE16(33922);
            writeLE16(12);
            writeLE32(6);
            writeLE32(tiepointOffsets[i]);

            // Write custom tags for this layer
            uint32_t customDataPos = customDataOffsets[i];
            for (const auto &[tag, values] : layer.customTags) {
                writeLE16(tag);
                writeLE16(4); // LONG type
                writeLE32(static_cast<uint32_t>(values.size()));
                if (values.size() == 1) {
                    writeLE32(values[0]); // Value fits in offset field
                } else {
                    writeLE32(customDataPos); // Pointer to data
                    customDataPos += static_cast<uint32_t>(values.size() * 4);
                }
            }

            // next IFD pointer
            uint32_t next = (i + 1 < N ? ifdOffsets[i + 1] : 0);
            writeLE32(next);
        }

        // --- 9) Write variable-length data for each layer ---
        for (size_t i = 0; i < N; ++i) {
            auto const &layer = rc.layers[i];

            // Description text + NUL
            writePos = descOffsets[i];
            std::memcpy(&buf[writePos], descriptions[i].data(), descriptions[i].size());
            buf[writePos + descriptions[i].size()] = '\0';

            // PixelScale doubles: X, Y, Z
            writePos = scaleOffsets[i];
            writeDouble(layer.resolution); // X scale
            writeDouble(layer.resolution); // Y scale
            writeDouble(0.0);              // Z scale

            // GeoKeyDirectory for this layer (always WGS84)
            writePos = geoKeyOffsets[i];
            writeLE16(1); // KeyDirectoryVersion
            writeLE16(1); // KeyRevision
            writeLE16(0); // MinorRevision
            writeLE16(4); // NumberOfKeys

            // Key entry 1: GTModelTypeGeoKey
            writeLE16(1024); // GTModelTypeGeoKey
            writeLE16(0);    // TIFFTagLocation (0 means value is in ValueOffset)
            writeLE16(1);    // Count
            writeLE16(2);    // 2=Geographic (WGS84)

            // Key entry 2: GTRasterTypeGeoKey
            writeLE16(1025); // GTRasterTypeGeoKey
            writeLE16(0);    // TIFFTagLocation
            writeLE16(1);    // Count
            writeLE16(1);    // RasterPixelIsArea

            // Key entry 3: GeographicTypeGeoKey - EPSG:4326 for WGS84
            writeLE16(2048); // GeographicTypeGeoKey
            writeLE16(0);    // TIFFTagLocation
            writeLE16(1);    // Count
            writeLE16(4326); // EPSG:4326 (WGS84)

            // Key entry 4: GeogAngularUnitsGeoKey - degrees for WGS84
            writeLE16(2054); // GeogAngularUnitsGeoKey
            writeLE16(0);    // TIFFTagLocation
            writeLE16(1);    // Count
            writeLE16(9102); // 9102=degree

            // Write ModelTiepointTag for this layer
            writePos = tiepointOffsets[i];
            auto const &g = layer.grid;
            uint32_t W = uint32_t(g.cols());
            uint32_t H = uint32_t(g.rows());

            // Tiepoint format: I,J,K,X,Y,Z where (I,J,K) are pixel coords and (X,Y,Z) are world coords
            // We'll tie the center of the image to the world coordinates calculated from shift
            writeDouble(W / 2.0); // I: pixel column (center)
            writeDouble(H / 2.0); // J: pixel row (center)
            writeDouble(0.0);     // K: always 0 for 2D

            // Convert shift (ENU) to WGS84 coordinates using datum
            concord::ENU enuShift{layer.shift.point.x, layer.shift.point.y, layer.shift.point.z, layer.datum};
            concord::WGS anchorWGS = enuShift.toWGS();

            writeDouble(anchorWGS.lon); // X: longitude
            writeDouble(anchorWGS.lat); // Y: latitude
            writeDouble(anchorWGS.alt); // Z: altitude

            // Write custom tag data for this layer
            writePos = customDataOffsets[i];
            for (const auto &[tag, values] : layer.customTags) {
                if (values.size() > 1) {
                    for (uint32_t value : values) {
                        writeLE32(value);
                    }
                }
            }
        }

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
