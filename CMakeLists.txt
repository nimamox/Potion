cmake_minimum_required(VERSION 3.20)
project(Potion VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(POTION_ENABLE_IPO "Enable checked C/C++ IPO for Kindle release builds" OFF)
option(POTION_VECTOR_REPORT "Report GCC vectorization decisions" OFF)
option(POTION_BUILD_BENCHMARKS "Build standalone workload benchmarks (not packaged)" OFF)
if(POTION_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR LANGUAGES C CXX)
  if(NOT IPO_SUPPORTED)
    message(FATAL_ERROR "IPO unavailable: ${IPO_ERROR}")
  endif()
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
if(POTION_VECTOR_REPORT)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Vector reports require GCC")
  endif()
  add_compile_options(-fopt-info-vec-optimized -fopt-info-vec-missed)
  add_link_options(-fopt-info-vec-optimized -fopt-info-vec-missed)
endif()

include(CTest)
find_package(Threads REQUIRED)
set(CURL_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

set(POTION_BUILD_COMMIT "$ENV{POTION_BUILD_COMMIT}")
if(NOT POTION_BUILD_COMMIT)
  find_package(Git QUIET)
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      RESULT_VARIABLE POTION_GIT_RESULT
      OUTPUT_VARIABLE POTION_BUILD_COMMIT
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT POTION_GIT_RESULT EQUAL 0)
      set(POTION_BUILD_COMMIT "unknown")
    else()
      execute_process(
        COMMAND "${GIT_EXECUTABLE}" diff-index --quiet HEAD --
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE POTION_GIT_DIRTY)
      if(NOT POTION_GIT_DIRTY EQUAL 0)
        string(APPEND POTION_BUILD_COMMIT "-dirty")
      endif()
    endif()
  else()
    set(POTION_BUILD_COMMIT "unknown")
  endif()
endif()
if(CMAKE_CROSSCOMPILING)
  set(POTION_BUILD_TYPE "release")
else()
  set(POTION_BUILD_TYPE "development")
endif()

option(POTION_BUILD_TESTS "Build Potion tests" ON)
option(POTION_BUILD_SIMULATOR "Build the browser-based Kindle simulator" OFF)
set(POTION_MATH_LIBRARY "" CACHE FILEPATH "Path to the native libpotion_math.a")

if(NOT POTION_MATH_LIBRARY)
  find_program(POTION_CARGO_EXECUTABLE cargo REQUIRED)
  set(POTION_MATH_TARGET_DIR "${CMAKE_BINARY_DIR}/math-target")
  set(POTION_MATH_LIBRARY
      "${POTION_MATH_TARGET_DIR}/release/${CMAKE_STATIC_LIBRARY_PREFIX}potion_math${CMAKE_STATIC_LIBRARY_SUFFIX}")
  add_custom_command(
    OUTPUT "${POTION_MATH_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env "CARGO_TARGET_DIR=${POTION_MATH_TARGET_DIR}"
            "${POTION_CARGO_EXECUTABLE}" build --locked --release
            --manifest-path "${CMAKE_SOURCE_DIR}/backend/math/Cargo.toml"
    DEPENDS backend/math/Cargo.toml backend/math/Cargo.lock backend/math/src/lib.rs
    COMMENT "Building the native katex-rs renderer"
    VERBATIM)
  add_custom_target(potion_math_build DEPENDS "${POTION_MATH_LIBRARY}")
endif()

add_library(potion_math STATIC IMPORTED GLOBAL)
set_target_properties(potion_math PROPERTIES IMPORTED_LOCATION "${POTION_MATH_LIBRARY}")
if(TARGET potion_math_build)
  add_dependencies(potion_math potion_math_build)
endif()

add_library(potion_core
  src/json.cpp src/app_state.cpp src/orientation.cpp src/reading_positions.cpp src/markdown.cpp
  src/math_renderer.cpp src/image_cache.cpp src/page_cache.cpp src/notion_client.cpp
  src/update_checker.cpp src/http_server.cpp)
target_include_directories(potion_core PUBLIC include)
target_link_libraries(potion_core PUBLIC potion_math CURL::libcurl OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(potion_core PUBLIC dl m rt)
endif()
target_compile_definitions(potion_core PRIVATE
  POTION_VERSION="${PROJECT_VERSION}"
  POTION_BUILD_COMMIT="${POTION_BUILD_COMMIT}"
  POTION_BUILD_TYPE="${POTION_BUILD_TYPE}")
target_compile_options(potion_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(potiond src/daemon_main.cpp)
target_link_libraries(potiond PRIVATE potion_core)
target_compile_options(potiond PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(potiond PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(potion_mesquite_whisper_touch SHARED src/mesquite_whisper_touch.c)
  # This preload runs inside system Mesquite, not under our bundled loader.
  # Leave GLib/GDK/dlsym references unversioned for the Kindle's native ABI.
  set_target_properties(potion_mesquite_whisper_touch PROPERTIES
    OUTPUT_NAME mesquite-whisper-touch
    LINK_FLAGS "-nostdlib")
  add_dependencies(potiond potion_mesquite_whisper_touch)
endif()

if(POTION_BUILD_SIMULATOR)
  if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "POTION_BUILD_SIMULATOR requires a native build")
  endif()
  add_custom_target(potion_simulator
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/run-simulator.sh"
            "$<TARGET_FILE:potiond>" "${CMAKE_SOURCE_DIR}/assets"
            "${CMAKE_SOURCE_DIR}/simulator" "${CMAKE_SOURCE_DIR}/.potion-simulator"
    DEPENDS potiond USES_TERMINAL COMMENT "Starting the Potion Kindle simulator")
endif()

if(POTION_BUILD_TESTS)
  add_executable(potion_motion_hint_tests tests/motion_hint_test.c)
  add_test(NAME potion_motion_hint_tests COMMAND potion_motion_hint_tests)
  add_executable(potion_tests tests/potion_test.cpp)
  target_link_libraries(potion_tests PRIVATE potion_core)
  target_compile_definitions(potion_tests PRIVATE
    POTION_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
  add_test(NAME potion_tests COMMAND potion_tests)
  find_program(POTION_NODE_EXECUTABLE node)
  if(POTION_NODE_EXECUTABLE)
    add_executable(potion_math_fixtures tests/math_fixtures.cpp)
    target_link_libraries(potion_math_fixtures PRIVATE potion_core)
    add_test(NAME potion_math_layout_tests
      COMMAND "${POTION_NODE_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/math_layout_test.js"
              "${CMAKE_SOURCE_DIR}/assets/app.js"
              "$<TARGET_FILE:potion_math_fixtures>"
              "${CMAKE_SOURCE_DIR}/tests/math_cases.json")
    add_test(NAME potion_frontend_contract_tests
      COMMAND "${POTION_NODE_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tests/frontend_contract_test.js"
              "${CMAKE_SOURCE_DIR}/assets/app.js"
              "${CMAKE_SOURCE_DIR}/assets/index.html"
              "${CMAKE_SOURCE_DIR}/assets/vendor/katex/katex.min.js"
              "${CMAKE_SOURCE_DIR}/assets/vendor/katex/katex.min.css"
              "${CMAKE_SOURCE_DIR}/assets/vendor/katex/fonts"
              "${CMAKE_SOURCE_DIR}/assets/app.css"
              "${CMAKE_SOURCE_DIR}/assets/config.xml"
              "${CMAKE_SOURCE_DIR}/scripts/run-kindle.sh"
              "${CMAKE_SOURCE_DIR}/simulator/index.html"
              "${CMAKE_SOURCE_DIR}/simulator/simulator.js"
              "${CMAKE_SOURCE_DIR}/simulator/simulator.css"
              "${CMAKE_SOURCE_DIR}/simulator/host-fonts.css"
              "${CMAKE_SOURCE_DIR}/src/http_server.cpp"
              "${CMAKE_SOURCE_DIR}/src/mesquite_whisper_touch.c"
              "${CMAKE_SOURCE_DIR}/assets/vendor/fast-font/fonts"
              "${CMAKE_SOURCE_DIR}/packaging/fontconfig-potion.conf"
              "${CMAKE_SOURCE_DIR}/src/notion_client.cpp")
  endif()
endif()

if(POTION_BUILD_BENCHMARKS)
  add_executable(workload_benchmark tests/workload_benchmark.cpp)
  target_link_libraries(workload_benchmark PRIVATE potion_core)
endif()
