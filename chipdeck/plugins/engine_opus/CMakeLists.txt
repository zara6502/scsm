cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_opus LANGUAGES CXX)

set(OPUS_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/deps/inst" CACHE PATH
    "Префикс статической libopusfile (собирается build-deps.sh)")

if(NOT EXISTS "${OPUS_DEPS_PREFIX}/lib/libopusfile.a")
  message(FATAL_ERROR
    "Не найден ${OPUS_DEPS_PREFIX}/lib/libopusfile.a. "
    "Сначала соберите зависимости: bash build-deps.sh")
endif()

add_library(engine_opus SHARED engine_opus.cpp)
target_include_directories(engine_opus PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${OPUS_DEPS_PREFIX}/include"
  "/ucrt64/include"
  "C:/msys64/ucrt64/include/opus"
)
target_compile_features(engine_opus PRIVATE cxx_std_17)
target_compile_definitions(engine_opus PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(engine_opus PRIVATE
  "${OPUS_DEPS_PREFIX}/lib/libopusfile.a"
  C:/msys64/ucrt64/lib/libopus.a
  C:/msys64/ucrt64/lib/libogg.a
)
if(MINGW)
  target_link_options(engine_opus PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_opus PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_opus"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(opus_test test_opus.cpp)
target_include_directories(opus_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(opus_test PRIVATE cxx_std_17)
target_compile_definitions(opus_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(opus_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
