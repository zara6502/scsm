cmake_minimum_required(VERSION 3.20)
project(chipdeck_engine_sidplayfp LANGUAGES CXX)

set(SID_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/deps/inst" CACHE PATH
    "Префикс статических libsidplayfp/libresidfp (собирается build-deps.sh)")

if(NOT EXISTS "${SID_DEPS_PREFIX}/lib/libsidplayfp.a")
  message(FATAL_ERROR
    "Не найден ${SID_DEPS_PREFIX}/lib/libsidplayfp.a. "
    "Сначала соберите зависимости: bash build-deps.sh")
endif()

add_library(engine_sidplayfp SHARED engine_sidplayfp.cpp)
target_include_directories(engine_sidplayfp PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
  "${SID_DEPS_PREFIX}/include"
)
target_compile_features(engine_sidplayfp PRIVATE cxx_std_17)
target_compile_definitions(engine_sidplayfp PRIVATE
  CHP_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
)
target_link_libraries(engine_sidplayfp PRIVATE
  "${SID_DEPS_PREFIX}/lib/libsidplayfp.a"
  "${SID_DEPS_PREFIX}/lib/libresidfp.a"
)
if(MINGW)
  target_link_options(engine_sidplayfp PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(engine_sidplayfp PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "engine_sidplayfp"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Engines"
)

add_executable(sid_test test_sid.cpp)
target_include_directories(sid_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
)
target_compile_features(sid_test PRIVATE cxx_std_17)
target_compile_definitions(sid_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(sid_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
