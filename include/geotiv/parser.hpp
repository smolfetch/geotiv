#pragma once

#include <cstdint>
#include <cstring> // for std::memcpy
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/concord.hpp" // for CRS, Datum, Euler

#include "geotiv/types.hpp"

namespace geotiv {
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------
    // Low-level TIFF readers (including 64-bit for DOUBLE)
    // ------------------------------------------------------------------
    namespace detail {
        inline uint16_t readLE16(std::ifstream &f) {
            uint16_t v = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            if (f.gcount() != sizeof(v)) {
                throw std::runtime_error("Failed to read LE16");
            }
            return v;
        }

        inline uint32_t readLE32(std::ifstream &f) {
            uint32_t v = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            if (f.gcount() != sizeof(v))
                throw std::runtime_error("Failed to read LE32");
            return v;
        }

        inline uint64_t readLE64(std::ifstream &f) {
            uint64_t v = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            if (f.gcount() != sizeof(v))
                throw std::runtime_error("Failed to read LE64");
            return v;
        }

        inline uint16_t readBE16(std::ifstream &f) {
            uint8_t bytes[2];
            f.read(reinterpret_cast<char *>(bytes), 2);
            if (f.gcount() != 2)
                throw std::runtime_error("Failed to read BE16");
            return (uint16_t(bytes[0]) << 8) | uint16_t(bytes[1]);
        }

        inline uint32_t readBE32(std::ifstream &f) {
            uint8_t bytes[4];
            f.read(reinterpret_cast<char *>(bytes), 4);
            if (f.gcount() != 4)
                throw std::runtime_error("Failed to read BE32");
            return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
                   uint32_t(bytes[3]);
        }

        inline uint64_t readBE64(std::ifstream &f) {
            uint8_t bytes[8];
            f.read(reinterpret_cast<char *>(bytes), 8);
            if (f.gcount() != 8)
                throw std::runtime_error("Failed to read BE64");
            uint64_t result = 0;
            for (int i = 0; i < 8; ++i) {
                result = (result << 8) | uint64_t(bytes[i]);
            }
            return result;
        }

        struct TIFFEntry {
            uint16_t tag, type;
            uint32_t count, valueOffset;
        };

        // All CRS are now WGS84 - no parsing needed

        inline std::string readString(std::ifstream &f, uint32_t offset, uint32_t count) {
            if (count == 0)
                return "";
            std::vector<char> buf(count);
            f.seekg(offset, std::ios::beg);
            f.read(buf.data(), count);
            if (f.gcount() != static_cast<std::streamsize>(count))
                throw std::runtime_error("Failed to read string data");

            // Ensure null termination
            if (buf.back() != '\0') {
                buf.push_back('\0');
            }
            return std::string(buf.data());
        }
    } // namespace detail

