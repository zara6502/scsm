cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_vorbis LANGUAGES CXX)

add_library(engine_vorbis SHARED engine_vorbis.cpp)
target_include_directories(engine_vorbis PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(engine_vorbis PRIVATE cxx_std_17)
target_compile_definitions(engine_vorbis PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
if(MINGW)
  target_link_options(engine_vorbis PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_vorbis PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_vorbis"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(vb_test test_vorbis.cpp)
target_include_directories(vb_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(vb_test PRIVATE cxx_std_17)
target_compile_definitions(vb_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(vb_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
