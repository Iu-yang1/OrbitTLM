cmake_minimum_required(VERSION 3.21)
project(OrbitTLM VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
include(CTest)
foreach(tree_file IN ITEMS ASRTU-1.json BY04.json)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees/${tree_file}")
    message(FATAL_ERROR "Official telemetry tree JSON is required: telemetry-trees/${tree_file}")
  endif()
endforeach()
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Sql Charts)
qt_standard_project_setup()

# The decoder is compiled into OrbitTLM. No external SSDV executable is
# required at run time. The files are fetched from immutable upstream commits
# into the build tree so normal builds do not modify the source checkout.
set(ORBITTLM_VENDOR_ROOT "${CMAKE_CURRENT_BINARY_DIR}/_deps/orbittlm_vendor")
set(ORBITTLM_SSDV_DIR "${ORBITTLM_VENDOR_ROOT}/third_party/ssdv_dslwp")
set(ORBITTLM_BY04_DIR "${ORBITTLM_VENDOR_ROOT}/protocols/by04")
file(MAKE_DIRECTORY "${ORBITTLM_SSDV_DIR}" "${ORBITTLM_BY04_DIR}")

function(orbittlm_download_pinned repository_path commit relative_path destination)
  if(NOT EXISTS "${destination}")
    file(DOWNLOAD
      "https://raw.githubusercontent.com/${repository_path}/${commit}/${relative_path}"
      "${destination}"
      STATUS download_status
      TLS_VERIFY ON)
    list(GET download_status 0 download_code)
    if(NOT download_code EQUAL 0)
      list(GET download_status 1 download_message)
      message(FATAL_ERROR "Unable to download pinned OrbitTLM source ${relative_path}: ${download_message}")
    endif()
    file(SIZE "${destination}" download_size)
    if(download_size LESS 64)
      message(FATAL_ERROR "Downloaded OrbitTLM source is unexpectedly small: ${relative_path}")
    endif()
  endif()
endfunction()

set(ORBITTLM_SSDV_COMMIT "68dd26280b4b7299def8a640d5c0f8122c12bd51")
foreach(source_file IN ITEMS ssdv.c ssdv.h rs8.c rs8.h)
  orbittlm_download_pinned(
    "BG7ZDQ/ASRTU_Series_Receiver" "${ORBITTLM_SSDV_COMMIT}"
    "third_party/ssdv_dslwp/${source_file}" "${ORBITTLM_SSDV_DIR}/${source_file}")
endforeach()
orbittlm_download_pinned(
  "BG7ZDQ/ASRTU_Series_Receiver" "${ORBITTLM_SSDV_COMMIT}"
  "third_party/ssdv_dslwp/COPYING" "${ORBITTLM_SSDV_DIR}/COPYING")

set(ORBITTLM_BY04_COMMIT "fe684b0b2124552efa2fc53a1e37dd82af961290")
foreach(catalog_file IN ITEMS OBC-SF.json BUV-S0.json)
  orbittlm_download_pinned(
    "Iu-yang1/OrbitTLM" "${ORBITTLM_BY04_COMMIT}"
    "protocols/by04/${catalog_file}" "${ORBITTLM_BY04_DIR}/${catalog_file}")
endforeach()

add_library(orbittlm_core STATIC src/core.cpp src/core.h src/schema.cpp src/schema.h src/ssdv.cpp src/ssdv.h src/transport.cpp src/transport.h src/business.cpp src/business.h src/by04.cpp src/by04.h
  src/telemetry_store.cpp src/telemetry_store.h
)
qt_add_resources(orbittlm_core official_telemetry_tree
  PREFIX "/telemetry-trees"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees"
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees/ASRTU-1.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees/BY04.json"
)