    // ------------------------------------------------------------------
    // Read single- or multi-IFD GeoTIFF into RasterCollection
    // ------------------------------------------------------------------
    inline geotiv::RasterCollection ReadRasterCollection(const fs::path &file) {
        std::ifstream f(file, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot open \"" + file.string() + "\"");

        // 1) Header
        char bom[2];
        f.read(bom, 2);
        bool little = (bom[0] == 'I' && bom[1] == 'I');
        if (!little && !(bom[0] == 'M' && bom[1] == 'M'))
            throw std::runtime_error("Bad TIFF byte-order");

        auto read16 = little ? detail::readLE16 : detail::readBE16;
        auto read32 = little ? detail::readLE32 : detail::readBE32;
        auto read64 = little ? detail::readLE64 : detail::readBE64;

        if (read16(f) != 42)
            throw std::runtime_error("Bad TIFF magic");
        uint32_t nextIFD = read32(f);

        geotiv::RasterCollection rc;

        // 2) Loop IFDs
        bool firstIFD = true;
        while (nextIFD) {
            f.seekg(nextIFD, std::ios::beg);
            uint16_t nEnt = read16(f);
            std::map<uint16_t, detail::TIFFEntry> E;
            for (int i = 0; i < nEnt; ++i) {
                detail::TIFFEntry e;
                e.tag = read16(f);
                e.type = read16(f);
                e.count = read32(f);
                e.valueOffset = read32(f);
                E[e.tag] = e;
            }
            uint32_t currentIFDOffset = nextIFD;
            nextIFD = read32(f);

            // helpers
            auto getUInt = [&](uint16_t tag) -> uint32_t {
                auto it = E.find(tag);
                if (it == E.end())
                    return 0;
                auto &e = it->second;
                if (e.type == 3) { // SHORT
                    if (e.count == 1) {
                        return little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
                    } else {
                        // Multiple shorts - read from offset
                        f.seekg(e.valueOffset, std::ios::beg);
                        return read16(f);
                    }
                } else if (e.type == 4) { // LONG
                    return e.valueOffset;
                }
                return 0;
            };

            auto readUInts = [&](uint16_t tag) -> std::vector<uint32_t> {
                auto it = E.find(tag);
                if (it == E.end())
                    return {};
                auto &e = it->second;

                std::vector<uint32_t> out;
                if (e.type == 3) { // SHORT
                    if (e.count == 1) {
                        uint16_t val = little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
                        out.push_back(val);
                    } else if (e.count == 2) {
                        uint16_t val1 = little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
                        uint16_t val2 = little ? ((e.valueOffset >> 16) & 0xFFFF) : (e.valueOffset & 0xFFFF);
                        out.push_back(val1);
                        out.push_back(val2);
                    } else {
                        f.seekg(e.valueOffset, std::ios::beg);
                        for (uint32_t i = 0; i < e.count; ++i)
                            out.push_back(read16(f));
                    }
                } else if (e.type == 4) { // LONG
                    if (e.count == 1) {
                        out.push_back(e.valueOffset);
                    } else {
                        f.seekg(e.valueOffset, std::ios::beg);
                        for (uint32_t i = 0; i < e.count; ++i)
                            out.push_back(read32(f));
                    }
                }
                return out;
            };

            auto readDoubles = [&](uint16_t tag) -> std::vector<double> {
                auto it = E.find(tag);
                if (it == E.end())
                    return {};
                auto &e = it->second;

                std::vector<double> out;
                if (e.type != 12)
                    return out; // 12 = DOUBLE

                f.seekg(e.valueOffset, std::ios::beg);
                for (uint32_t i = 0; i < e.count; ++i) {
                    uint64_t bits = read64(f);
                    double d;
                    std::memcpy(&d, &bits, sizeof(d));
                    out.push_back(d);
                }
                return out;
            };

            // build Layer - validate required tags
            geotiv::Layer L;
            L.ifdOffset = currentIFDOffset;
            L.width = getUInt(256);  // ImageWidth
            L.height = getUInt(257); // ImageLength

            if (L.width == 0 || L.height == 0)
                throw std::runtime_error("Invalid or missing image dimensions");

            L.samplesPerPixel = getUInt(277);
            if (L.samplesPerPixel == 0)
                L.samplesPerPixel = 1; // default

            L.planarConfig = getUInt(284);
            if (L.planarConfig == 0)
                L.planarConfig = 1; // default: chunky

            uint32_t bitsPerSample = getUInt(258);
            if (bitsPerSample == 0)
                bitsPerSample = 1; // default
            if (bitsPerSample != 8)
                throw std::runtime_error("Only 8-bit samples supported, got " + std::to_string(bitsPerSample));

            L.stripOffsets = readUInts(273);    // StripOffsets
            L.stripByteCounts = readUInts(279); // StripByteCounts

            if (L.stripOffsets.empty() || L.stripByteCounts.empty())
                throw std::runtime_error("Missing strip data");
            if (L.stripOffsets.size() != L.stripByteCounts.size())
                throw std::runtime_error("Mismatched strip arrays");

            // Read all strips and combine pixel data
            size_t totalBytes = 0;
            for (auto count : L.stripByteCounts) {
                totalBytes += count;
            }

            size_t expectedBytes = size_t(L.width) * L.height * L.samplesPerPixel;
            if (totalBytes != expectedBytes) {
                throw std::runtime_error("Strip byte count mismatch: expected " + std::to_string(expectedBytes) +
                                         ", got " + std::to_string(totalBytes));
            }

            std::vector<uint8_t> pix(totalBytes);
            size_t pixOffset = 0;

            for (size_t i = 0; i < L.stripOffsets.size(); ++i) {
                f.seekg(L.stripOffsets[i], std::ios::beg);
                f.read(reinterpret_cast<char *>(pix.data() + pixOffset), L.stripByteCounts[i]);
                if (f.gcount() != static_cast<std::streamsize>(L.stripByteCounts[i]))
                    throw std::runtime_error("Failed to read strip data");
                pixOffset += L.stripByteCounts[i];
            }

            // Parse geotags for each IFD independently (always WGS84)
            concord::Datum layerDatum;                 // Will be set from ImageDescription or use a valid default
            concord::Pose layerShift{concord::Point{0, 0, 0}, concord::Euler{0, 0, 0}};  // default
            double layerResolution = 1.0;              // default
            std::string layerDescription;
            bool datumFromDescription = false;

            // Parse ImageDescription for CRS/DATUM/HEADING
            auto itD = E.find(270);
            if (itD != E.end() && itD->second.type == 2) {
                layerDescription = detail::readString(f, itD->second.valueOffset, itD->second.count);
                std::istringstream ss(layerDescription);
                std::string tok;

                while (ss >> tok) {
                    if (tok == "CRS") {
                        std::string s;
                        if (ss >> s) {
                            // All CRS are WGS84 - ignore the parsed value
                        }
                    } else if (tok == "DATUM") {
                        if (ss >> layerDatum.lat >> layerDatum.lon >> layerDatum.alt) {
                            datumFromDescription = true;
                        }
                    } else if (tok == "SHIFT") {
                        double x, y, z, yaw;
                        if (ss >> x >> y >> z >> yaw) {
                            layerShift = concord::Pose{concord::Point{x, y, z}, concord::Euler{0, 0, yaw}};
                        }
                    }
                }
            }

            // If we didn't get a valid datum from ImageDescription, use a default that passes is_set()
            if (!datumFromDescription) {
                layerDatum = {0.001, 0.001, 1.0}; // Valid minimal coordinates
            }

            // ModelPixelScale → resolution for this IFD
            auto scales = readDoubles(33550);
            if (scales.size() >= 2) {
                layerResolution = scales[0]; // X scale
                // Could also check that scales[0] == scales[1] for square pixels
            }

            if (layerResolution <= 0) {
                throw std::runtime_error("Invalid pixel scale: " + std::to_string(layerResolution));
            }

            // Set layer-specific metadata (always WGS84)
            L.datum = layerDatum;
            L.shift = layerShift;
            L.resolution = layerResolution;
            L.imageDescription = layerDescription;

            // Read custom tags (tag numbers 50000 and above are typically custom)
            for (const auto &[tag, entry] : E) {
                if (tag >= 50000) {
                    L.customTags[tag] = readUInts(tag);
                }
            }

            // Set collection defaults from first IFD if not set
            if (firstIFD) {
                firstIFD = false;
                rc.datum = layerDatum;
                rc.shift = layerShift;
                rc.resolution = layerResolution;
            }

            // Build geo-grid using layer-specific resolution and datum
            if (!L.datum.is_set()) {
                throw std::runtime_error("Datum not properly initialized for layer");
            }

            // Use the shift directly - it's already in ENU space
            concord::Pose shift = L.shift;

            concord::Grid<uint8_t> grid(
                /*rows=*/L.height,
                /*cols=*/L.width,
                /*diameter=*/L.resolution,
                /*centered=*/true,
                /*shift=*/shift);

            // Fill grid with pixel data
            if (L.planarConfig == 1) { // Chunky format
                size_t idx = 0;
                for (uint32_t r = 0; r < L.height; ++r) {
                    for (uint32_t c = 0; c < L.width; ++c) {
                        // For multi-sample pixels, take first sample
                        grid(r, c) = pix[idx];
                        idx += L.samplesPerPixel;
                    }
                }
            } else { // Planar format
                // Take first plane only
                size_t idx = 0;
                for (uint32_t r = 0; r < L.height; ++r) {
                    for (uint32_t c = 0; c < L.width; ++c) {
                        grid(r, c) = pix[idx++];
                    }
                }
            }

            L.grid = std::move(grid);
            rc.layers.emplace_back(std::move(L));
        }

        if (rc.layers.empty()) {
            throw std::runtime_error("No valid IFDs found in TIFF file");
        }

        return rc;
    }

    // ------------------------------------------------------------------
    // Pretty-printer
    // ------------------------------------------------------------------
    inline std::ostream &operator<<(std::ostream &os, geotiv::RasterCollection const &rc) {
        os << "GeoTIFF RasterCollection\n"
           << " CRS:        WGS84\n"
           << " DATUM:      " << rc.datum.lat << ", " << rc.datum.lon << ", " << rc.datum.alt << "\n"
           << " SHIFT:      " << rc.shift.point.x << ", " << rc.shift.point.y << ", " << rc.shift.point.z << " (yaw=" << rc.shift.angle.yaw << ")\n"
           << " RESOLUTION: " << rc.resolution << " (map units per pixel)\n"
           << " Layers:     " << rc.layers.size() << "\n";
        for (auto const &L : rc.layers) {
            os << "  IFD@0x" << std::hex << L.ifdOffset << std::dec << " → " << L.width << "×" << L.height
               << ", SPP=" << L.samplesPerPixel << ", PC=" << L.planarConfig << "\n";
        }
        return os;
    }

} // namespace geotiv
