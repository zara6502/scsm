// Библиотечный плагин ChipDeck для Battle of the Bits (API v1, публичный).
// Query: TOP (по score desc), SEARCH, RANDOM; Fetch: donload_url.
// Скачивание на BotB закрыто для гостей (анти-bandwidth) — поэтому
// плагин поддерживает опциональный логин (email/password в настройках):
// POST /barracks/Login/ -> cookie сессии -> Cookie на всех запросах.
// Сеть через WinHTTP (системный), JSON парсится вручную.
// См. ../../include/chipdeck_api.h и ../../docs/DESIGN.ru.md.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include "chipdeck_api.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kHost[] = L"battleofthebits.com";

void set_error(ChipError* err, uint32_t code, const char* msg) {
    if (!err) return;
    err->code = code;
    err->reserved = 0;
    if (msg) std::snprintf(err->message, sizeof(err->message), "%s", msg);
    else err->message[0] = '\0';
}

std::wstring utf8_to_wide_nt(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()),
                                      nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

std::string wide_to_utf8_nt(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

std::string url_encode_utf8(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        const bool unreserved = (c >= '0' && c <= '9') ||
                                (c >= 'a' && c <= 'z') ||
                                (c >= 'A' && c <= 'Z') ||
                                c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

// ---------- минимальный JSON: значения строковых/числовых полей ----------

std::string json_member(const char* obj, const char* obj_end,
                        const char* key) {
    const size_t klen = std::strlen(key);
    const char* p = obj;
    while (p + klen + 2 < obj_end) {
        if (*p == '"' && std::memcmp(p + 1, key, klen) == 0 &&
            p[klen + 1] == '"') {
            p += klen + 2;
            while (p < obj_end && (*p == ' ' || *p == ':')) ++p;
            if (p >= obj_end) return "";
            if (*p == '"') {
                ++p;
                std::string out;
                while (p < obj_end && *p != '"') {
                    if (*p == '\\' && p + 1 < obj_end) {
                        ++p;
                        switch (*p) {
                        case 'n': out += '\n'; break;
                        case 't': out += '\t'; break;
                        case 'r': out += '\r'; break;
                        default: out += *p; break;
                        }
                    } else {
                        out += *p;
                    }
                    ++p;
                }
                return out;
            }
            if (*p == '-' || (*p >= '0' && *p <= '9')) {
                const char* q = p;
                while (q < obj_end &&
                       (*q == '-' || *q == '.' || (*q >= '0' && *q <= '9') ||
                        *q == 'e' || *q == 'E'))
                    ++q;
                return std::string(p, q);
            }
            return "";
        }
        ++p;
    }
    return "";
}

template <class F>
void json_foreach_object(const char* text, size_t size, F&& fn) {
    size_t i = 0;
    int depth = 0;
    bool in_str = false;
    size_t obj_start = 0;
    while (i < size) {
        const char c = text[i];
        if (in_str) {
            if (c == '\\' && i + 1 < size) ++i;
            else if (c == '"') in_str = false;
        } else {
            if (c == '"') {
                in_str = true;
            } else if (c == '{') {
                if (depth == 1) obj_start = i;
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 1) fn(text + obj_start, text + i + 1);
            } else if (c == '[') {
                ++depth;
            } else if (c == ']') {
                --depth;
                if (depth == 0) break;
            }
        }
        ++i;
    }
}

// ---------- BotB entry -> ChipLibEntry ----------

bool fill_entry(const char* obj, const char* obj_end, ChipLibEntry* e) {
    std::memset(e, 0, sizeof(*e));
    e->struct_size = sizeof(*e);

    const std::string id = json_member(obj, obj_end, "id");
    if (id.empty()) return false;
    std::snprintf(e->entry_id, sizeof(e->entry_id), "botb-%s", id.c_str());

    const std::string title = json_member(obj, obj_end, "title");
    std::snprintf(e->title, sizeof(e->title), "%s", title.c_str());

    const std::string author = json_member(obj, obj_end, "authors_display");
    std::snprintf(e->author, sizeof(e->author), "%s", author.c_str());

    const std::string fmt = json_member(obj, obj_end, "format_token");
    std::snprintf(e->format, sizeof(e->format), "%s", fmt.c_str());

    // Только аудио-записи: medium == 0 (пиксель-арт medium == 1).
    const std::string medium = json_member(obj, obj_end, "medium");
    if (medium != "0") return false;

    // Рейтинг: score BotB ~ 0..50 -> нормируем в 0..1000.
    const std::string score = json_member(obj, obj_end, "score");
    if (!score.empty()) {
        const double v = std::atof(score.c_str());
        double r = v / 50.0 * 1000.0;
        if (r < 0) r = 0;
        if (r > 1000) r = 1000;
        e->rating = uint32_t(r);
    } else {
        e->rating = CHP_LIB_RATING_UNKNOWN;
    }

    const std::string length = json_member(obj, obj_end, "length");
    if (!length.empty()) {
        const double sec = std::atof(length.c_str());
        if (sec > 0) {
            e->duration_ms = uint32_t(sec * 1000.0);
            e->flags |= CHP_ENTRY_HAS_DURATION;
        }
    }

    const std::string dl = json_member(obj, obj_end, "donload_url");
    if (!dl.empty() && dl[0] == '/') {
        std::snprintf(e->download_hint, sizeof(e->download_hint), "%s",
                      dl.c_str());
    }
    return true;
}

// ---------- WinHTTP ----------

struct LibSession {
    HINTERNET internet = nullptr;
    HINTERNET connection = nullptr;
    std::wstring cookie;     // "name=value; ..." после логина
    bool logged_in = false;
};

bool http_get(LibSession* s, const std::string& path_utf8,
              std::vector<uint8_t>* out,
              ChipLibProgressCb progress_cb, void* user_data,
              volatile int* cancel, std::string* err,
              const char* body = nullptr,
              const char* content_type = nullptr,
              std::string* set_cookies = nullptr) {
    const std::wstring path = utf8_to_wide_nt(path_utf8);

    HINTERNET req = WinHttpOpenRequest(
        s->connection, body ? L"POST" : L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        if (err) *err = "WinHttpOpenRequest failed";
        return false;
    }

    bool ok = false;
    do {
        if (cancel && *cancel) {
            if (err) *err = "cancelled";
            break;
        }
        std::wstring headers;
        if (!s->cookie.empty())
            headers += L"Cookie: " + s->cookie + L"\r\n";
        if (content_type)
            headers += L"Content-Type: " + utf8_to_wide_nt(content_type);

        const void* post = body ? body : WINHTTP_NO_REQUEST_DATA;
        const DWORD post_len = body ? DWORD(std::strlen(body)) : 0;
        if (!WinHttpSendRequest(req,
                                headers.empty()
                                    ? WINHTTP_NO_ADDITIONAL_HEADERS
                                    : headers.c_str(),
                                DWORD(headers.size()),
                                const_cast<void*>(post), post_len,
                                post_len, 0) ||
            !WinHttpReceiveResponse(req, nullptr)) {
            if (err) *err = "request failed (WinHTTP)";
            break;
        }

        if (set_cookies) {
            DWORD idx = 0;
            for (;;) {
                wchar_t cbuf[2048];
                DWORD csz = sizeof(cbuf);
                if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_SET_COOKIE,
                                         WINHTTP_HEADER_NAME_BY_INDEX, cbuf,
                                         &csz, &idx))
                    break;
                std::wstring cw(cbuf, csz / sizeof(wchar_t));
                const size_t semi = cw.find(L';');
                if (semi != std::wstring::npos) cw.resize(semi);
                if (!cw.empty()) {
                    if (!set_cookies->empty()) *set_cookies += "; ";
                    *set_cookies += wide_to_utf8_nt(cw);
                }
                ++idx;
            }
        }

        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE |
                                WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                            WINHTTP_NO_HEADER_INDEX);
        if (status != 200 && status != 302) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "HTTP status %lu",
                          static_cast<unsigned long>(status));
            if (err) *err = buf;
            break;
        }

        std::vector<uint8_t> body_out;
        DWORD bytes = 0;
        while (WinHttpQueryDataAvailable(req, &bytes) && bytes > 0) {
            if (cancel && *cancel) {
                if (err) *err = "cancelled";
                break;
            }
            const size_t old = body_out.size();
            body_out.resize(old + bytes);
            DWORD read = 0;
            if (!WinHttpReadData(req, body_out.data() + old, bytes, &read)) {
                if (err) *err = "WinHttpReadData failed";
                break;
            }
            body_out.resize(old + read);
            if (progress_cb) progress_cb(user_data, body_out.size(), 0);
        }

        if (out) *out = std::move(body_out);
        ok = true;
    } while (false);

    WinHttpCloseHandle(req);
    return ok;
}

} // namespace

