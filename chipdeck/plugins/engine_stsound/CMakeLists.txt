cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_stsound LANGUAGES CXX)

# Исходники StSoundLibrary вендорятся целиком (deps/StSound-main) и
# компилируются прямо в DLL — отдельная статическая библиотека не нужна.
set(STSOUND "${CMAKE_CURRENT_SOURCE_DIR}/deps/StSound-main/StSoundLibrary")

add_library(engine_stsound SHARED
  engine_stsound.cpp
  "${STSOUND}/Ym2149Ex.cpp"
  "${STSOUND}/YmMusic.cpp"
  "${STSOUND}/YmUserInterface.cpp"
  "${STSOUND}/Ymload.cpp"
  "${STSOUND}/digidrum.cpp"
  "${STSOUND}/LZH/LzhLib.cpp"
)
target_include_directories(engine_stsound PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${STSOUND}"
)
target_compile_features(engine_stsound PRIVATE cxx_std_17)
target_compile_definitions(engine_stsound PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX NDEBUG
)
if(MINGW)
  target_link_options(engine_stsound PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_stsound PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_stsound"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(ym_test test_stsound.cpp)
target_include_directories(ym_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(ym_test PRIVATE cxx_std_17)
target_compile_definitions(ym_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(ym_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
