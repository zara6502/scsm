// Общие утилиты host'а ChipDeck.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace host {

// UTF-8 <-> UTF-16.
std::wstring utf8_to_wide(const std::string& s);
std::string wide_to_utf8(const std::wstring& s);

// Читает файл целиком; false при ошибке/слишком большом файле.
bool read_file_bytes(const std::wstring& path, std::vector<uint8_t>& out);

// Каталог, где лежит exe хоста (без имени файла).
std::wstring exe_dir();

// "мм:сс".
std::string format_ms(uint32_t ms);

} // namespace host
