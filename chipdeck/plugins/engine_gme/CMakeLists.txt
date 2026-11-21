cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_gme LANGUAGES CXX)

set(GME_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/deps/inst" CACHE PATH
    "Префикс статической libgme (собирается build-deps.sh)")

if(NOT EXISTS "${GME_DEPS_PREFIX}/lib/libgme.a")
  message(FATAL_ERROR
    "Не найден ${GME_DEPS_PREFIX}/lib/libgme.a. "
    "Сначала соберите зависимости: bash build-deps.sh")
endif()

add_library(engine_gme SHARED engine_gme.cpp)
target_include_directories(engine_gme PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${GME_DEPS_PREFIX}/include"
)
target_compile_features(engine_gme PRIVATE cxx_std_17)
target_compile_definitions(engine_gme PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(engine_gme PRIVATE
  "${GME_DEPS_PREFIX}/lib/libgme.a"
  # zlib: статическая (libz.a) для самодостаточности DLL (VGZ)
  C:/msys64/ucrt64/lib/libz.a
)
if(MINGW)
  target_link_options(engine_gme PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_gme PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_gme"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(gme_test test_gme.cpp)
target_include_directories(gme_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(gme_test PRIVATE cxx_std_17)
target_compile_definitions(gme_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(gme_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
