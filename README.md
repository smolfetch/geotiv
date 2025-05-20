
<img align="right" width="26%" src="./misc/logo.png">

Geotiv
===

A C++ library for working with geotiff files.


This library takes advantage of the [GeoTIFF Specification](http://geotiff.maptools.org/spec/geotiff6.html) to provide a simple interface for reading and writing geotiff files.

At the core, this converts any grotiff file into a Concord grid with Pigment information, basicaly a grid of points with a color value.

### Dependencies

This library depends on [Concord](https://github.com/smolfetch/concord) and [Pigment](https://github.com/smolfetch/pigment).
