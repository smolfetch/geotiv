#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "concord/types_basic.hpp" // for concord::CRS, Datum, Euler
#include "concord/types_line.hpp"  // if you need Geometry output later
#include "concord/types_path.hpp"
#include "concord/types_polygon.hpp"

#include "geotiv/types.hpp"

namespace geotiv {

    using std::uint16_t;
    using std::uint32_t;
    namespace fs = std::filesystem;

    // ------------------------------------------------------------------
    // Internal helpers
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
            uint16_t tag;
            uint16_t type;
            uint32_t count;
            uint32_t valueOffset;
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
    // Main reader
    // ------------------------------------------------------------------

    inline RasterCollection ReadRasterCollection(const fs::path &file) {
        std::ifstream f(file, std::ios::binary);
        if (!f) {
            throw std::runtime_error("geotiff::ReadRasterCollection(): cannot open \"" + file.string() + "\"");
        }

        // 1) TIFF header: byte order, magic, first IFD offset
        char bom[2];
        f.read(bom, 2);
        bool little;
        if (bom[0] == 'I' && bom[1] == 'I')
            little = true;
        else if (bom[0] == 'M' && bom[1] == 'M')
            little = false;
        else
            throw std::runtime_error("geotiff::Bad TIFF byte order");

        auto read16 = little ? detail::readLE16 : detail::readBE16;
        auto read32 = little ? detail::readLE32 : detail::readBE32;

        uint16_t magic = read16(f);
        if (magic != 42)
            throw std::runtime_error("geotiff::Unexpected TIFF magic: " + std::to_string(magic));

        uint32_t ifdOffset = read32(f);

        // 2) Seek to IFD and read entries
        f.seekg(ifdOffset, std::ios::beg);
        uint16_t numEntries = read16(f);

        std::map<uint16_t, detail::TIFFEntry> entries;
        for (int i = 0; i < numEntries; ++i) {
            detail::TIFFEntry e;
            e.tag = read16(f);
            e.type = read16(f);
            e.count = read32(f);
            e.valueOffset = read32(f);
            entries[e.tag] = e;
        }
        // ignore next-IFD pointer (single-IFD mode)

        // helpers to extract numeric tags
        auto getUInt = [&](uint16_t tagID) -> uint32_t {
            auto it = entries.find(tagID);
            if (it == entries.end())
                return 0;
            auto &e = it->second;
            if (e.type == 3) { // SHORT
                return little ? (e.valueOffset & 0xFFFF) : ((e.valueOffset >> 16) & 0xFFFF);
            }
            return e.valueOffset; // LONG
        };
        auto readUIntVec = [&](const detail::TIFFEntry &e) {
            std::vector<uint32_t> out;
            if (e.type != 4)
                return out;
            if (e.count == 1) {
                out.push_back(e.valueOffset);
            } else {
                f.seekg(e.valueOffset, std::ios::beg);
                for (uint32_t i = 0; i < e.count; ++i) {
                    out.push_back(read32(f));
                }
            }
            return out;
        };

        RasterCollection rc;

        // 3) Build our one Layer
        Layer L;
        L.width = getUInt(256);           // ImageWidth
        L.height = getUInt(257);          // ImageLength
        L.samplesPerPixel = getUInt(277); // SamplesPerPixel
        L.planarConfig = getUInt(284);    // PlanarConfiguration

        auto itOff = entries.find(273), itCnt = entries.find(279);
        if (itOff != entries.end() && itCnt != entries.end()) {
            L.stripOffsets = readUIntVec(itOff->second);
            L.stripByteCounts = readUIntVec(itCnt->second);
        }
        rc.layers.push_back(std::move(L));

        // 4) Read ImageDescription (ascii) for CRS, DATUM, HEADING
        std::string desc;
        auto itDesc = entries.find(270);
        if (itDesc != entries.end() && itDesc->second.type == 2) {
            auto &e = itDesc->second;
            std::vector<char> buf(e.count);
            f.seekg(e.valueOffset, std::ios::beg);
            f.read(buf.data(), e.count);
            desc.assign(buf.data(), buf.data() + e.count);
        }

        // 5) Tokenize
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
                double yaw;
                ss >> yaw;
                rc.heading = concord::Euler{0.0, 0.0, yaw};
            }
        }

        // 6) Validate mandatory fields
        if (!rc.datum.is_set())
            throw std::runtime_error("geotiff::missing DATUM in ImageDescription");
        if (!rc.heading.is_set())
            throw std::runtime_error("geotiff::missing HEADING in ImageDescription");

        return rc;
    }

    // ------------------------------------------------------------------
    // Optional pretty-printer
    // ------------------------------------------------------------------

    inline std::ostream &operator<<(std::ostream &os, RasterCollection const &rc) {
        os << "GeoTIFF RasterCollection\n";
        os << " CRS:     " << (rc.crs == concord::CRS::WGS ? "WGS" : "ENU") << "\n";
        os << " DATUM:   " << rc.datum.lat << ", " << rc.datum.lon << ", " << rc.datum.alt << "\n";
        os << " HEADING: yaw=" << rc.heading.yaw << "\n";
        os << " Layers:  " << rc.layers.size() << "\n";
        for (size_t i = 0; i < rc.layers.size(); ++i) {
            auto &L = rc.layers[i];
            os << "  [" << i << "] " << L.width << "×" << L.height << ", SPP=" << L.samplesPerPixel
               << (L.planarConfig == 1 ? " chunky" : " planar") << ", strips=" << L.stripOffsets.size() << "\n";
        }
        return os;
    }

} // namespace geotiv
