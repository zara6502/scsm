cmake_minimum_required(VERSION 3.20)
project(chipdeck_host LANGUAGES CXX)

add_executable(chipdeck_host WIN32
  src/main.cpp
  src/common.cpp
  src/engine_manager.cpp
  src/audio.cpp
  src/player.cpp
  src/ui.cpp
)
target_include_directories(chipdeck_host PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_compile_features(chipdeck_host PRIVATE cxx_std_17)
target_compile_definitions(chipdeck_host PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(chipdeck_host PRIVATE ole32 uuid comdlg32 shell32 user32 gdi32)
if(MINGW)
  target_link_options(chipdeck_host PRIVATE
    -static -static-libgcc -static-libstdc++ -municode
  )
endif()
set_target_properties(chipdeck_host PROPERTIES CXX_EXTENSIONS OFF)
