cmake_minimum_required(VERSION 3.28...3.30)

option(LENSES_ENABLE_AI "Enable Lenses AI runtime integration" ON)
option(LENSES_ENABLE_ORT "Enable ONNX Runtime backend for Lenses AI" ON)
option(LENSES_ENABLE_CLOUD "Enable cloud backend adapters for Lenses AI" OFF)
set(LENSES_OPENCV_ROOT "" CACHE PATH "Optional OpenCV root used to resolve OpenCVConfig.cmake")
set(LENSES_OPENCV_VERSION "4.13.0" CACHE STRING "Pinned OpenCV version required by lenses")
set(LENSES_ONNXRUNTIME_ROOT "" CACHE PATH "Optional ONNX Runtime root (manual fallback when CMake package is unavailable)")
set(LENSES_BUNDLED_ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps_vendor/onnxruntime"
    CACHE PATH "Bundled ONNX Runtime root used when LENSES_ONNXRUNTIME_ROOT is not provided")

add_library(lenses MODULE)
add_library(OBS::lenses ALIAS lenses)

if(NOT LENSES_OPENCV_ROOT AND APPLE)
  if(EXISTS "/opt/homebrew/opt/opencv/lib/cmake/opencv4/OpenCVConfig.cmake")
    set(LENSES_OPENCV_ROOT "/opt/homebrew/opt/opencv")
  elseif(EXISTS "/usr/local/opt/opencv/lib/cmake/opencv4/OpenCVConfig.cmake")
    set(LENSES_OPENCV_ROOT "/usr/local/opt/opencv")
  endif()
endif()

if(LENSES_OPENCV_ROOT)
  set(OpenCV_DIR "${LENSES_OPENCV_ROOT}/lib/cmake/opencv4")
  message(STATUS "Lenses resolving OpenCV from root: ${LENSES_OPENCV_ROOT}")
endif()

find_package(OpenCV ${LENSES_OPENCV_VERSION} EXACT CONFIG REQUIRED COMPONENTS opencv_core opencv_imgproc)
find_package(Qt6 REQUIRED Core Widgets)

target_sources(
  lenses
  PRIVATE
    src/filter/lenses-filter.c
    src/filter/host/lenses-filter-ai-lane.c
    src/filter/host/lenses-filter-ai-submit.c
    src/filter/host/lenses-filter-hue-presets.c
    src/filter/host/lenses-filter-hue-qualifier.c
    src/filter/host/lenses-filter-invert-component-mask.cpp
    src/filter/host/lenses-filter-invert-region.c
    src/filter/host/lenses-filter-mask-shape.cpp
    src/filter/host/lenses-filter-pass-setup.c
    src/filter/host/lenses-filter-policy-graph.c
    src/filter/host/lenses-filter-properties.c
    src/filter/host/lenses-filter-render-path.c
    src/filter/host/lenses-filter-runtime-config.c
    src/filter/host/lenses-filter-runtime-diagnostics.c
    src/filter/host/lenses-filter-sync-mask.c
    src/filter/ui/lenses-hue-qualifier-editor.cpp
    src/filter/invert/lenses-hue-qualifier.cpp
    src/filter/invert/lenses-invert-opencv-pipeline.cpp
    src/io/lenses-policy.c
    src/io/lenses-model-catalog.c
    src/io/lenses-model-selection.c
    lenses-module.c
    src/pipeline/lenses-pass.c
    src/core/core-bridge.cpp
    src/core/noop-compositor.cpp
    src/core/noop-mask-generator.cpp
    src/core/registry.cpp
    src/core/rule-compiler.cpp
    src/ai/decode/segmentation-decoder.cpp
    src/ai/tracking/bytetrack-tracker.cpp
    src/ai/cloud/cloud-mask-generator.cpp
    src/ai/runtime/backend-resolver.cpp
    src/ai/runtime/benchmark-runner.cpp
    src/ai/runtime/mask-generator-factory.cpp
    src/ai/runtime/model-registry.cpp
    src/ai/ort/onnx-mask-generator-artifacts.cpp
    src/ai/ort/onnx-mask-generator-bootstrap.cpp
    src/ai/ort/onnx-mask-generator-iobinding.cpp
    src/ai/ort/onnx-mask-generator-layout.cpp
    src/ai/ort/onnx-mask-generator-lifecycle.cpp
    src/ai/ort/onnx-mask-generator-provider.cpp
    src/ai/ort/onnx-mask-generator-runner.cpp
    src/ai/ort/onnx-mask-generator-session.cpp
    src/ai/ort/onnx-mask-generator-session-io.cpp
    src/ai/ort/onnx-mask-generator-worker.cpp
    src/ai/ort/onnx-mask-generator.cpp
    src/ai/ort/onnx-mask-generator-preprocess.cpp
)

