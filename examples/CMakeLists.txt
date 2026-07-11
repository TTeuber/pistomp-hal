# examples/ — four small demos of the pistomp_hal API, smallest to largest:
#   passthrough  audio only        — open the codec, copy in -> out
#   controls     control surface   — encoders + footswitches + NeoPixels
#   display      screen only       — LVGL on the ILI9341 (SDL window on the Mac)
#   mini_pedal   the whole thing   — audio + a knob + a footswitch + the screen
#
# The SAME main.cpp compiles for the Pi and the macOS simulator: CMake selects
# the per-platform driver .cpp behind each pistomp/ header (see the root
# CMakeLists), so there is not a single #ifdef in the example code. Each program
# just links pistomp_hal, which carries the driver headers, LVGL and pthreads.

function(pistomp_example name)
  add_executable(${name} ${name}/main.cpp)
  target_link_libraries(${name} PRIVATE pistomp_hal)
endfunction()

pistomp_example(passthrough)
pistomp_example(controls)
pistomp_example(display)
pistomp_example(mini_pedal)
