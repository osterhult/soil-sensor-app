
cmake_minimum_required(VERSION 3.20.0)

get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../connectedhomeip REALPATH)
get_filename_component(NRFCONNECT_COMMON ${CHIP_ROOT}/examples/platform/nrfconnect REALPATH)
get_filename_component(SOILSENSOR_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../soil-sensor-common REALPATH)
#get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)
set(GEN_DIR ${CMAKE_BINARY_DIR}/gen/app-zapgen/zapgen/app-templates/zap-generated)

set(CHIP_ROOT ${CHIP_ROOT} CACHE STRING "Path to CHIP root directory")

# Where ZAP puts generated headers for THIS build
set(ZAP_GEN_DIR ${CMAKE_BINARY_DIR}/gen/app-zapgen/zapgen/app-templates)

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

target_compile_definitions(app PRIVATE MATTER_ATTRIBUTE_FLAG_SINGLETON=0x20)



# NCS exposes this variable; fall back to your absolute path if not set

if (NOT CHIP_ROOT)
  set(CHIP_ROOT "/Users/osterhult/Kod/Matter/connectedhomeip")
endif()

# CMakeLists.txt – keep this AFTER project()/find_package(Zephyr)
message(STATUS "CHIP_ROOT=${CHIP_ROOT}")

# Sanity-check: ensure ExampleDACs.cpp exists where we think it does
if (NOT EXISTS "${CHIP_ROOT}/src/credentials/examples/ExampleDACs.cpp")
  message(FATAL_ERROR "ExampleDACs.cpp not found at: ${CHIP_ROOT}/src/credentials/examples/ExampleDACs.cpp")
endif()

# Provide the expected DevelopmentCerts::* symbols and pull in ExampleDACs constants
target_sources(app PRIVATE
  ${CHIP_ROOT}/src/credentials/examples/ExampleDACs.cpp
  # main/DevCertsShim.cpp
)

target_sources(app PRIVATE
  main/main.cpp
  main/SoilSensormanager.cpp
  main/soil_measurement_nrf.cpp 
)


target_include_directories(app PRIVATE
  main/include
  # Our ZAP outputs (put these FIRST and use BEFORE)
  ${ZAP_GEN_DIR}
  ${ZAP_GEN_DIR}/zap-generated
  ${ZAP_GEN_DIR}/zap-generated/app-common

  # Common headers you already had
  ${SOILSENSOR_COMMON}

  # Avoid pulling in headers too broadly from CHIP unless necessary.
  # If you really need something from CHIP, keep it AFTER our generated dirs.
  ${CHIP_ROOT}/src
  
#   ${GEN_DIR}
#   ${GEN_DIR}/app-common
#   ${SOILSENSOR_COMMON}
# #  ${CMAKE_BINARY_DIR}/nrfconnect/gen/app-zapgen/zapgen/app-templates/zap-generated
#   ${CMAKE_BINARY_DIR}/nrfconnect/gen/app-zapgen/zapgen/app-templates/zap-generated/app-common
#   ${CHIP_ROOT}/src
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

# (optional) Show where we think the generated headers are
message(STATUS "ZAP_GEN_DIR = ${ZAP_GEN_DIR}")
