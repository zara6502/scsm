#include "skin.h"

#include "json.h"
#include "player.h"

#include <wincodec.h>

#include <cstdio>
#include <cstring>

namespace host {

int g_alphablend_failures = 0;

namespace {

// ---- WIC: PNG -> 32bpp premultiplied DIB ----

bool wic_decode_png(const std::wstring& path, std::vector<uint8_t>& pixels,
                    int* w, int* h) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  __uuidof(IWICImagingFactory),
                                  reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) return false;

    bool ok = false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    do {
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &dec)))
            break;
        if (FAILED(dec->GetFrame(0, &frame))) break;
        if (FAILED(factory->CreateFormatConverter(&conv))) break;
        if (FAILED(conv->Initialize(
                frame, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom)))
            break;

        UINT iw = 0, ih = 0;
        if (FAILED(conv->GetSize(&iw, &ih)) || iw == 0 || ih == 0) break;
        *w = int(iw);
        *h = int(ih);

        const UINT stride = iw * 4;
        const UINT buf_size = stride * ih;
        pixels.resize(buf_size);
        if (FAILED(conv->CopyPixels(nullptr, stride, buf_size,
                                    pixels.data())))
            break;
        ok = true;
    } while (false);

    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    factory->Release();
    return ok;
}

HBITMAP dib_from_pixels(const std::vector<uint8_t>& pixels, int w, int h,
                        int* out_stride) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!bmp) return nullptr;
    std::memcpy(bits, pixels.data(), size_t(w) * 4 * size_t(h));
    *out_stride = w * 4;
    return bmp;
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parse_color(const std::string& s, COLORREF* out) {
    if (s.size() == 7 && s[0] == '#') {
        int r = hex_val(s[1]), g = hex_val(s[3]), b = hex_val(s[5]);
        if (r < 0 || g < 0 || b < 0) return false;
        r = (r << 4) | hex_val(s[2]);
        g = (g << 4) | hex_val(s[4]);
        b = (b << 4) | hex_val(s[6]);
        *out = RGB(r, g, b);
        return true;
    }
    return false;
}

HICON icon_from_dib(HBITMAP bmp, int w, int h) {
    // маска: монохромный битмап, все нули — альфа берётся из color bitmap
    HDC dc = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(mem, mask));
    PatBlt(mem, 0, 0, w, h, BLACKNESS);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, dc);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = bmp;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    return icon;
}

} // namespace

HBITMAP png_load_bitmap(const std::wstring& path, int* w, int* h) {
    std::vector<uint8_t> pixels;
    int iw = 0, ih = 0;
    if (!wic_decode_png(path, pixels, &iw, &ih)) return nullptr;
    int stride = 0;
    HBITMAP bmp = dib_from_pixels(pixels, iw, ih, &stride);
    if (bmp && w) *w = iw;
    if (bmp && h) *h = ih;
    return bmp;
}

HICON png_load_icon(const std::wstring& path) {
    int w = 0, h = 0;
    HBITMAP bmp = png_load_bitmap(path, &w, &h);
    if (!bmp) return nullptr;
    HICON icon = icon_from_dib(bmp, w, h);
    DeleteObject(bmp);
    return icon;
}

// rect в skin.json — [x, y, w, h]; конвертация в RECT.
static bool rect_from_json(const JsonValue* r, RECT* out) {
    if (!r || r->type != JsonValue::Array || r->arr.size() != 4) return false;
    const int x = int(r->arr[0].num), y = int(r->arr[1].num);
    const int w = int(r->arr[2].num), h = int(r->arr[3].num);
    if (w <= 0 || h <= 0) return false;
    out->left = x;
    out->top = y;
    out->right = x + w;
    out->bottom = y + h;
    return true;
}

