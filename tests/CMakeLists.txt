# pistomp_hal host-runnable unit tests -- Catch2 + ctest, hardware-free.
#
# The extracted decision logic in include/pistomp/detail/ (quadrature decode,
# switch edge/threshold) is std-only, so this suite compiles those headers
# directly and runs on any host -- no libgpiod/spidev/ALSA, no Pi. It does NOT
# link pistomp_hal (that pulls in LVGL/SDL and the platform backends); the point
# is that the logic is separable from the hardware I/O.

find_package(Threads REQUIRED)

# Catch2 v3, pinned by tag -- same FetchContent pattern as LVGL/miniaudio in the
# root CMakeLists. Catch2::Catch2WithMain supplies main().
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(pistomp_hal_tests
  test_quadrature.cpp
  test_debounce.cpp
)
target_include_directories(pistomp_hal_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(pistomp_hal_tests PRIVATE
  Catch2::Catch2WithMain
  Threads::Threads)

# Register each TEST_CASE as its own ctest test (run `ctest` in the build dir).
catch_discover_tests(pistomp_hal_tests)
