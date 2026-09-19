cmake_minimum_required(VERSION 3.16.0)

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cb2s_matter_plug)

# No managed_components/ patch step here, unlike the sibling
# esp32c6-radar-demo-matter project. That project's OccupancySensing cluster
# needed one because esp_matter keeps its setter private; ElectricalPowerMeasurement
# (delegate pattern) and ElectricalEnergyMeasurement (public NotifyCumulativeEnergyMeasured()/
# GetClusterInstance()) do not have that problem — verified by reading
# managed_components/espressif__esp_matter/components/esp_matter/data_model_provider/clusters/
# {electrical_power_measurement,electrical_energy_measurement}/integration.h once this
# project has been configured once. Do not port that patch machinery here.
