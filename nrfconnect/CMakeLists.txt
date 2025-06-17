cmake_minimum_required(VERSION 3.20.0)

get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../connectedhomeip REALPATH)
get_filename_component(NRFCONNECT_COMMON ${CHIP_ROOT}/examples/platform/nrfconnect REALPATH)
#get_filename_component(SOILSENSOR_COMMON ${CHIP_ROOT}/examples/soil-sensor-app/soil-sensor-common REALPATH)
get_filename_component(SOILSENSOR_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../soil-sensor-common REALPATH)
get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)


set(CHIP_ROOT ${CHIP_ROOT} CACHE STRING "Path to CHIP root directory")

# # --- ZAP generator paths -----------------------------------------------
set(ENV{ZCL_PROPERTIES}    "${CHIP_ROOT}/src/app/zap-templates/zcl/zcl.json")
set(ENV{ZAP_TEMPLATE_PATH} "${CHIP_ROOT}/src/app/zap-templates")
# # -----------------------------------------------------------------------


include(${CHIP_ROOT}/config/nrfconnect/app/check-nrfconnect-version.cmake)

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/nrfconnect/chip-module)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(soil-sensor-app)

include(${CHIP_ROOT}/config/nrfconnect/app/check-sysbuild-use.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/flashing.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_sources(app PRIVATE
  main/main.cpp
  main/SoilSensorManager.cpp
)

target_include_directories(app PRIVATE
  main/include
  ${GEN_DIR}
  ${GEN_DIR}/app-common
  ${SOILSENSOR_COMMON}
)

set(PM_STATIC_YML ${CMAKE_CURRENT_SOURCE_DIR}/pm_static.yml)



#  === 
set(CHIP_CERT_EXECUTABLE "${CHIP_ROOT}/out/host/chip-cert")
set(GEN_CERTS TRUE) # eller 1


set(ZCL_PATH ${CHIP_ROOT}/src/app/zap-templates/zcl/zcl.json)

chip_configure_data_model(app
  ZAP_FILE  ${SOILSENSOR_COMMON}/soil-sensor-app.zap
  # Anything after ZAP_ARGS is forwarded verbatim to generate.py
  ZAP_ARGS  --zcl ${ZCL_JSON}
)












# chip_configure_data_model(app
#   ZAP_FILE ${SOILSENSOR_COMMON}/soil-sensor-app.zap
# )

# chip_configure_data_model(app
#   ZAP_FILE ${SOILSENSOR_COMMON}/soil-sensor-app.zap
#   CHIP_CERT_PATH ${CHIP_CERT_EXECUTABLE}
#   GEN_CERTS ${GEN_CERTS}
# )

# set(FACTORY_GEN_OPTS "--chip_cert_path=${CHIP_ROOT}/src/tools/chip-cert/out/chip-cert --gen_certs")
# set(FACTORY_GEN_OPTS "--chip_cert_path=${CHIP_ROOT}/out/host/chip-cert --gen_certs")

# get_target_property(factory_gen_cmd app CHIP_FACTORY_DATA_GEN_COMMAND)
# if(NOT "${factory_gen_cmd}" STREQUAL "factory_gen_cmd-NOTFOUND")
#     string(REPLACE "generate_nrfconnect_chip_factory_data.py" 
#         "generate_nrfconnect_chip_factory_data.py ${FACTORY_GEN_OPTS}" 
#         factory_gen_cmd "${factory_gen_cmd}")
#     set_target_properties(app PROPERTIES CHIP_FACTORY_DATA_GEN_COMMAND "${factory_gen_cmd}")
# endif()