cmake_minimum_required(VERSION 3.20.0)

# === Paths ===
# Absolute CHIP checkout (upstream)
set(CHIP_ROOT "/Users/osterhult/Kod/Matter/connectedhomeip" CACHE PATH "")
get_filename_component(CHIP_ROOT "${CHIP_ROOT}" REALPATH)
message(STATUS "CHIP_ROOT = ${CHIP_ROOT}")

# Ensure we’re on a compatible NCS/Zephyr
include(${CHIP_ROOT}/config/nrfconnect/app/check-nrfconnect-version.cmake)

# Register CHIP as an extra Zephyr module (NO Nordic matter module)
list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/nrfconnect/chip-module)

# Use Nordic’s Zephyr (as installed by NCS). Do NOT override it.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(soil-sensor-app)

# === CHIP/NCS helpers ===
include(${CHIP_ROOT}/config/nrfconnect/app/check-sysbuild-use.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/enable-gnu-std.cmake)
include(${CHIP_ROOT}/config/nrfconnect/app/flashing.cmake)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

# Ensure all CHIP GN compile units pre-include our Zephyr compatibility shims
# so that Wi-Fi net_mgmt token churn is handled consistently.
get_filename_component(APP_INC_DIR ${CMAKE_CURRENT_LIST_DIR}/main/include REALPATH)
set(CHIP_CFLAGS "${CHIP_CFLAGS} -I${APP_INC_DIR} -include ${APP_INC_DIR}/matter_zephyr_compat.h")

# === Wi-Fi mgmt shim injection for Zephyr wifi_mgmt API mismatches ===
# CHIP's nrfconnect module expects generated includes under:
#   ${CMAKE_BINARY_DIR}/modules/connectedhomeip/gen/include
set(CHIP_GEN_INC_DIR ${CMAKE_BINARY_DIR}/modules/connectedhomeip/gen/include)
add_custom_command(
  OUTPUT ${CHIP_GEN_INC_DIR}/zephyr/net/wifi_mgmt.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CHIP_GEN_INC_DIR}/zephyr/net
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/shims/zephyr/net/wifi_mgmt.h
          ${CHIP_GEN_INC_DIR}/zephyr/net/wifi_mgmt.h
  COMMENT "Inject Wi-Fi mgmt shim into CHIP include path")
add_custom_target(wifi_shim ALL DEPENDS ${CHIP_GEN_INC_DIR}/zephyr/net/wifi_mgmt.h)

# Make sure the shim include dir is on the include path and built first
target_include_directories(app PRIVATE ${CHIP_GEN_INC_DIR})
add_dependencies(app wifi_shim)

# === ZAP / Data model generation ===
get_filename_component(SOILSENSOR_COMMON "${CMAKE_CURRENT_LIST_DIR}/../soil-sensor-common" REALPATH)
set(ZAP_FILE "${SOILSENSOR_COMMON}/soil-sensor-app.zap")
if(NOT EXISTS "${ZAP_FILE}")
  message(FATAL_ERROR "Missing ZAP file: ${ZAP_FILE}")
endif()

chip_configure_data_model(app
  ZAP_FILE "${ZAP_FILE}"
  ZAP_ARGS
    --zcl       "${CHIP_ROOT}/src/app/zap-templates/zcl/zcl-with-test-extensions.json"
    --templates "${CHIP_ROOT}/src/app/zap-templates/app-templates.json"
)

# -Wmaybe-uninitialized has too many false positives (same as Nordic examples)
# Be strict but avoid noisy false positives from some generated code
target_compile_options(app PRIVATE -Werror -Wno-error=maybe-uninitialized -Wno-error=array-bounds -Wno-error=stringop-overread -Wno-error=stringop-overflow)

# === Sources / includes ===
target_sources(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/main/main.cpp
  ${CMAKE_CURRENT_LIST_DIR}/main/SoilSensorManager.cpp
  ${CHIP_ROOT}/src/credentials/examples/ExampleDACs.cpp
  # Bring in example DeviceInfoProvider used by Nordic samples to print onboarding info
  ${CHIP_ROOT}/examples/providers/DeviceInfoProviderImpl.cpp
)

# App-local includes; ZAP includes are added by chip_configure_data_model()
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/main/include
  ${SOILSENSOR_COMMON}
  ${CHIP_ROOT}/examples/providers
)

# Also pre-include the compat header for non-GN app sources (e.g. attribute-storage.cpp)
target_compile_options(app PRIVATE -include ${APP_INC_DIR}/matter_zephyr_compat.h)

# If you keep a GN-built CHIP around, make the app wait on it (optional)
if(TARGET chip-gn)
  add_dependencies(app chip-gn)
endif()
