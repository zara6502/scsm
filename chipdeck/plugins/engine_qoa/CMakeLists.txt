cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_qoa LANGUAGES CXX)

add_library(engine_qoa SHARED engine_qoa.cpp)
target_include_directories(engine_qoa PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_compile_features(engine_qoa PRIVATE cxx_std_17)
target_compile_definitions(engine_qoa PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
if(MINGW)
  target_link_options(engine_qoa PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_qoa PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_qoa"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

# Консольный конвертер WAV -> QOA для тестовых сэмплов.
add_executable(wav2qoa wav2qoa.cpp)
target_include_directories(wav2qoa PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(wav2qoa PRIVATE cxx_std_17)
if(MINGW)
  target_link_options(wav2qoa PRIVATE -static -static-libgcc -static-libstdc++)
endif()

add_executable(qoa_test test_qoa.cpp)
target_include_directories(qoa_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(qoa_test PRIVATE cxx_std_17)
target_compile_definitions(qoa_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(qoa_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
