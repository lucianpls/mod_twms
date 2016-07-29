# mod_twms

# WORK IN PROGRESS
# LIMITED FUNCTIONALITY

An apache httpd module converting tiledWMS requests to the REST M/L/R/C encoding

Implements two apache configuration directives:
**tWMS_RegExp string**
Can be present more than once, one of the existing regular expressions has to match the request URL for the request to be considered

**tWMS_ConfigurationFile configuration_file**
Raster and tiled WMS specific directives
