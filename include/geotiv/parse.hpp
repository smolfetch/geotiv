// geotiff/parser.hpp
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

#include "concord/types_basic.hpp" // for CRS, Datum, Euler
#include "concord/types_grid.hpp"

#include "geotiv/types.hpp"

namespace geotiff {
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------
    // Low-level TIFF readers (including 64-bit for DOUBLE)
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
        inline uint64_t readLE64(std::ifstream &f) {
            uint64_t v = 0;
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
        inline uint64_t readBE64(std::ifstream &f) {
            uint64_t v = 0, r = 0;
            f.read(reinterpret_cast<char *>(&v), sizeof(v));
            for (int i = 0; i < 8; ++i)
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
            nextIFD = read32(f);

            // helpers
            auto getUInt = [&](uint16_t tag) -> uint32_t {
                auto it = E.find(tag);
                if (it == E.end())
                    return 0;
                auto &e = it->second;
                if (e.type == 3) // SHORT
                    return little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
                return e.valueOffset; // LONG
            };
            auto readUInts = [&](auto const &e) {
                std::vector<uint32_t> out;
                if (e.type != 4)
                    return out;
                if (e.count == 1)
                    out.push_back(e.valueOffset);
                else {
                    f.seekg(e.valueOffset, std::ios::beg);
                    for (uint32_t i = 0; i < e.count; ++i)
                        out.push_back(read32(f));
                }
                return out;
            };
            auto readDoubles = [&](auto const &e) {
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

            // build Layer
            geotiv::Layer L;
            L.ifdOffset = nextIFD;
            L.width = getUInt(256);
            L.height = getUInt(257);
            L.samplesPerPixel = getUInt(277);
            L.planarConfig = getUInt(284);
            L.stripOffsets = readUInts(E[273]);
            L.stripByteCounts = readUInts(E[279]);

            // pixel bytes
            if (L.stripOffsets.empty() || L.stripByteCounts.empty())
                throw std::runtime_error("Missing strip data");
            uint32_t off = L.stripOffsets[0], ct = L.stripByteCounts[0];
            std::vector<uint8_t> pix(ct);
            f.seekg(off, std::ios::beg);
            f.read(reinterpret_cast<char *>(pix.data()), ct);

            // parse geotags on first IFD only
            if (firstIFD) {
                firstIFD = false;
                // CRS/DATUM/HEADING
                auto itD = E.find(270);
                if (itD != E.end() && itD->second.type == 2) {
                    std::vector<char> buf(itD->second.count);
                    f.seekg(itD->second.valueOffset, std::ios::beg);
                    f.read(buf.data(), buf.size());
                    std::string desc(buf.data(), buf.size());
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

                // ModelPixelScale → resolution
                auto itS = E.find(33550);
                if (itS != E.end()) {
                    auto scales = readDoubles(itS->second);
                    if (scales.size() >= 2)
                        rc.resolution = scales[0];
                }
                if (rc.resolution <= 0) {
                    // fallback: assume 1.0 if no tag
                    rc.resolution = 1.0;
                }
            }

            // build geo-grid using rc.resolution
            concord::WGS w0{rc.datum.lat, rc.datum.lon, rc.datum.alt};
            concord::Point p0{w0, rc.datum};
            concord::Pose shift{p0, rc.heading};

            concord::Grid<uint8_t> grid(
                /*rows=*/L.height,
                /*cols=*/L.width,
                /*diameter=*/rc.resolution,
                /*datum=*/rc.datum,
                /*centered=*/true,
                /*shift=*/shift);
            size_t idx = 0;
            for (uint32_t r = 0; r < L.height; ++r)
                for (uint32_t c = 0; c < L.width; ++c)
                    grid(r, c).second = pix[idx++];
            L.grid = std::move(grid);

            rc.layers.emplace_back(std::move(L));
        }

        return rc;
    }

    // ------------------------------------------------------------------
    // Pretty-printer
    // ------------------------------------------------------------------
    inline std::ostream &operator<<(std::ostream &os, geotiv::RasterCollection const &rc) {
        os << "GeoTIFF RasterCollection\n"
           << " CRS:        " << (rc.crs == concord::CRS::WGS ? "WGS" : "ENU") << "\n"
           << " DATUM:      " << rc.datum.lat << ", " << rc.datum.lon << ", " << rc.datum.alt << "\n"
           << " HEADING:    yaw=" << rc.heading.yaw << "\n"
           << " RESOLUTION: " << rc.resolution << " (map units per pixel)\n"
           << " Layers:     " << rc.layers.size() << "\n";
        for (auto const &L : rc.layers) {
            os << "  IFD@0x" << std::hex << L.ifdOffset << std::dec << " → " << L.width << "×" << L.height
               << ", SPP=" << L.samplesPerPixel << ", PC=" << L.planarConfig << "\n";
        }
        return os;
    }

} // namespace geotiff
