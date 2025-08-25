
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../connectedhomeip REALPATH)
get_filename_component(NRFCONNECT_COMMON ${CHIP_ROOT}/examples/platform/nrfconnect REALPATH)
get_filename_component(SOILSENSOR_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../soil-sensor-common REALPATH)
#get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen/app-zapgen/zapgen/app-templates/zap-generated)

set(CHIP_ROOT ${CHIP_ROOT} CACHE STRING "Path to CHIP root directory")

# --- ZAP generator paths -----------------------------------------------
#set(ENV{ZCL_PROPERTIES}    "${CHIP_ROOT}/src/app/zap-templates/zcl/zcl.json")
set(ENV{ZCL_PROPERTIES}    "${CHIP_ROOT}/src/app/zap-templates/zcl/zcl-with-test-extensions.json")
set(ENV{ZAP_TEMPLATE_PATH} "${CHIP_ROOT}/src/app/zap-templates")
# -----------------------------------------------------------------------

# Include Matter build system
include(${CHIP_ROOT}/config/nrfconnect/app/check-nrfconnect-version.cmake)

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/nrfconnect/chip-module)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

# Project name
project(soil-sensor-app)


include(${CHIP_ROOT}/config/nrfconnect/app/check-sysbuild-use.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/flashing.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)


target_sources(app PRIVATE
    main/main.cpp
    main/SoilSensormanager.cpp
    main/soil_measurement_nrf.cpp
)


target_include_directories(app PRIVATE
  main/include
  ${GEN_DIR}
  ${GEN_DIR}/app-common
  ${SOILSENSOR_COMMON}
#  ${CMAKE_BINARY_DIR}/nrfconnect/gen/app-zapgen/zapgen/app-templates/zap-generated
  ${CMAKE_BINARY_DIR}/nrfconnect/gen/app-zapgen/zapgen/app-templates/zap-generated/app-common
  ${CHIP_ROOT}/src
)

#set(PM_STATIC_YML ${CMAKE_CURRENT_SOURCE_DIR}/pm_static_nrf7002dk_nrf5340_cpuapp_dfu.yml)
set(PM_STATIC_YML ${CMAKE_CURRENT_SOURCE_DIR}/pm_static.yml)



#  === 
set(CHIP_CERT_EXECUTABLE "${CHIP_ROOT}/out/host/chip-cert")
set(GEN_CERTS TRUE) # eller 1


#set(ZCL_PATH ${CHIP_ROOT}/src/app/zap-templates/zcl/zcl.json)
set(ZCL_JSON ${CHIP_ROOT}/src/app/zap-templates/zcl/zcl-with-test-extensions.json)

chip_configure_data_model(app
  ZAP_FILE  ${SOILSENSOR_COMMON}/soil-sensor-app.zap
  ZAP_ARGS
    --zcl ${CHIP_ROOT}/src/app/zap-templates/zcl/zcl-with-test-extensions.json
    --templates ${CHIP_ROOT}/src/app/zap-templates/app-templates.json
    --gen-only
)