target_include_directories(lenses PRIVATE include src)
target_compile_features(lenses PRIVATE c_std_17 cxx_std_20)

if(LENSES_ENABLE_AI)
  target_compile_definitions(lenses PRIVATE LENSES_ENABLE_AI=1)
endif()

if(LENSES_ENABLE_ORT)
  if(NOT LENSES_ONNXRUNTIME_ROOT AND EXISTS "${LENSES_BUNDLED_ONNXRUNTIME_ROOT}/include/onnxruntime/onnxruntime_cxx_api.h")
    set(LENSES_ONNXRUNTIME_ROOT "${LENSES_BUNDLED_ONNXRUNTIME_ROOT}")
    message(STATUS "Lenses using bundled ONNX Runtime root: ${LENSES_ONNXRUNTIME_ROOT}")
  endif()

  if(LENSES_ONNXRUNTIME_ROOT)
    message(STATUS "Lenses resolving ONNX Runtime from explicit root: ${LENSES_ONNXRUNTIME_ROOT}")
    unset(LENSES_ONNXRUNTIME_INCLUDE_DIR CACHE)
    unset(LENSES_ONNXRUNTIME_LIBRARY CACHE)
    find_path(LENSES_ONNXRUNTIME_INCLUDE_DIR
      NAMES onnxruntime_cxx_api.h
      HINTS ${LENSES_ONNXRUNTIME_ROOT}
      NO_DEFAULT_PATH
      PATH_SUFFIXES
        include
        Include
        include/onnxruntime
        Include/onnxruntime
        Source/ThirdParty/Include/onnxruntime
    )

    find_library(LENSES_ONNXRUNTIME_LIBRARY
      NAMES onnxruntime libonnxruntime
      HINTS ${LENSES_ONNXRUNTIME_ROOT}
      NO_DEFAULT_PATH
      PATH_SUFFIXES
        lib
        Lib
        lib64
        Lib/Mac/onnxruntime
        Source/ThirdParty/Lib/Mac/onnxruntime
    )

    if(LENSES_ONNXRUNTIME_INCLUDE_DIR AND LENSES_ONNXRUNTIME_LIBRARY)
      add_library(onnxruntime SHARED IMPORTED)
      set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${LENSES_ONNXRUNTIME_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LENSES_ONNXRUNTIME_INCLUDE_DIR}"
      )
      set(onnxruntime_FOUND TRUE)
    else()
      set(onnxruntime_FOUND FALSE)
      message(WARNING "Lenses could not resolve ONNX Runtime from LENSES_ONNXRUNTIME_ROOT='${LENSES_ONNXRUNTIME_ROOT}'.")
    endif()
  else()
    find_package(onnxruntime CONFIG QUIET)
  endif()

  if(onnxruntime_FOUND)
    target_compile_definitions(lenses PRIVATE LENSES_ENABLE_ORT=1)
    target_link_libraries(lenses PRIVATE onnxruntime)
    if(APPLE)
      target_link_options(lenses PRIVATE "SHELL:-Wl,-rpath,@loader_path/../Frameworks")

      add_custom_command(TARGET lenses POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lenses>/../Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:onnxruntime>"
          "$<TARGET_FILE_DIR:lenses>/../Frameworks/"
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix-onnxruntime-soname.sh"
          "$<TARGET_FILE_DIR:lenses>/../Frameworks"
          "$<TARGET_FILE_NAME:onnxruntime>"
      )
    endif()
  else()
    message(WARNING "LENSES_ENABLE_ORT is ON but onnxruntime was not found; building fallback backend only. Set LENSES_ONNXRUNTIME_ROOT to a local ORT installation.")
  endif()
endif()

if(LENSES_ENABLE_CLOUD)
  target_compile_definitions(lenses PRIVATE LENSES_ENABLE_CLOUD=1)
endif()

target_link_libraries(
  lenses
  PRIVATE
    OBS::frontend-api
    OBS::libobs
    Qt::Core
    Qt::Widgets
    $<$<PLATFORM_ID:Windows>:OBS::w32-pthreads>
)

if(TARGET opencv_core AND TARGET opencv_imgproc)
  set(LENSES_OPENCV_CORE_TARGET opencv_core)
  set(LENSES_OPENCV_IMGPROC_TARGET opencv_imgproc)
elseif(TARGET OpenCV::opencv_core AND TARGET OpenCV::opencv_imgproc)
  set(LENSES_OPENCV_CORE_TARGET OpenCV::opencv_core)
  set(LENSES_OPENCV_IMGPROC_TARGET OpenCV::opencv_imgproc)
else()
  message(FATAL_ERROR "OpenCV package was found but required imported targets are missing")
endif()

target_link_libraries(lenses PRIVATE ${LENSES_OPENCV_CORE_TARGET} ${LENSES_OPENCV_IMGPROC_TARGET})