extern "C" {

CHP_EXPORT uint32_t CHP_CALL ChipLibGetInfo(ChipLibInfo* info) {
    if (!info || info->struct_size < sizeof(*info)) return 0;
    const uint32_t caller_size = info->struct_size;
    std::memset(info, 0, sizeof(*info));
    info->struct_size = caller_size;
    info->api_version = CHP_API_VERSION;
    info->flags = CHP_LIB_SEARCH | CHP_LIB_TOP | CHP_LIB_RATINGS |
                  CHP_LIB_DOWNLOAD | CHP_LIB_RANDOM;
    std::snprintf(info->id, sizeof(info->id), "%s", "botb");
    std::snprintf(info->name, sizeof(info->name), "%s", "Battle of the Bits");
    std::snprintf(info->homepage, sizeof(info->homepage), "%s",
                  "https://battleofthebits.com");
    return 1;
}

// Опциональный логин: BotB закрывает скачивание от гостей.
CHP_EXPORT uint32_t CHP_CALL ChipLibGetConfigSchema(ChipLibConfigSchema* schema) {
    if (!schema || schema->struct_size < sizeof(*schema)) return 0;
    const uint32_t caller_size = schema->struct_size;
    std::memset(schema, 0, sizeof(*schema));
    schema->struct_size = caller_size;
    schema->version = 1u;
    schema->param_count = 2;
    std::snprintf(schema->params[0].id, sizeof(schema->params[0].id), "%s",
                  "email");
    std::snprintf(schema->params[0].label, sizeof(schema->params[0].label),
                  "%s", "BotBr e-mail");
    std::snprintf(schema->params[1].id, sizeof(schema->params[1].id), "%s",
                  "password");
    std::snprintf(schema->params[1].label, sizeof(schema->params[1].label),
                  "%s", "Password");
    schema->params[1].secret = 1;
    return 1;
}

static const ChipLibStringParam* find_param(const ChipLibConnectConfig* cfg,
                                            const char* id) {
    if (!cfg) return nullptr;
    for (uint32_t i = 0; i < cfg->param_count; ++i)
        if (std::strcmp(cfg->params[i].id, id) == 0) return &cfg->params[i];
    return nullptr;
}

CHP_EXPORT uint32_t CHP_CALL ChipLibConnect(const ChipLibConnectConfig* config,
                                            ChipHandle* out, ChipError* err) {
    if (!out) return 0;
    *out = nullptr;

    LibSession* s = new LibSession();
    s->internet = WinHttpOpen(L"ChipDeck/1.0 (BotB library plugin)",
                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                              0);
    if (!s->internet) {
        set_error(err, CHP_ERR_NETWORK, "WinHttpOpen failed");
        delete s;
        return 0;
    }
    s->connection =
        WinHttpConnect(s->internet, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!s->connection) {
        set_error(err, CHP_ERR_NETWORK, "WinHttpConnect failed");
        WinHttpCloseHandle(s->internet);
        delete s;
        return 0;
    }

    // Опциональный логин: POST /barracks/Login/ -> cookie сессии.
    const ChipLibStringParam* email = find_param(config, "email");
    const ChipLibStringParam* pass = find_param(config, "password");
    if (email && pass && email->value[0] && pass->value[0]) {
        const std::string boundary = "----ChipDeckBotB7f3a";
        std::string body;
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"email\"\r\n\r\n";
        body += email->value;
        body += "\r\n--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"password\"\r\n\r\n";
        body += pass->value;
        body += "\r\n--" + boundary + "\r\n";
        body +=
            "Content-Disposition: form-data; name=\"submitok\"\r\n\r\n"
            "LOGIN\r\n";
        body += "--" + boundary + "--\r\n";
        const std::string ctype = "multipart/form-data; boundary=" + boundary;

        std::vector<uint8_t> resp;
        std::string cookies, lerr;
        http_get(s, "/barracks/Login/", &resp, nullptr, nullptr, nullptr,
                 &lerr, body.c_str(), ctype.c_str(), &cookies);
        if (cookies.empty()) {
            set_error(err, CHP_ERR_AUTH, "login failed (no session cookie)");
            WinHttpCloseHandle(s->connection);
            WinHttpCloseHandle(s->internet);
            delete s;
            return 0;
        }
        s->cookie = utf8_to_wide_nt(cookies);
        s->logged_in = true;
    }

    *out = s;
    return 1;
}

CHP_EXPORT void CHP_CALL ChipLibDisconnect(ChipHandle handle) {
    LibSession* s = static_cast<LibSession*>(handle);
    if (!s) return;
    if (s->connection) WinHttpCloseHandle(s->connection);
    if (s->internet) WinHttpCloseHandle(s->internet);
    delete s;
}

CHP_EXPORT uint32_t CHP_CALL ChipLibQuery(ChipHandle handle,
                                          const ChipLibRequest* request,
                                          ChipLibResultCb result_cb,
                                          void* user_data,
                                          volatile int* cancel,
                                          ChipError* err) {
    LibSession* s = static_cast<LibSession*>(handle);
    if (!s || !request || !result_cb) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad args");
        return 0;
    }

    // случай: random может вернуть и пиксель-арт, до 5 попыток на аудио
    if (request->query_type == CHP_QUERY_RANDOM) {
        for (int attempt = 0; attempt < 5 && !*cancel; ++attempt) {
            std::vector<uint8_t> body;
            std::string http_err;
            if (!http_get(s, "/api/v1/entry/random", &body, nullptr, nullptr,
                          cancel, &http_err)) {
                set_error(err, CHP_ERR_NETWORK, http_err.c_str());
                return 0;
            }
            uint32_t emitted = 0;
            const char* text = reinterpret_cast<const char*>(body.data());
            json_foreach_object(
                text, body.size(), [&](const char* o, const char* e) {
                    if (*cancel) return;
                    ChipLibEntry entry;
                    if (!fill_entry(o, e, &entry)) return;
                    if (result_cb(user_data, &entry))
                        *const_cast<volatile int*>(cancel) = 1;
                    ++emitted;
                });
            if (emitted > 0) return 1;
        }
        set_error(err, CHP_ERR_NOT_FOUND, "no audio entry in 5 randoms");
        return 0;
    }

    std::string path;
    switch (request->query_type) {
    case CHP_QUERY_TOP:
        path = "/api/v1/entry/list/0/" +
               std::to_string(request->limit ? request->limit : 25) +
               "?sort=score&desc=true&filters=medium~0";
        break;
    case CHP_QUERY_SEARCH: {
        const char* q = request->text ? request->text : "";
        path = "/api/v1/entry/search/" + url_encode_utf8(q) + "/0/" +
               std::to_string(request->limit ? request->limit : 25);
        break;
    }
    default:
        set_error(err, CHP_ERR_UNSUPPORTED, "query type not supported");
        return 0;
    }

    std::vector<uint8_t> body;
    std::string http_err;
    if (!http_get(s, path, &body, nullptr, nullptr, cancel, &http_err)) {
        set_error(err, *cancel ? CHP_ERR_CANCELLED : CHP_ERR_NETWORK,
                  http_err.c_str());
        return 0;
    }

    uint32_t emitted = 0;
    const char* text = reinterpret_cast<const char*>(body.data());
    json_foreach_object(text, body.size(), [&](const char* o, const char* e) {
        if (*cancel) return;
        ChipLibEntry entry;
        if (!fill_entry(o, e, &entry)) return;
        if (result_cb(user_data, &entry)) {
            *const_cast<volatile int*>(cancel) = 1;
        }
        ++emitted;
    });

    if (emitted == 0 && !*cancel) {
        set_error(err, CHP_ERR_NOT_FOUND, "no audio entries in response");
        return 0;
    }
    return 1;
}

