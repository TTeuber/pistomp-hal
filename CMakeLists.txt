# pistomp_hal -- hardware abstraction layer for Pi-based audio pedals.
#
# A JUCE-free static library of the reusable hardware drivers:
#   * encoder      -- quadrature rotary encoders (libgpiod)
#   * footswitch   -- momentary switches read over SPI (spidev)
#   * gpio_button  -- momentary push-switch on a raw GPIO line (libgpiod)
#   * leds         -- NeoPixel state lights
#   * ili9341      -- SPI TFT panel driver (spidev + libgpiod)
#   * lvgl_display -- glue that drives an LVGL UI onto the ILI9341
#
# Owns the LVGL dependency (and its lv_conf.h), exposed PUBLICly so any consumer
# that links pistomp_hal also gets LVGL, libgpiod and the driver headers without
# repeating the wiring. Deliberately depends on NO audio framework -- the HAL is
# about hardware lines, not DSP, so it stays reusable across apps.

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPIOD REQUIRED libgpiod)   # libgpiod v1 C API
find_package(Threads REQUIRED)

# --- LVGL v9. Point it at our lv_conf.h via LV_CONF_INCLUDE_SIMPLE + adding this
#     dir to LVGL's include path (the most version-robust way to supply a config
#     through FetchContent). Skip its examples/demos. ---
include(FetchContent)
FetchContent_Declare(
  lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.2.2
  GIT_SHALLOW TRUE
)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(pistomp_hal STATIC
  encoder.cpp
  footswitch.cpp
  gpio_button.cpp
  leds.cpp
  ili9341.cpp
  lvgl_display.cpp
)

# PUBLIC so consumers inherit the driver headers, LVGL, libgpiod and threads just
# by linking pistomp_hal.
target_include_directories(pistomp_hal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${GPIOD_INCLUDE_DIRS}
)
target_link_libraries(pistomp_hal PUBLIC
  lvgl
  ${GPIOD_LIBRARIES}
  Threads::Threads
)
