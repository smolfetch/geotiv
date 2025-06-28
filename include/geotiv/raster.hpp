#pragma once

#include "geotiv.hpp"
#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace geotiv {

struct GridLayer {
    concord::Grid<uint8_t> grid;
    std::string name;
    std::string type;
    std::unordered_map<std::string, std::string> properties;

    GridLayer(const concord::Grid<uint8_t>& g, const std::string& layer_name, 
              const std::string& layer_type = "", 
              const std::unordered_map<std::string, std::string>& props = {})
        : grid(g), name(layer_name), type(layer_type), properties(props) {}
};

class Raster {
private:
    std::vector<GridLayer> grid_layers_;
    concord::Datum datum_;
    concord::Euler heading_;
    CRS crs_;
    double resolution_;

public:
    Raster(const concord::Datum& datum = concord::Datum{0.001, 0.001, 1.0},
           const concord::Euler& heading = concord::Euler{0, 0, 0},
           CRS crs = CRS::ENU,
           double resolution = 1.0)
        : datum_(datum), heading_(heading), crs_(crs), resolution_(resolution) {}

    static Raster fromFile(const std::filesystem::path& path) {
        auto rc = geotiv::ReadRasterCollection(path);
        
        if (rc.layers.empty()) {
            throw std::runtime_error("Raster::fromFile: No layers found in file");
        }

        Raster raster(rc.datum, rc.heading, rc.crs, rc.resolution);
        
        for (const auto& layer : rc.layers) {
            std::string layerName = "layer_" + std::to_string(layer.ifdOffset);
            std::string layerType = "unknown";
            std::unordered_map<std::string, std::string> props;
            
            if (!layer.imageDescription.empty()) {
                std::istringstream ss(layer.imageDescription);
                std::string token;
                while (ss >> token) {
                    if (token == "NAME" && ss >> token) {
                        layerName = token;
                    } else if (token == "TYPE" && ss >> token) {
                        layerType = token;
                    }
                }
                props["description"] = layer.imageDescription;
            }
            
            props["width"] = std::to_string(layer.width);
            props["height"] = std::to_string(layer.height);
            props["resolution"] = std::to_string(layer.resolution);
            props["samples_per_pixel"] = std::to_string(layer.samplesPerPixel);
            
            raster.grid_layers_.emplace_back(layer.grid, layerName, layerType, props);
        }
        
        return raster;
    }

    void toFile(const std::filesystem::path& path) const {
        RasterCollection rc;
        rc.datum = datum_;
        rc.heading = heading_;
        rc.crs = crs_;
        rc.resolution = resolution_;
        
        for (const auto& gridLayer : grid_layers_) {
            Layer layer;
            layer.grid = gridLayer.grid;
            layer.width = static_cast<uint32_t>(gridLayer.grid.cols());
            layer.height = static_cast<uint32_t>(gridLayer.grid.rows());
            layer.resolution = resolution_;
            layer.datum = datum_;
            layer.heading = heading_;
            layer.crs = crs_;
            layer.samplesPerPixel = 1;
            layer.planarConfig = 1;
            
            std::string description = "NAME " + gridLayer.name + " TYPE " + gridLayer.type;
            layer.imageDescription = description;
            
            rc.layers.push_back(std::move(layer));
        }
        
        geotiv::WriteRasterCollection(rc, path);
    }

    size_t gridCount() const { return grid_layers_.size(); }
    bool hasGrids() const { return !grid_layers_.empty(); }
    void clearGrids() { grid_layers_.clear(); }

    const GridLayer& getGrid(size_t index) const {
        if (index >= grid_layers_.size()) {
            throw std::out_of_range("Grid index out of range");
        }
        return grid_layers_[index];
    }

    GridLayer& getGrid(size_t index) {
        if (index >= grid_layers_.size()) {
            throw std::out_of_range("Grid index out of range");
        }
        return grid_layers_[index];
    }

    const GridLayer& getGrid(const std::string& name) const {
        auto it = std::find_if(grid_layers_.begin(), grid_layers_.end(),
                              [&name](const GridLayer& layer) { return layer.name == name; });
        if (it == grid_layers_.end()) {
            throw std::runtime_error("Grid with name '" + name + "' not found");
        }
        return *it;
    }

    GridLayer& getGrid(const std::string& name) {
        auto it = std::find_if(grid_layers_.begin(), grid_layers_.end(),
                              [&name](const GridLayer& layer) { return layer.name == name; });
        if (it == grid_layers_.end()) {
            throw std::runtime_error("Grid with name '" + name + "' not found");
        }
        return *it;
    }

    void addGrid(uint32_t width, uint32_t height, const std::string& name, 
                const std::string& type = "", 
                const std::unordered_map<std::string, std::string>& properties = {}) {
        concord::Point p0;
        concord::Pose shift;
        
        if (crs_ == CRS::WGS) {
            concord::WGS w0{datum_.lat, datum_.lon, datum_.alt};
            concord::ENU enu0 = w0.toENU(datum_);
            p0 = concord::Point{enu0.x, enu0.y, enu0.z};
            shift = concord::Pose{p0, heading_};
        } else {
            p0 = concord::Point{0.0, 0.0, 0.0};
            shift = concord::Pose{p0, heading_};
        }

        concord::Grid<uint8_t> grid(height, width, resolution_, true, shift);
        auto props = properties;
        if (!type.empty()) {
            props["type"] = type;
        }
        grid_layers_.emplace_back(grid, name, type, props);
    }

    void removeGrid(size_t index) {
        if (index < grid_layers_.size()) {
            grid_layers_.erase(grid_layers_.begin() + index);
        }
    }

    void addTerrainGrid(uint32_t width, uint32_t height, const std::string& name = "terrain") {
        addGrid(width, height, name, "terrain");
    }

    void addOcclusionGrid(uint32_t width, uint32_t height, const std::string& name = "occlusion") {
        addGrid(width, height, name, "occlusion");
    }

    void addElevationGrid(uint32_t width, uint32_t height, const std::string& name = "elevation") {
        addGrid(width, height, name, "elevation");
    }

    std::vector<GridLayer> getGridsByType(const std::string& type) const {
        std::vector<GridLayer> result;
        for (const auto& layer : grid_layers_) {
            if (layer.type == type) {
                result.push_back(layer);
            }
        }
        return result;
    }

    std::vector<GridLayer> filterByProperty(const std::string& key, const std::string& value) const {
        std::vector<GridLayer> result;
        for (const auto& layer : grid_layers_) {
            auto it = layer.properties.find(key);
            if (it != layer.properties.end() && it->second == value) {
                result.push_back(layer);
            }
        }
        return result;
    }

    std::vector<std::string> getGridNames() const {
        std::vector<std::string> names;
        for (const auto& layer : grid_layers_) {
            names.push_back(layer.name);
        }
        return names;
    }

    const concord::Datum& getDatum() const { return datum_; }
    void setDatum(const concord::Datum& datum) { datum_ = datum; }

    const concord::Euler& getHeading() const { return heading_; }
    void setHeading(const concord::Euler& heading) { heading_ = heading; }

    CRS getCRS() const { return crs_; }
    void setCRS(CRS crs) { crs_ = crs; }

    double getResolution() const { return resolution_; }
    void setResolution(double resolution) { resolution_ = resolution; }

    auto begin() { return grid_layers_.begin(); }
    auto end() { return grid_layers_.end(); }
    auto begin() const { return grid_layers_.begin(); }
    auto end() const { return grid_layers_.end(); }
    auto cbegin() const { return grid_layers_.cbegin(); }
    auto cend() const { return grid_layers_.cend(); }
};

} // namespace geotiv