cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_dr LANGUAGES CXX)

add_library(engine_dr SHARED engine_dr.cpp)
target_include_directories(engine_dr PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(engine_dr PRIVATE cxx_std_17)
target_compile_definitions(engine_dr PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
if(MINGW)
  target_link_options(engine_dr PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_dr PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_dr"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(dr_test test_dr.cpp)
target_include_directories(dr_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(dr_test PRIVATE cxx_std_17)
target_compile_definitions(dr_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(dr_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
