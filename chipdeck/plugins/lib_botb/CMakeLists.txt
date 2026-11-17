cmake_minimum_required(VERSION 3.20)
project(chipdeck_lib_botb LANGUAGES CXX)

add_library(lib_botb SHARED lib_botb.cpp)
target_include_directories(lib_botb PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(lib_botb PRIVATE cxx_std_17)
target_compile_definitions(lib_botb PRIVATE CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(lib_botb PRIVATE winhttp)
if(MINGW)
  target_link_options(lib_botb PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(lib_botb PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "lib_botb"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Libraries"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Libraries"
)

add_executable(botb_test test_botb.cpp)
target_include_directories(botb_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(botb_test PRIVATE cxx_std_17)
target_compile_definitions(botb_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(botb_test PRIVATE winhttp)
if(MINGW)
  target_link_options(botb_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
