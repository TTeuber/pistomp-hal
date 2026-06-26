# pistomp_hal -- hardware abstraction layer for Pi-based audio pedals.
#
# A JUCE-free static library of the reusable hardware drivers:
#   * encoder      -- quadrature rotary encoders (libgpiod)
#   * footswitch   -- momentary switches read over SPI (spidev)
#   * gpio_button  -- momentary push-switch on a raw GPIO line (libgpiod)
#   * leds         -- NeoPixel state lights
#   * ili9341      -- SPI TFT panel driver (spidev + libgpiod)
#   * lvgl_display -- glue that drives an LVGL UI onto the ILI9341
#   * audio_io     -- realtime duplex audio on the codec (ALSA)
#
# Owns the LVGL dependency (and its lv_conf.h), exposed PUBLICly so any consumer
# that links pistomp_hal also gets LVGL and the driver headers without repeating
# the wiring. Deliberately depends on NO audio framework -- the HAL is about
# hardware lines, not DSP, so it stays reusable across apps.
#
# PLATFORMS. Each subsystem's header is the contract; the .cpp behind it is
# selected per platform here -- "a file move, not a rewrite":
#   * Linux/Pi (else branch): the real drivers (libgpiod, spidev, ALSA, ILI9341).
#   * macOS (APPLE branch): a desktop simulator -- LVGL in an SDL window, audio
#     via miniaudio/CoreAudio, controls fed from the keyboard. Lets the whole app
#     run on the Mac with no Pi. See *_sim.cpp / *_stub.cpp / *_sdl.cpp / *_mac.cpp.

find_package(Threads REQUIRED)
include(FetchContent)

if(APPLE)
  # --- macOS desktop simulator backend -------------------------------------
  find_package(SDL2 REQUIRED)   # brew install sdl2

  # miniaudio: single public-domain header. Populate ONLY (SOURCE_SUBDIR points
  # at a path with no CMakeLists.txt, so MakeAvailable fetches the source but
  # does NOT configure miniaudio's own CMake -- we don't want its vorbis/opus/
  # node-graph machinery, just the header, which we compile via our own TU).
  FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR  do-not-configure
  )
  FetchContent_MakeAvailable(miniaudio)   # ${miniaudio_SOURCE_DIR}/miniaudio.h

  set(HAL_SOURCES
    encoder_sim.cpp
    footswitch_sim.cpp
    gpio_button_sim.cpp
    leds_stub.cpp
    input_level_stub.cpp
    ili9341_stub.cpp
    lvgl_display_sdl.cpp
    audio_io_mac.cpp
    sim_input.cpp
    settings_menu_mac.mm
  )
else()
  # --- Linux / Raspberry Pi hardware backend -------------------------------
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GPIOD REQUIRED libgpiod)   # libgpiod v1 C API
  pkg_check_modules(ALSA REQUIRED alsa)        # libasound, for audio_io (codec I/O)

  set(HAL_SOURCES
    encoder.cpp
    footswitch.cpp
    gpio_button.cpp
    leds.cpp
    input_level.cpp
    ili9341.cpp
    lvgl_display.cpp
    audio_io.cpp
    settings_menu_stub.cpp
  )
endif()

# --- LVGL v9. Point it at our lv_conf.h via LV_CONF_INCLUDE_SIMPLE + adding this
#     dir to LVGL's include path (the most version-robust way to supply a config
#     through FetchContent). Skip its examples/demos. ---
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

# On macOS, turn on LVGL's built-in SDL window/mouse/keyboard driver and give
# the lvgl target SDL2 so its drivers/sdl/*.c compile and link (they #include
# <SDL2/SDL.h>). PUBLIC so our SDL display backend sees the same headers.
if(APPLE)
  target_compile_definitions(lvgl PUBLIC LV_USE_SDL=1)
  target_link_libraries(lvgl PUBLIC SDL2::SDL2)
endif()

add_library(pistomp_hal STATIC ${HAL_SOURCES})

target_include_directories(pistomp_hal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pistomp_hal PUBLIC
  lvgl
  Threads::Threads
)

if(APPLE)
  # miniaudio header + the CoreAudio frameworks its backend needs. PRIVATE: the
  # header (with MINIAUDIO_IMPLEMENTATION) is an implementation detail of
  # audio_io_mac.cpp, just like libasound is on the Pi.
  target_include_directories(pistomp_hal PRIVATE ${miniaudio_SOURCE_DIR})
  target_link_libraries(pistomp_hal PRIVATE
    SDL2::SDL2
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework AudioUnit"
    "-framework Cocoa"        # settings_menu_mac.mm: NSMenu / NSWindow / NSPopUpButton
  )
else()
  # PUBLIC libgpiod so consumers inherit the driver headers just by linking.
  target_include_directories(pistomp_hal PUBLIC ${GPIOD_INCLUDE_DIRS})
  target_link_libraries(pistomp_hal PUBLIC ${GPIOD_LIBRARIES})

  # libasound is needed only by audio_io.cpp, and audio_io.h is deliberately
  # ALSA-free, so keep it PRIVATE: consumers don't take ALSA headers into their
  # compile. (As a static lib, the link still propagates to the final
  # executable, which is fine -- libasound is always present on the Pi.)
  target_include_directories(pistomp_hal PRIVATE ${ALSA_INCLUDE_DIRS})
  target_link_libraries(pistomp_hal PRIVATE ${ALSA_LIBRARIES})
endif()