if(APPLE)
  # Force framework linker flag to avoid generator-specific dropping of framework symbols.
  target_link_options(lenses PRIVATE "SHELL:-framework Accelerate")
  # Bundle OpenCV dylibs into the plugin frameworks folder and rewrite install names
  # so the plugin is self-contained instead of depending on external Homebrew paths.
  add_custom_command(TARGET lenses POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lenses>/../Frameworks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${LENSES_OPENCV_CORE_TARGET}>"
      "$<TARGET_FILE_DIR:lenses>/../Frameworks/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${LENSES_OPENCV_IMGPROC_TARGET}>"
      "$<TARGET_FILE_DIR:lenses>/../Frameworks/"
    COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix-opencv-rpath.sh"
      "$<TARGET_FILE_DIR:lenses>/../Frameworks"
      "$<TARGET_FILE:lenses>"
      "$<TARGET_FILE:${LENSES_OPENCV_CORE_TARGET}>"
      "$<TARGET_FILE:${LENSES_OPENCV_IMGPROC_TARGET}>"
  )
endif()

if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in lenses.rc)
  target_sources(lenses PRIVATE lenses.rc)
endif()

set_target_properties_obs(lenses PROPERTIES FOLDER plugins PREFIX "")

if(APPLE)
  # Keep bundled data hot-reload friendly in existing build trees: copy full plugin data
  # every build so newly added models/locales/effects are always present without requiring
  # a manual CMake reconfigure.
  add_custom_command(TARGET lenses POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/data"
      "$<TARGET_FILE_DIR:lenses>/../Resources"
  )
endif()

