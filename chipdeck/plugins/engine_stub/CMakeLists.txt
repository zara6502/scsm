cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_stub LANGUAGES CXX)

add_library(engine_stub SHARED engine_stub.cpp)
target_include_directories(engine_stub PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_compile_features(engine_stub PRIVATE cxx_std_17)
target_compile_definitions(engine_stub PRIVATE CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(engine_stub PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(engine_stub PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_stub"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(stub_test test_stub.cpp)
target_include_directories(stub_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_compile_features(stub_test PRIVATE cxx_std_17)
target_compile_definitions(stub_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(stub_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