bool skin_load(const std::wstring& dir, Skin& out, std::string& err) {
    out = Skin{};
    out.dir = dir;

    const std::wstring json_path = dir + L"\\skin.json";
    JsonValue root;
    if (!json_parse_file(json_path.c_str(), root, err)) {
        if (err == "cannot open file") err = "skin.json not found";
        return false;
    }
    if (root.get_int("api", 0) != 1) {
        err = "unsupported skin api version";
        return false;
    }
    out.name = root.get_str("name", "");
    out.w = root.get_int("w", 0);
    out.h = root.get_int("h", 0);
    {
        const JsonValue* win = root.find("window");
        if (win) {
            out.w = win->get_int("w", out.w);
            out.h = win->get_int("h", out.h);
        }
    }
    if (out.w <= 0 || out.h <= 0 || out.w > 4096 || out.h > 4096) {
        err = "bad window size";
        return false;
    }
    parse_color(root.get_str("background", ""), &out.fallback_bg);

    // фон
    const JsonValue* bg = root.find("bg");
    if (bg && bg->type == JsonValue::Str) {
        const std::wstring p = dir + L"\\" + utf8_to_wide(bg->str);
        out.bg = png_load_bitmap(p, &out.bg_w, &out.bg_h);
    }

    // tray
    if (const JsonValue* tray = root.find("tray")) {
        auto icon_or_null = [&](const char* key) -> HICON {
            const JsonValue* v = tray->find(key);
            if (!v || v->type != JsonValue::Str) return nullptr;
            return png_load_icon(dir + L"\\" + utf8_to_wide(v->str));
        };
        out.tray_idle = icon_or_null("idle");
        out.tray_playing = icon_or_null("playing");
        out.tray_paused = icon_or_null("paused");
    }

    bool have_buttons = false;
    if (const JsonValue* els = root.find("elements")) {
        if (els->type == JsonValue::Array) {
            for (const JsonValue& el : els->arr) {
                const std::string type = el.get_str("type");
                if (type == "image") {
                    const std::string img = el.get_str("image");
                    if (img.empty()) continue;
                    SkinImage si;
                    si.bmp = png_load_bitmap(dir + L"\\" + utf8_to_wide(img),
                                             &si.w, &si.h);
                    if (!si.bmp) continue;
                    si.x = el.get_int("x", 0);
                    si.y = el.get_int("y", 0);
                    out.images.push_back(si);
                } else if (type == "button") {
                    const std::string action = el.get_str("action");
                    const std::string img = el.get_str("image");
                    if (action.empty() || img.empty()) continue;
                    SkinButton b;
                    b.action = action;
                    b.x = el.get_int("x", 0);
                    b.y = el.get_int("y", 0);
                    b.bmp = png_load_bitmap(dir + L"\\" + utf8_to_wide(img),
                                            &b.w, &b.h);
                    if (!b.bmp) continue;
                    out.buttons.push_back(b);
                    have_buttons = true;
                } else if (type == "text") {
                    SkinText t;
                    t.source = el.get_str("source");
                    if (t.source.empty()) continue;
                    if (!rect_from_json(el.find("rect"), &t.r)) continue;
                    parse_color(el.get_str("color", "#FFFFFF"), &t.color);
                    t.font_size = el.get_int("size", 12);
                    t.face = utf8_to_wide(el.get_str("face", "Segoe UI"));
                    t.bold = el.get_int("bold", 0) != 0;
                    out.texts.push_back(t);
                } else if (type == "progress") {
                    SkinProgress pr;
                    if (!rect_from_json(el.find("rect"), &pr.r)) continue;
                    parse_color(el.get_str("color", "#50A050"), &pr.color);
                    parse_color(el.get_str("back", "#282828"), &pr.back);
                    out.progress.push_back(pr);
                }
            }
        }
    }

    if (out.texts.empty() && out.progress.empty() && !have_buttons) {
        err = "skin has no usable elements";
        return false;
    }

    out.loaded = true;
    err.clear();
    return true;
}

const SkinButton* skin_hit_test(const Skin& skin, int x, int y) {
    for (const SkinButton& b : skin.buttons)
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h)
            return &b;
    return nullptr;
}