if(BUILD_TESTS OR ENABLE_UNIT_TESTS)
  add_executable(lenses-segmentation-decode-test)
  target_sources(lenses-segmentation-decode-test PRIVATE test/segmentation-decoder-test.cpp
                                                         src/ai/decode/segmentation-decoder.cpp)
  target_include_directories(lenses-segmentation-decode-test PRIVATE include)
  target_compile_features(lenses-segmentation-decode-test PRIVATE cxx_std_20)
  add_test(NAME lenses-segmentation-decode-test COMMAND lenses-segmentation-decode-test)
  set_target_properties(lenses-segmentation-decode-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-bytetrack-tracker-test)
  target_sources(lenses-bytetrack-tracker-test PRIVATE test/bytetrack-tracker-test.cpp
                                                       src/ai/tracking/bytetrack-tracker.cpp)
  target_include_directories(lenses-bytetrack-tracker-test PRIVATE include)
  target_compile_features(lenses-bytetrack-tracker-test PRIVATE cxx_std_20)
  add_test(NAME lenses-bytetrack-tracker-test COMMAND lenses-bytetrack-tracker-test)
  set_target_properties(lenses-bytetrack-tracker-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-runtime-soak-test)
  target_sources(lenses-runtime-soak-test PRIVATE test/lenses-runtime-soak-test.cpp
                                                  src/core/noop-mask-generator.cpp
                                                  src/ai/tracking/bytetrack-tracker.cpp)
  target_include_directories(lenses-runtime-soak-test PRIVATE include)
  target_compile_features(lenses-runtime-soak-test PRIVATE cxx_std_20)
  add_test(NAME lenses-runtime-soak-test COMMAND lenses-runtime-soak-test)
  set_target_properties(lenses-runtime-soak-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-policy-rule-decision-test)
  target_sources(lenses-policy-rule-decision-test PRIVATE test/lenses-policy-rule-decision-test.cpp)
  target_include_directories(lenses-policy-rule-decision-test PRIVATE include src)
  target_compile_features(lenses-policy-rule-decision-test PRIVATE cxx_std_20)
  add_test(NAME lenses-policy-rule-decision-test COMMAND lenses-policy-rule-decision-test)
  set_target_properties(lenses-policy-rule-decision-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-model-selection-test)
  target_sources(lenses-model-selection-test PRIVATE test/lenses-model-selection-test.cpp
                                                    src/io/lenses-model-selection.c)
  target_include_directories(lenses-model-selection-test PRIVATE include)
  target_compile_features(lenses-model-selection-test PRIVATE cxx_std_20)
  add_test(NAME lenses-model-selection-test COMMAND lenses-model-selection-test)
  set_target_properties(lenses-model-selection-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-preprocess-parity-test)
  target_sources(lenses-preprocess-parity-test PRIVATE test/lenses-preprocess-parity-test.cpp
                                                       src/ai/ort/onnx-mask-generator-preprocess.cpp)
  target_include_directories(lenses-preprocess-parity-test PRIVATE include src)
  target_compile_features(lenses-preprocess-parity-test PRIVATE cxx_std_20)
  if(APPLE)
    target_link_libraries(lenses-preprocess-parity-test PRIVATE "-framework Accelerate")
  endif()
  add_test(NAME lenses-preprocess-parity-test COMMAND lenses-preprocess-parity-test)
  set_target_properties(lenses-preprocess-parity-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-invert-opencv-pipeline-test)
  target_sources(
    lenses-invert-opencv-pipeline-test
    PRIVATE
      test/invert-opencv-pipeline-test.cpp
      src/filter/invert/lenses-hue-qualifier.cpp
      src/filter/invert/lenses-invert-opencv-pipeline.cpp)
  target_include_directories(lenses-invert-opencv-pipeline-test PRIVATE include src)
  target_compile_features(lenses-invert-opencv-pipeline-test PRIVATE cxx_std_20)
  if(APPLE)
    target_link_options(lenses-invert-opencv-pipeline-test PRIVATE "SHELL:-Wl,-rpath,@executable_path")
    set_target_properties(lenses-invert-opencv-pipeline-test PROPERTIES
      XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
      XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME "NO")
  endif()
  target_link_libraries(lenses-invert-opencv-pipeline-test PRIVATE ${LENSES_OPENCV_CORE_TARGET} ${LENSES_OPENCV_IMGPROC_TARGET})
  if(APPLE)
    add_custom_command(TARGET lenses-invert-opencv-pipeline-test POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${LENSES_OPENCV_CORE_TARGET}>"
        "$<TARGET_FILE_DIR:lenses-invert-opencv-pipeline-test>/"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${LENSES_OPENCV_IMGPROC_TARGET}>"
        "$<TARGET_FILE_DIR:lenses-invert-opencv-pipeline-test>/"
      COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix-opencv-rpath.sh"
        "$<TARGET_FILE_DIR:lenses-invert-opencv-pipeline-test>"
        "$<TARGET_FILE:lenses-invert-opencv-pipeline-test>"
        "$<TARGET_FILE:${LENSES_OPENCV_CORE_TARGET}>"
        "$<TARGET_FILE:${LENSES_OPENCV_IMGPROC_TARGET}>"
    )
  endif()
  add_test(NAME lenses-invert-opencv-pipeline-test COMMAND lenses-invert-opencv-pipeline-test)
  set_target_properties(lenses-invert-opencv-pipeline-test PROPERTIES FOLDER plugins/lenses)

  add_executable(lenses-benchmark)
  target_sources(
    lenses-benchmark
    PRIVATE
      test/lenses-benchmark.cpp
      test/lenses-benchmark-module-stub.c
      src/ai/runtime/benchmark-runner.cpp
      src/ai/runtime/backend-resolver.cpp
      src/ai/runtime/mask-generator-factory.cpp
      src/ai/ort/onnx-mask-generator-artifacts.cpp
      src/ai/ort/onnx-mask-generator-bootstrap.cpp
      src/ai/ort/onnx-mask-generator-iobinding.cpp
      src/ai/ort/onnx-mask-generator-layout.cpp
      src/ai/ort/onnx-mask-generator-lifecycle.cpp
      src/ai/ort/onnx-mask-generator-provider.cpp
      src/ai/ort/onnx-mask-generator-runner.cpp
      src/ai/ort/onnx-mask-generator-session.cpp
      src/ai/ort/onnx-mask-generator-session-io.cpp
      src/ai/ort/onnx-mask-generator-worker.cpp
      src/ai/ort/onnx-mask-generator.cpp
      src/ai/ort/onnx-mask-generator-preprocess.cpp
      src/ai/decode/segmentation-decoder.cpp
      src/ai/tracking/bytetrack-tracker.cpp
      src/ai/cloud/cloud-mask-generator.cpp
      src/core/noop-mask-generator.cpp)
  target_include_directories(lenses-benchmark PRIVATE include src)
  target_compile_features(lenses-benchmark PRIVATE cxx_std_20)
  target_link_libraries(lenses-benchmark PRIVATE OBS::libobs)
  if(APPLE)
    target_link_libraries(lenses-benchmark PRIVATE "-framework Accelerate")
  endif()
  if(LENSES_ENABLE_AI)
    target_compile_definitions(lenses-benchmark PRIVATE LENSES_ENABLE_AI=1)
  endif()
  if(LENSES_ENABLE_ORT AND onnxruntime_FOUND)
    target_compile_definitions(lenses-benchmark PRIVATE LENSES_ENABLE_ORT=1)
    target_link_libraries(lenses-benchmark PRIVATE onnxruntime)
  endif()
  if(LENSES_ENABLE_CLOUD)
    target_compile_definitions(lenses-benchmark PRIVATE LENSES_ENABLE_CLOUD=1)
  endif()
  set_target_properties(lenses-benchmark PROPERTIES FOLDER plugins/lenses)
endif()
