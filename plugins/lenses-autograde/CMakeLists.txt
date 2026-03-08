cmake_minimum_required(VERSION 3.28...3.30)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

add_library(lenses-autograde MODULE)
add_library(OBS::lenses-autograde ALIAS lenses-autograde)

target_sources(
  lenses-autograde
  PRIVATE
    lenses-autograde-module.cpp
    src/filter/autograde-filter.cpp
    src/frontend/frontend-controller.cpp
    src/frontend/region-selection-dialog.cpp
    src/runtime/deterministic-grader.cpp
    src/runtime/deterministic/analysis.cpp
    src/runtime/deterministic/params.cpp
    src/runtime/deterministic/lut.cpp
    src/runtime/analysis-runtime.cpp
)

target_include_directories(lenses-autograde PRIVATE include src)
target_compile_features(lenses-autograde PRIVATE cxx_std_20)

target_link_libraries(
  lenses-autograde
  PRIVATE
    OBS::frontend-api
    OBS::libobs
    Qt::Core
    Qt::Widgets
)

if(OS_WINDOWS)
  configure_file(${CMAKE_SOURCE_DIR}/cmake/windows/obs-module.rc.in lenses-autograde.rc)
  target_sources(lenses-autograde PRIVATE lenses-autograde.rc)
endif()

set_target_properties_obs(lenses-autograde PROPERTIES FOLDER plugins PREFIX "")

if(APPLE)
  add_custom_command(
    TARGET lenses-autograde
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/data"
      "$<TARGET_FILE_DIR:lenses-autograde>/../Resources"
  )
endif()
