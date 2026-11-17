// Минимальный JSON-парсер для skin.json (RFC 8259 subset).
// Строки UTF-8; числа double; объекты сохраняют порядок ключей.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace host {

struct JsonValue {
    enum Type { Null, Bool, Number, Str, Array, Object };
    Type type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> members;

    const JsonValue* find(const char* key) const {
        if (type != Object) return nullptr;
        for (const auto& kv : members)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string get_str(const char* key, const char* def = "") const {
        const JsonValue* v = find(key);
        return v && v->type == Str ? v->str : def;
    }
    double get_num(const char* key, double def = 0) const {
        const JsonValue* v = find(key);
        return v && v->type == Number ? v->num : def;
    }
    int get_int(const char* key, int def = 0) const {
        return int(get_num(key, def));
    }
};

class JsonParser {
public:
    static bool parse(const char* s, size_t n, JsonValue& out,
                      std::string& err) {
        JsonParser p(s, n);
        p.skip_ws();
        if (!p.value(out, 0)) {
            err = p.err_;
            return false;
        }
        p.skip_ws();
        if (p.pos_ != p.n_) {
            err = "trailing data";
            return false;
        }
        return true;
    }

private:
    JsonParser(const char* s, size_t n) : s_(s), n_(n) {}
    const char* s_;
    size_t n_;
    size_t pos_ = 0;
    std::string err_;

    void fail(const char* m) {
        if (err_.empty()) err_ = m;
    }
    void skip_ws() {
        while (pos_ < n_ &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' ||
                s_[pos_] == '\n'))
            ++pos_;
    }
    bool peek(char c) { return pos_ < n_ && s_[pos_] == c; }
    bool eat(char c) {
        if (peek(c)) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool value(JsonValue& out, int depth) {
        if (depth > 32) {
            fail("too deep");
            return false;
        }
        skip_ws();
        if (pos_ >= n_) {
            fail("unexpected end");
            return false;
        }
        const char c = s_[pos_];
        if (c == '{') return object(out, depth + 1);
        if (c == '[') return array(out, depth + 1);
        if (c == '"') {
            out.type = JsonValue::Str;
            return string(out.str);
        }
        if (c == 't' || c == 'f') {
            out.type = JsonValue::Bool;
            out.b = (c == 't');
            const char* lit = c == 't' ? "true" : "false";
            if (pos_ + 4 <= n_ && std::memcmp(s_ + pos_, lit, 4) == 0) {
                pos_ += 4;
                return true;
            }
            fail("bad literal");
            return false;
        }
        if (c == 'n') {
            if (pos_ + 4 <= n_ && std::memcmp(s_ + pos_, "null", 4) == 0) {
                pos_ += 4;
                out.type = JsonValue::Null;
                return true;
            }
            fail("bad null");
            return false;
        }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        fail("unexpected character");
        return false;
    }

    bool object(JsonValue& out, int depth) {
        out.type = JsonValue::Object;
        if (!eat('{')) {
            fail("expected '{'");
            return false;
        }
        skip_ws();
        if (eat('}')) return true;
        for (;;) {
            skip_ws();
            std::string key;
            if (!string(key)) return false;
            skip_ws();
            if (!eat(':')) {
                fail("expected ':'");
                return false;
            }
            JsonValue v;
            if (!value(v, depth)) return false;
            out.members.emplace_back(std::move(key), std::move(v));
            skip_ws();
            if (eat(',')) continue;
            if (eat('}')) return true;
            fail("expected ',' or '}'");
            return false;
        }
    }

    bool array(JsonValue& out, int depth) {
        out.type = JsonValue::Array;
        if (!eat('[')) {
            fail("expected '['");
            return false;
        }
        skip_ws();
        if (eat(']')) return true;
        for (;;) {
            JsonValue v;
            if (!value(v, depth)) return false;
            out.arr.push_back(std::move(v));
            skip_ws();
            if (eat(',')) continue;
            if (eat(']')) return true;
            fail("expected ',' or ']'");
            return false;
        }
    }

    bool string(std::string& out) {
        if (!eat('"')) {
            fail("expected string");
            return false;
        }
        while (pos_ < n_) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= n_) break;
            const char e = s_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                // \uXXXX — только BMP, суррогаты складываем без декода
                if (pos_ + 4 > n_) {
                    fail("bad \\u escape");
                    return false;
                }
                char hex[5] = {s_[pos_], s_[pos_ + 1], s_[pos_ + 2],
                               s_[pos_ + 3], 0};
                pos_ += 4;
                const unsigned cp =
                    static_cast<unsigned>(std::strtoul(hex, nullptr, 16));
                if (cp < 0x80) {
                    out += char(cp);
                } else if (cp < 0x800) {
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                } else {
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
                break;
            }
            default:
                fail("bad escape");
                return false;
            }
        }
        fail("unterminated string");
        return false;
    }

    bool number(JsonValue& out) {
        const size_t start = pos_;
        if (peek('-')) ++pos_;
        while (pos_ < n_ &&
               ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' ||
                s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' ||
                s_[pos_] == '-'))
            ++pos_;
        if (pos_ == start) {
            fail("bad number");
            return false;
        }
        out.type = JsonValue::Number;
        out.num = std::strtod(std::string(s_ + start, pos_ - start).c_str(),
                              nullptr);
        return true;
    }
};

inline bool json_parse_file(const wchar_t* path, JsonValue& out,
                            std::string& err) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "cannot open file";
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 ||
        sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(h);
        err = "bad file size";
        return false;
    }
    std::string buf(size_t(sz.QuadPart), '\0');
    DWORD got = 0;
    const bool ok = ReadFile(h, buf.data(), DWORD(buf.size()), &got, nullptr) &&
                    got == buf.size();
    CloseHandle(h);
    if (!ok) {
        err = "read failed";
        return false;
    }
    // UTF-8 BOM
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        off = 3;
    return JsonParser::parse(buf.data() + off, buf.size() - off, out, err);
}

} // namespace host