void skin_draw(HDC hdc, const Skin& skin, const Player& player) {
    // фон
    if (skin.bg) {
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, skin.bg);
        if (skin.bg_w == skin.w && skin.bg_h == skin.h) {
            BitBlt(hdc, 0, 0, skin.w, skin.h, mem, 0, 0, SRCCOPY);
        } else {
            StretchBlt(hdc, 0, 0, skin.w, skin.h, mem, 0, 0, skin.bg_w,
                       skin.bg_h, SRCCOPY);
        }
        SelectObject(mem, old);
        DeleteDC(mem);
    } else {
        RECT full{0, 0, skin.w, skin.h};
        SetDCBrushColor(hdc, skin.fallback_bg);
        FillRect(hdc, &full, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }

    // декоративные изображения
    for (const SkinImage& im : skin.images) {
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, im.bmp);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const BOOL ab = AlphaBlend(hdc, im.x, im.y, im.w, im.h, mem, 0, 0,
                                   im.w, im.h, bf);
        if (!ab) g_alphablend_failures++;
        SelectObject(mem, old);
        DeleteDC(mem);
    }

    // тексты
    for (const SkinText& t : skin.texts) {
        wchar_t buf[512] = L"";
        if (t.source == "title") {
            const PlaylistItem* cur = player.current();
            if (cur) {
                const std::string src =
                    cur->title.empty() ? wide_to_utf8(cur->path)
                                       : cur->title;
                const std::wstring w = utf8_to_wide(src);
                wcsncpy(buf, w.c_str(), 511);
            } else {
                wcsncpy(buf, L"—", 511);
            }
        } else if (t.source == "author") {
            const PlaylistItem* cur = player.current();
            if (cur && !cur->author.empty()) {
                const std::wstring w = utf8_to_wide(cur->author);
                wcsncpy(buf, w.c_str(), 511);
            }
        } else if (t.source == "format") {
            const PlaylistItem* cur = player.current();
            if (cur) {
                const std::wstring w = utf8_to_wide(cur->format);
                wcsncpy(buf, w.c_str(), 511);
            }
        } else if (t.source == "position") {
            const std::string s = format_ms(player.position_ms());
            const std::wstring w = utf8_to_wide(s);
            wcsncpy(buf, w.c_str(), 511);
        } else if (t.source == "duration") {
            const uint32_t d = player.duration_ms();
            if (d) {
                const std::wstring w = utf8_to_wide(format_ms(d));
                wcsncpy(buf, w.c_str(), 511);
            }
        } else if (t.source == "state") {
            const PlayState st = player.state();
            wcsncpy(buf, st == PlayState::Playing ? L"PLAYING"
                         : st == PlayState::Paused ? L"PAUSED"
                                                   : L"STOPPED",
                    511);
        }
        if (!buf[0]) continue;

        HFONT font = CreateFontW(
            -t.font_size, 0, 0, 0, t.bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, t.face.c_str());
        HGDIOBJ old_font = SelectObject(hdc, font);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, t.color);
        DrawTextW(hdc, buf, -1, const_cast<RECT*>(&t.r), DT_SINGLELINE |
                                                            DT_LEFT |
                                                            DT_VCENTER);
        SelectObject(hdc, old_font);
        DeleteObject(font);
    }

    // прогресс
    for (const SkinProgress& pr : skin.progress) {
        RECT back = pr.r;
        HBRUSH back_brush = CreateSolidBrush(pr.back);
        FillRect(hdc, &back, back_brush);
        DeleteObject(back_brush);
        const uint32_t dur = player.duration_ms();
        if (dur > 0) {
            const uint32_t pos = std::min(player.position_ms(), dur);
            const double k = double(pos) / double(dur);
            RECT fill = pr.r;
            fill.right = fill.left +
                         LONG(double(pr.r.right - pr.r.left) * k);
            HBRUSH brush = CreateSolidBrush(pr.color);
            FillRect(hdc, &fill, brush);
            DeleteObject(brush);
        }
    }

    // кнопки
    for (const SkinButton& b : skin.buttons) {
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, b.bmp);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const BOOL ab = BitBlt(hdc, b.x, b.y, b.w, b.h, mem, 0, 0, SRCCOPY);
        if (!ab) g_alphablend_failures++;
        SelectObject(mem, old);
        DeleteDC(mem);
    }
}

} // namespace host