target_include_directories(orbittlm_core PUBLIC src)
target_link_libraries(orbittlm_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
target_sources(orbittlm_core PRIVATE "${ORBITTLM_SSDV_DIR}/ssdv.c" "${ORBITTLM_SSDV_DIR}/rs8.c")
target_include_directories(orbittlm_core PRIVATE "${ORBITTLM_VENDOR_ROOT}")
qt_add_resources(orbittlm_core by04_catalog
  PREFIX "/by04" BASE "${ORBITTLM_BY04_DIR}"
  FILES "${ORBITTLM_BY04_DIR}/OBC-SF.json" "${ORBITTLM_BY04_DIR}/BUV-S0.json")
add_executable(OrbitTLM-cli src/cli.cpp)
target_link_libraries(OrbitTLM-cli PRIVATE orbittlm_core)
find_file(ORBITTLM_QTBASE_JA qtbase_ja.qm
  HINTS "${Qt6_DIR}/../../../translations" "/usr/share/qt6/translations"
  REQUIRED)
set_source_files_properties("${ORBITTLM_QTBASE_JA}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_ja.qm")
add_library(orbittlm_ui STATIC src/mainwindow.h src/mainwindow_widgets.h src/mainwindow_build.cpp src/mainwindow_data.cpp src/i18n.h src/i18n.cpp)
# Embed every locale dictionary into the UI binary; runtime language switching reads :/i18n resources.
qt_add_resources(orbittlm_ui orbittlm_i18n
  PREFIX "/i18n"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/i18n"
  FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/zh-CN.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/en-US.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/i18n/ja-JP.json"
)
qt_add_resources(orbittlm_ui orbittlm_qt_i18n
  PREFIX "/qt-i18n" FILES "${ORBITTLM_QTBASE_JA}")
target_include_directories(orbittlm_ui PUBLIC src)
target_link_libraries(orbittlm_ui PUBLIC orbittlm_core Qt6::Widgets Qt6::Charts)
add_executable(OrbitTLM src/main.cpp)
target_link_libraries(OrbitTLM PRIVATE orbittlm_ui)
target_compile_definitions(OrbitTLM PRIVATE ORBITTLM_VERSION="${PROJECT_VERSION}")
set_target_properties(OrbitTLM PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(orbit_tests tests/protocol.cpp)
  target_link_libraries(orbit_tests PRIVATE orbittlm_core Qt6::Test)
  add_test(NAME protocol COMMAND orbit_tests)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME i18n COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_i18n.py")
    add_test(NAME cli_help COMMAND OrbitTLM-cli --help)
    add_executable(orbit_ui_tests tests/ui_smoke.cpp)
    target_link_libraries(orbit_ui_tests PRIVATE orbittlm_ui Qt6::Test)
    add_test(NAME ui_smoke COMMAND orbit_ui_tests)
  add_executable(orbit_store_tests tests/telemetry_store.cpp)
    target_link_libraries(orbit_store_tests PRIVATE orbittlm_core Qt6::Test)
    add_test(NAME telemetry_store COMMAND orbit_store_tests)
    if(UNIX AND NOT APPLE)
      set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
      add_test(NAME gui_startup COMMAND OrbitTLM --smoke-test)
      set_tests_properties(gui_startup PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()
endif()
install(TARGETS OrbitTLM OrbitTLM-cli RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
  install(FILES packaging/linux/OrbitTLM.desktop DESTINATION share/applications)
  install(FILES packaging/linux/orbittlm.svg DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES LICENSE DESTINATION share/licenses/OrbitTLM)
endif()

# Keep offline telemetry catalog and the standalone HTML history template available next to the application.
foreach(target OrbitTLM OrbitTLM-cli orbit_tests)
  if(TARGET ${target})
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/protocols/asrtu1"
        "$<TARGET_FILE_DIR:${target}>/protocols/asrtu1"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees"
        "$<TARGET_FILE_DIR:${target}>/telemetry-trees"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/web"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/web/index.template.html"
        "$<TARGET_FILE_DIR:${target}>/web/index.template.html"
      VERBATIM)
  endif()
endforeach()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/protocols/asrtu1"
        DESTINATION "bin/protocols")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/telemetry-trees"
        DESTINATION "bin")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/web/index.template.html"
        DESTINATION "bin/web")
install(FILES "${ORBITTLM_SSDV_DIR}/ssdv.c" "${ORBITTLM_SSDV_DIR}/ssdv.h"
              "${ORBITTLM_SSDV_DIR}/rs8.c" "${ORBITTLM_SSDV_DIR}/rs8.h" "${ORBITTLM_SSDV_DIR}/COPYING"
        DESTINATION "share/OrbitTLM/third_party/ssdv_dslwp")

# Public BY04 VC1 DSLWP capture fixture used for decoder regression.
if(TARGET orbit_tests)
  add_custom_command(TARGET orbit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:orbit_tests>/fixtures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/by04_ssdv_bad_packet.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/by04_ssdv_bad_packet.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jamx_satnogs_14907288.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/jamx_satnogs_14907288.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jamx_capture_20261031_id2_218.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/jamx_capture_20261031_id2_218.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/jamx_capture_20261031_frames223.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/jamx_capture_20261031_frames223.b64"
    VERBATIM)
endif()
