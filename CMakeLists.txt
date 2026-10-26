cmake_minimum_required(VERSION 3.21)
project(OrbitTLM VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
include(CTest)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Sql Charts)
qt_standard_project_setup()
add_library(orbittlm_core STATIC src/core.cpp src/core.h src/schema.cpp src/schema.h src/ssdv.cpp src/ssdv.h src/transport.cpp src/transport.h src/business.cpp src/business.h src/by04.cpp src/by04.h)
target_include_directories(orbittlm_core PUBLIC src)
target_link_libraries(orbittlm_core PUBLIC Qt6::Core Qt6::Sql Qt6::Network)
qt_add_resources(orbittlm_core by04_catalog
  PREFIX "/by04" BASE "protocols/by04"
  FILES protocols/by04/OBC-SF.json protocols/by04/BUV-S0.json)
add_executable(OrbitTLM-cli src/cli.cpp)
target_link_libraries(OrbitTLM-cli PRIVATE orbittlm_core)
add_library(orbittlm_ui STATIC src/mainwindow.h src/mainwindow_widgets.h src/mainwindow_build.cpp src/mainwindow_data.cpp)
target_include_directories(orbittlm_ui PUBLIC src)
target_link_libraries(orbittlm_ui PUBLIC orbittlm_core Qt6::Widgets Qt6::Charts)
add_executable(OrbitTLM src/main.cpp)
target_link_libraries(OrbitTLM PRIVATE orbittlm_ui)
set_target_properties(OrbitTLM PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(orbit_tests tests/protocol.cpp)
  target_link_libraries(orbit_tests PRIVATE orbittlm_core Qt6::Test)
  add_test(NAME protocol COMMAND orbit_tests)
    add_test(NAME cli_help COMMAND OrbitTLM-cli --help)
    add_executable(orbit_ui_tests tests/ui_smoke.cpp)
    target_link_libraries(orbit_ui_tests PRIVATE orbittlm_ui Qt6::Test)
    add_test(NAME ui_smoke COMMAND orbit_ui_tests)
    if(UNIX AND NOT APPLE)
      set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
install(TARGETS OrbitTLM OrbitTLM-cli RUNTIME DESTINATION bin BUNDLE DESTINATION .)

# Keep all 27 mission field catalogs available offline, including Windows CI artifacts.
foreach(target OrbitTLM OrbitTLM-cli orbit_tests)
  if(TARGET ${target})
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/protocols/asrtu1"
        "$<TARGET_FILE_DIR:${target}>/protocols/asrtu1"
      VERBATIM)
  endif()
endforeach()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/protocols/asrtu1"
        DESTINATION "bin/protocols")

# Public BY04 VC1 DSLWP capture fixture used for decoder regression.
if(TARGET orbit_tests)
  add_custom_command(TARGET orbit_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:orbit_tests>/fixtures"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/by04_ssdv_bad_packet.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/by04_ssdv_bad_packet.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/by04_business_capture.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/by04_business_capture.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/asrtu_capture_20261024.b64"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/asrtu_capture_20261024.b64"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/asrtu_capture_20261024_fec.txt"
      "$<TARGET_FILE_DIR:orbit_tests>/fixtures/asrtu_capture_20261024_fec.txt"
    VERBATIM)
endif()
