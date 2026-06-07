cmake_minimum_required(VERSION 3.16)

# Point at the shared components so the test firmware can use imb_* types if needed
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/../../../components")

# Exclude BLE components — they require NimBLE which is not enabled in this test firmware
set(EXCLUDE_COMPONENTS "imb_ble" "imb_ble_session")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(imb_integration_tests)
