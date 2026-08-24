cmake_minimum_required(VERSION 3.20)
project(Potion VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
find_package(Threads REQUIRED)
set(CURL_USE_STATIC_LIBS ON)
set(OPENSSL_USE_STATIC_LIBS ON)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

option(POTION_BUILD_TESTS "Build Potion tests" ON)
option(POTION_BUILD_SIMULATOR "Build the browser-based Kindle simulator" OFF)

add_library(potion_core
  src/json.cpp src/app_state.cpp src/markdown.cpp src/notion_client.cpp src/http_server.cpp)
target_include_directories(potion_core PUBLIC include)
target_link_libraries(potion_core PUBLIC CURL::libcurl OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(potion_core PUBLIC dl)
endif()
target_compile_definitions(potion_core PRIVATE POTION_VERSION="${PROJECT_VERSION}")
target_compile_options(potion_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(potiond src/daemon_main.cpp)
target_link_libraries(potiond PRIVATE potion_core)
target_compile_options(potiond PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(potiond PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")

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
  add_executable(potion_tests tests/potion_test.cpp)
  target_link_libraries(potion_tests PRIVATE potion_core)
  add_test(NAME potion_tests COMMAND potion_tests)
endif()

