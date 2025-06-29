#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "concord/concord.hpp"

namespace geotiv {

    using std::uint16_t;
    
    // Custom TIFF tag numbers for global properties (ASCII strings)
    constexpr uint16_t GLOBAL_PROPERTIES_BASE_TAG = 50100; // Base tag for global properties
    
    // Helper functions for ASCII tag encoding/decoding
    inline std::vector<uint32_t> stringToAsciiTag(const std::string& str) {
        // Add null terminator and pad to 4-byte boundary
        std::string padded = str + '\0';
        while (padded.size() % 4 != 0) {
            padded += '\0';
        }
        
        std::vector<uint32_t> result;
        result.reserve(padded.size() / 4);
        
        for (size_t i = 0; i < padded.size(); i += 4) {
            uint32_t value = 0;
            for (int j = 0; j < 4; ++j) {
                value |= (static_cast<uint32_t>(padded[i + j]) << (j * 8));
            }
            result.push_back(value);
        }
        return result;
    }
    
    inline std::string asciiTagToString(const std::vector<uint32_t>& data) {
        std::string result;
        result.reserve(data.size() * 4);
        
        for (uint32_t value : data) {
            for (int j = 0; j < 4; ++j) {
                char c = static_cast<char>((value >> (j * 8)) & 0xFF);
                if (c == '\0') {
                    return result; // Stop at null terminator
                }
                result += c;
            }
        }
        return result;
    }

    // CRS enum to replace the old concord::CRS enum
    enum class CRS {
        WGS,  // WGS84 coordinate system
        ENU   // East-North-Up coordinate system
    };

    struct Layer {
        // **New**: where in the file this IFD lived
        uint32_t ifdOffset = 0;

        // dims & layout
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t samplesPerPixel = 0;
        uint32_t planarConfig = 0;

        // strip info
        std::vector<uint32_t> stripOffsets;
        std::vector<uint32_t> stripByteCounts;

        // Per-IFD geospatial metadata (allows different coordinate systems per layer)
        CRS crs = CRS::WGS;
        concord::Datum datum;    // lat=lon=alt=0
        concord::Euler heading;  // roll=pitch=0, yaw=0
        double resolution = 1.0; // the representation of one pixel in meters

        // Additional GeoTIFF tags per IFD
        std::string imageDescription;
        std::map<uint16_t, std::vector<uint32_t>> customTags; // For additional TIFF tags

        // the actual samples, geo-gridded
        concord::Grid<uint8_t> grid;
        
        // Helper methods for global properties stored as ASCII custom tags
        void setGlobalProperty(const std::string& key, const std::string& value) {
            // Use a hash of the key to generate a unique tag number
            std::hash<std::string> hasher;
            uint16_t tag = GLOBAL_PROPERTIES_BASE_TAG + (hasher(key) % 1000);
            customTags[tag] = stringToAsciiTag(key + "=" + value);
        }
        
        std::unordered_map<std::string, std::string> getGlobalProperties() const {
            std::unordered_map<std::string, std::string> props;
            for (const auto& [tag, data] : customTags) {
                if (tag >= GLOBAL_PROPERTIES_BASE_TAG && tag < GLOBAL_PROPERTIES_BASE_TAG + 1000) {
                    std::string keyValue = asciiTagToString(data);
                    size_t eq_pos = keyValue.find('=');
                    if (eq_pos != std::string::npos) {
                        std::string key = keyValue.substr(0, eq_pos);
                        std::string value = keyValue.substr(eq_pos + 1);
                        props[key] = value;
                    }
                }
            }
            return props;
        }
    };

    struct RasterCollection {
        std::vector<Layer> layers; // one entry per IFD

        CRS crs = CRS::ENU;
        concord::Datum datum;   // lat=lon=alt=0
        concord::Euler heading; // roll=pitch=0, yaw=0
        double resolution;      // the representation of one pixel in meters
        
        // Helper methods for global properties (stored in first layer's customTags as ASCII tags)
        std::unordered_map<std::string, std::string> getGlobalPropertiesFromFirstLayer() const {
            if (!layers.empty()) {
                return layers[0].getGlobalProperties();
            }
            return {};
        }
        
        void setGlobalPropertiesOnAllLayers(const std::unordered_map<std::string, std::string>& props) {
            for (auto& layer : layers) {
                for (const auto& [key, value] : props) {
                    layer.setGlobalProperty(key, value);
                }
            }
        }
    };

} // namespace geotiv
