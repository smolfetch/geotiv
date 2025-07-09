#include "concord/concord.hpp"
#include "geotiv/geotiv.hpp"
#include <doctest/doctest.h>

TEST_CASE("Layer structure tests") {
    SUBCASE("Default Layer construction") {
        geotiv::Layer layer;
        CHECK(layer.ifdOffset == 0);
        CHECK(layer.width == 0);
        CHECK(layer.height == 0);
        CHECK(layer.samplesPerPixel == 0);
        CHECK(layer.planarConfig == 0);
        CHECK(layer.stripOffsets.empty());
        CHECK(layer.stripByteCounts.empty());
    }

    SUBCASE("Layer with dimensions") {
        geotiv::Layer layer;
        layer.width = 100;
        layer.height = 50;
        layer.samplesPerPixel = 1;
        layer.planarConfig = 1;

        CHECK(layer.width == 100);
        CHECK(layer.height == 50);
        CHECK(layer.samplesPerPixel == 1);
        CHECK(layer.planarConfig == 1);
    }
}

TEST_CASE("RasterCollection structure tests") {
    SUBCASE("Default RasterCollection construction") {
        geotiv::RasterCollection rc;
        CHECK(rc.layers.empty());
        // CRS is always WGS84 by default
        CHECK(rc.datum.lat == 0.0);
        CHECK(rc.datum.lon == 0.0);
        CHECK(rc.datum.alt == 0.0);
        CHECK(rc.heading.roll == 0.0);
        CHECK(rc.heading.pitch == 0.0);
        CHECK(rc.heading.yaw == 0.0);
    }

    SUBCASE("RasterCollection with custom values") {
        geotiv::RasterCollection rc;
        // CRS is always WGS84
        rc.datum = concord::Datum{48.0, 11.0, 500.0};
        rc.heading = concord::Euler{0, 0, 45};
        rc.resolution = 2.0;

        // CRS is always WGS84
        CHECK(rc.datum.lat == 48.0);
        CHECK(rc.datum.lon == 11.0);
        CHECK(rc.datum.alt == 500.0);
        CHECK(rc.heading.yaw == 45.0);
        CHECK(rc.resolution == 2.0);
    }

    SUBCASE("RasterCollection with layers") {
        geotiv::RasterCollection rc;

        geotiv::Layer layer1, layer2;
        layer1.width = 100;
        layer1.height = 50;
        layer2.width = 200;
        layer2.height = 100;

        rc.layers.push_back(layer1);
        rc.layers.push_back(layer2);

        CHECK(rc.layers.size() == 2);
        CHECK(rc.layers[0].width == 100);
        CHECK(rc.layers[0].height == 50);
        CHECK(rc.layers[1].width == 200);
        CHECK(rc.layers[1].height == 100);
    }
}
