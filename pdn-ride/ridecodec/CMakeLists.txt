# RideCodec.dll — C ABI над reference-кодеком RIDE (rideent).
# Требует собранную статическую библиотеку rideent (libride.a):
#   RIDEENT_ROOT — корень проекта rideent (по умолчанию C:/work/rideent)
#   Сборка rideent: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#                   cmake --build build --target ride
cmake_minimum_required(VERSION 3.20)
project(RideCodec LANGUAGES CXX)

set(RIDEENT_ROOT "C:/work/rideent" CACHE PATH "Корень проекта rideent")

if(NOT EXISTS "${RIDEENT_ROOT}/include/ride/ride.hpp")
  message(FATAL_ERROR "rideent не найден: ${RIDEENT_ROOT} (задайте RIDEENT_ROOT)")
endif()
if(NOT EXISTS "${RIDEENT_ROOT}/build/libride.a")
  message(STATUS "libride.a не найдена — собираю rideent...")
  file(MAKE_DIRECTORY "${RIDEENT_ROOT}/build")
  execute_process(COMMAND cmake -S "${RIDEENT_ROOT}" -B "${RIDEENT_ROOT}/build"
                          -G Ninja -DCMAKE_BUILD_TYPE=Release
                  RESULT_VARIABLE r)
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "конфигурация rideent не удалась")
  endif()
  execute_process(COMMAND cmake --build "${RIDEENT_ROOT}/build" --target ride
                  RESULT_VARIABLE r)
  if(NOT r EQUAL 0)
    message(FATAL_ERROR "сборка rideent не удалась")
  endif()
endif()

add_library(RideCodec SHARED
  src/ride_codec.cpp
  src/quantizer.cpp
  "${RIDEENT_ROOT}/src/format.cpp"
  "${RIDEENT_ROOT}/src/rgb_codec.cpp"
  "${RIDEENT_ROOT}/src/indexed_codec.cpp"
  "${RIDEENT_ROOT}/src/qoi_codec.cpp"
  "${RIDEENT_ROOT}/src/scan.cpp"
  "${RIDEENT_ROOT}/src/entropy.cpp"
  "${RIDEENT_ROOT}/src/codec.cpp"
)
target_include_directories(RideCodec PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${RIDEENT_ROOT}/include"
  "${RIDEENT_ROOT}/src"
)
target_compile_features(RideCodec PRIVATE cxx_std_20)
target_compile_definitions(RideCodec PRIVATE RIDE_CODEC_BUILD)
if(MINGW)
  target_link_options(RideCodec PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(RideCodec PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "RideCodec"
  SUFFIX ".dll"
)

add_executable(codec_smoke EXCLUDE_FROM_ALL tests/codec_smoke.cpp)
target_include_directories(codec_smoke PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(codec_smoke PRIVATE cxx_std_20)
target_link_libraries(codec_smoke PRIVATE RideCodec)