CHP_EXPORT uint32_t CHP_CALL ChipLibFetch(ChipHandle handle,
                                          const ChipLibEntry* entry,
                                          ChipLibProgressCb progress_cb,
                                          void* user_data,
                                          volatile int* cancel,
                                          uint8_t** data, size_t* size,
                                          ChipError* err) {
    LibSession* s = static_cast<LibSession*>(handle);
    if (!s || !entry || !data || !size) {
        set_error(err, CHP_ERR_INVALID_ARG, "bad args");
        return 0;
    }
    *data = nullptr;
    *size = 0;

    const char* dl = entry->download_hint;
    if (!dl || dl[0] != '/') {
        set_error(err, CHP_ERR_NOT_FOUND, "entry has no download URL");
        return 0;
    }
    if (!s->logged_in) {
        set_error(err, CHP_ERR_AUTH,
                  "BotB requires login for downloads; configure "
                  "email/password in plugin settings");
        return 0;
    }

    std::vector<uint8_t> body;
    std::string http_err;
    if (!http_get(s, dl, &body, progress_cb, user_data, cancel, &http_err)) {
        set_error(err, *cancel ? CHP_ERR_CANCELLED : CHP_ERR_NETWORK,
                  http_err.c_str());
        return 0;
    }
    if (body.empty()) {
        set_error(err, CHP_ERR_NETWORK, "empty response");
        return 0;
    }

    uint8_t* buf = static_cast<uint8_t*>(std::malloc(body.size()));
    if (!buf) {
        set_error(err, CHP_ERR_OUT_OF_MEMORY, "out of memory");
        return 0;
    }
    std::memcpy(buf, body.data(), body.size());
    *data = buf;
    *size = body.size();
    return 1;
}

CHP_EXPORT void CHP_CALL ChipLibFreeBuffer(ChipHandle handle, uint8_t* data) {
    (void)handle;
    std::free(data);
}

} // extern "C"
