cmake_minimum_required(VERSION 3.20)
project(ride_codec_heic LANGUAGES CXX)

set(HEIC_DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/deps/inst" CACHE PATH
    "Префикс со статическими libheif/libde265 (собирается build-deps.sh)")

if(NOT EXISTS "${HEIC_DEPS_PREFIX}/lib/libheif.a")
  message(FATAL_ERROR
    "Не найден ${HEIC_DEPS_PREFIX}/lib/libheif.a. "
    "Сначала соберите зависимости: bash build-deps.sh")
endif()

add_library(ride_codec_heic SHARED
  src/codec.cpp
  src/heic_decoder.cpp
)
target_include_directories(ride_codec_heic PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
  "${HEIC_DEPS_PREFIX}/include"
)
target_compile_features(ride_codec_heic PRIVATE cxx_std_17)
target_compile_definitions(ride_codec_heic PRIVATE
  RIDE_CODEC_BUILD WIN32_LEAN_AND_MEAN NOMINMAX
  LIBHEIF_STATIC_BUILD LIBDE265_STATIC_BUILD
)
target_link_libraries(ride_codec_heic PRIVATE
  "${HEIC_DEPS_PREFIX}/lib/libheif.a"
  "${HEIC_DEPS_PREFIX}/lib/libde265.a"
)
if(MINGW)
  # Самодостаточная DLL: без зависимостей от DLL MSYS2.
  # --exclude-all-symbols прячет символы статических libheif/libde265
  # из таблицы экспорта; dllexport из общего заголовка остаётся.
  target_link_options(ride_codec_heic PRIVATE
    -static -static-libgcc -static-libstdc++
    -Wl,--exclude-all-symbols
  )
endif()
set_target_properties(ride_codec_heic PROPERTIES
  CXX_EXTENSIONS OFF
  PREFIX ""
  OUTPUT_NAME "ride_codec_heic"
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Codecs"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Codecs"
)

# Тестовый стенд: грузит готовую DLL и проверяет контракт ABI.
add_executable(codec_test tests/harness.cpp)
target_include_directories(codec_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)
target_compile_features(codec_test PRIVATE cxx_std_17)
target_compile_definitions(codec_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
  target_link_options(codec_test PRIVATE -static -static-libgcc -static-libstdc++)
endif()
