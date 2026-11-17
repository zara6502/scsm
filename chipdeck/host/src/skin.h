// Скины ChipDeck: декларативный skin.json + PNG (WIC -> premultiplied
// ARGB DIB -> AlphaBlend). Спецификация — docs/DESIGN.ru.md раздел 4.
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <vector>

namespace host {

class Player;

struct SkinImage {
    HBITMAP bmp = nullptr;
    int w = 0, h = 0;
    int x = 0, y = 0;
};

struct SkinButton {
    std::string action;
    int x = 0, y = 0, w = 0, h = 0;
    HBITMAP bmp = nullptr;
};

struct SkinText {
    std::string source; // title/author/format/position/duration/state
    RECT r{};
    COLORREF color = RGB(255, 255, 255);
    int font_size = 12;
    std::wstring face = L"Segoe UI";
    bool bold = false;
};

struct SkinProgress {
    RECT r{};
    COLORREF color = RGB(80, 160, 80);
    COLORREF back = RGB(40, 40, 40);
};

struct Skin {
    bool loaded = false;
    std::wstring dir;      // каталог скина (для путей ресурсов)
    std::string name;
    int w = 0, h = 0;      // клиентский размер окна
    HBITMAP bg = nullptr;
    int bg_w = 0, bg_h = 0;
    COLORREF fallback_bg = RGB(32, 32, 32);
    std::vector<SkinImage> images;
    std::vector<SkinButton> buttons;
    std::vector<SkinText> texts;
    std::vector<SkinProgress> progress;
    // иконки лотка (пустые пути -> системный дефолт)
    HICON tray_idle = nullptr;
    HICON tray_playing = nullptr;
    HICON tray_paused = nullptr;
};

// Загружает скин из каталога (dir/skin.json + ресурсы). false = не
// найден/некорректен (и тогда host использует встроенный вид).
bool skin_load(const std::wstring& dir, Skin& out, std::string& err);

// PNG -> premultiplied ARGB DIB (для AlphaBlend). Возвращает HBITMAP,
// размеры в *w/*h. nullptr при ошибке.
HBITMAP png_load_bitmap(const std::wstring& path, int* w, int* h);

// PNG -> HICON (цвет 32bpp c альфой).
HICON png_load_icon(const std::wstring& path);

// Полный рендер скина под текущее состояние плеера.
void skin_draw(HDC hdc, const Skin& skin, const Player& player);

// Hit-test кнопки по действию; nullptr если нет.
const SkinButton* skin_hit_test(const Skin& skin, int x, int y);

} // namespace host
