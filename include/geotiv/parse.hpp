#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/types_basic.hpp" // for CRS, Datum, Euler
#include "concord/types_line.hpp"
#include "concord/types_path.hpp"
#include "concord/types_polygon.hpp"

#include "geotiv/types.hpp"

namespace geotiv {
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------
    // Low-level TIFF readers
    // ------------------------------------------------------------------
    namespace detail {
        inline uint16_t readLE16(std::ifstream &f) {
            uint16_t v = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            return v;
        }
        inline uint32_t readLE32(std::ifstream &f) {
            uint32_t v = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            return v;
        }
        inline uint16_t readBE16(std::ifstream &f) {
            uint16_t v = 0, r = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            for (int i = 0; i < 2; ++i)
                r = (r << 8) | ((v >> (8 * i)) & 0xFF);
            return r;
        }
        inline uint32_t readBE32(std::ifstream &f) {
            uint32_t v = 0, r = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            for (int i = 0; i < 4; ++i)
                r = (r << 8) | ((v >> (8 * i)) & 0xFF);
            return r;
        }

        struct TIFFEntry {
            uint16_t tag, type;
            uint32_t count, valueOffset;
        };

        inline concord::CRS parseCRS(const std::string &s) {
            if (s == "ENU")
                return concord::CRS::ENU;
            if (s == "WGS" || s == "WGS84" || s == "EPSG:4326")
                return concord::CRS::WGS;
            throw std::runtime_error("geotiff::Unknown CRS string: " + s);
        }
    } // namespace detail

    // ------------------------------------------------------------------
    // Main reader: single-IFD or chained IFDs
    // ------------------------------------------------------------------
    inline geotiv::RasterCollection ReadRasterCollection(const fs::path &file) {
        std::ifstream f(file, std::ios::binary);
        if (!f)
            throw std::runtime_error("Cannot open \"" + file.string() + "\"");

        // 1) Read TIFF header
        char bom[2];
        f.read(bom, 2);
        bool little = (bom[0] == 'I' && bom[1] == 'I');
        if (!little && !(bom[0] == 'M' && bom[1] == 'M'))
            throw std::runtime_error("Bad TIFF byte-order");

        auto read16 = little ? detail::readLE16 : detail::readBE16;
        auto read32 = little ? detail::readLE32 : detail::readBE32;

        if (read16(f) != 42)
            throw std::runtime_error("Bad TIFF magic");
        uint32_t nextIFD = read32(f);

        geotiv::RasterCollection rc;

        // 2) Loop over IFDs
        while (nextIFD != 0) {
            // a) jump to this IFD
            f.seekg(nextIFD, std::ios::beg);
            uint16_t numEntries = read16(f);

            // b) read directory entries
            std::map<uint16_t, detail::TIFFEntry> entries;
            for (int i = 0; i < numEntries; ++i) {
                detail::TIFFEntry e;
                e.tag = read16(f);
                e.type = read16(f);
                e.count = read32(f);
                e.valueOffset = read32(f);
                entries[e.tag] = e;
            }
            // c) read pointer to following IFD
            nextIFD = read32(f);

            // d) helpers to extract numeric data
            auto getUInt = [&](uint16_t tag) -> uint32_t {
                auto it = entries.find(tag);
                if (it == entries.end())
                    return 0;
                auto &e = it->second;
                if (e.type == 3) // SHORT
                    return little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
                return e.valueOffset; // LONG
            };
            auto readUInts = [&](detail::TIFFEntry const &e) {
                std::vector<uint32_t> out;
                if (e.type != 4)
                    return out;
                if (e.count == 1) {
                    out.push_back(e.valueOffset);
                } else {
                    f.seekg(e.valueOffset, std::ios::beg);
                    for (uint32_t i = 0; i < e.count; ++i)
                        out.push_back(read32(f));
                }
                return out;
            };

            // e) build Layer
            geotiv::Layer L;
            // record which IFD this was:
            L.ifdOffset = nextIFD;
            L.width = getUInt(256);
            L.height = getUInt(257);
            L.samplesPerPixel = getUInt(277);
            L.planarConfig = getUInt(284);
            auto offs = readUInts(entries[273]);
            auto cnts = readUInts(entries[279]);
            L.stripOffsets = std::move(offs);
            L.stripByteCounts = std::move(cnts);

            // f) extract pixel bytes (assumes chunky single strip)
            if (L.stripOffsets.empty() || L.stripByteCounts.empty())
                throw std::runtime_error("Missing strip data");
            uint32_t off = L.stripOffsets[0], ct = L.stripByteCounts[0];
            std::vector<uint8_t> pix(ct);
            f.seekg(off, std::ios::beg);
            f.read(reinterpret_cast<char *>(pix.data()), ct);

            // g) only read ImageDescription for the *first* IFD
            static bool first = true;
            if (first) {
                first = false;
                auto itD = entries.find(270);
                if (itD != entries.end() && itD->second.type == 2) {
                    auto &e = itD->second;
                    std::vector<char> buf(e.count);
                    f.seekg(e.valueOffset, std::ios::beg);
                    f.read(buf.data(), e.count);
                    std::string desc(buf.data(), buf.data() + e.count);
                    std::istringstream ss(desc);
                    std::string tok;
                    while (ss >> tok) {
                        if (tok == "CRS") {
                            std::string s;
                            ss >> s;
                            rc.crs = detail::parseCRS(s);
                        } else if (tok == "DATUM") {
                            ss >> rc.datum.lat >> rc.datum.lon >> rc.datum.alt;
                        } else if (tok == "HEADING") {
                            double y;
                            ss >> y;
                            rc.heading = concord::Euler{0, 0, y};
                        }
                    }
                }
                if (!rc.datum.is_set())
                    throw std::runtime_error("Missing DATUM");
                if (!rc.heading.is_set())
                    throw std::runtime_error("Missing HEADING");
            }

            // h) build the geo-grid for this layer
            concord::WGS w{rc.datum.lat, rc.datum.lon, rc.datum.alt};
            concord::Point p{w, rc.datum};
            concord::Pose shift{p, rc.heading};

            concord::Grid<uint8_t> grid(
                /*rows=*/L.height,
                /*cols=*/L.width,
                /*diameter=*/1.0,
                /*datum=*/rc.datum,
                /*centered=*/true,
                /*shift=*/shift);
            size_t idx = 0;
            for (uint32_t r = 0; r < L.height; ++r)
                for (uint32_t c = 0; c < L.width; ++c)
                    grid(r, c).second = pix[idx++];

            L.grid = std::move(grid);
            rc.layers.push_back(std::move(L));
        }

        return rc;
    }

    // ------------------------------------------------------------------
    // Pretty-print
    // ------------------------------------------------------------------
    inline std::ostream &operator<<(std::ostream &os, geotiv::RasterCollection const &rc) {
        os << "GeoTIFF RasterCollection\n"
           << " CRS:     " << (rc.crs == concord::CRS::WGS ? "WGS" : "ENU") << "\n"
           << " DATUM:   " << rc.datum.lat << ", " << rc.datum.lon << ", " << rc.datum.alt << "\n"
           << " HEADING: yaw=" << rc.heading.yaw << "\n"
           << " Layers:  " << rc.layers.size() << "\n";
        for (auto const &L : rc.layers) {
            os << "  IFD@0x" << std::hex << L.ifdOffset << std::dec << " → " << L.width << "×" << L.height
               << ", SPP=" << L.samplesPerPixel << ", PC=" << L.planarConfig << "\n";
        }
        return os;
    }

} // namespace geotiv
