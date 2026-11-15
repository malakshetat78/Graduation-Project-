#include "json_reply.h"

#include <cstdint>
#include <cstring>

namespace icvsp {

namespace {

constexpr int kMaxDepth = 16;

struct Reader {
    const char* p;

    void ws() {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    }

    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    bool hex4(uint32_t& v) {
        v = 0;
        for (int i = 0; i < 4; ++i) {
            const int h = hex(p[i]);
            if (h < 0) return false;
            v = v << 4 | static_cast<uint32_t>(h);
        }
        p += 4;
        return true;
    }

    // Reads a string at p (which is on the opening quote). With out != nullptr the decoded UTF-8 text is
    // stored; *fits turns false when it is longer than cap - 1 bytes.
    bool string(char* out, size_t cap, bool* fits) {
        if (*p != '"') return false;
        ++p;
        size_t n = 0;
        auto put = [&](char c) {
            if (out == nullptr) return;
            if (n + 1 < cap) out[n++] = c;
            else *fits = false;
        };
        for (;;) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '\0' || c < 0x20) return false;  // cut off, or a raw control character
            ++p;
            if (c == '"') break;
            if (c != '\\') {
                put(static_cast<char>(c));
                continue;
            }
            const char e = *p++;
            switch (e) {
                case '"': put('"'); break;
                case '\\': put('\\'); break;
                case '/': put('/'); break;
                case 'b': put('\b'); break;
                case 'f': put('\f'); break;
                case 'n': put('\n'); break;
                case 'r': put('\r'); break;
                case 't': put('\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
                        uint32_t lo;
                        if (p[0] != '\\' || p[1] != 'u') return false;
                        p += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    if (cp < 0x80) {
                        put(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        put(static_cast<char>(0xC0 | cp >> 6));
                        put(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else if (cp < 0x10000) {
                        put(static_cast<char>(0xE0 | cp >> 12));
                        put(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
                        put(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        put(static_cast<char>(0xF0 | cp >> 18));
                        put(static_cast<char>(0x80 | (cp >> 12 & 0x3F)));
                        put(static_cast<char>(0x80 | (cp >> 6 & 0x3F)));
                        put(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return false;
            }
        }
        if (out != nullptr && cap > 0) out[n < cap ? n : cap - 1] = '\0';
        return true;
    }

    bool digits() {
        if (*p < '0' || *p > '9') return false;
        while (*p >= '0' && *p <= '9') ++p;
        return true;
    }

    bool number() {
        if (*p == '-') ++p;
        if (*p == '0') ++p;
        else if (!digits()) return false;
        if (*p == '.') {
            ++p;
            if (!digits()) return false;
        }
        if (*p == 'e' || *p == 'E') {
            ++p;
            if (*p == '+' || *p == '-') ++p;
            if (!digits()) return false;
        }
        return !(*p >= '0' && *p <= '9');  // "01" is not a number
    }

    bool literal(const char* word) {
        const size_t n = std::strlen(word);
        if (std::strncmp(p, word, n) != 0) return false;
        p += n;
        return true;
    }

    bool value(int depth) {
        ws();
        switch (*p) {
            case '"': return string(nullptr, 0, nullptr);
            case '{': return object(depth + 1, nullptr, nullptr, 0, nullptr);
            case '[': return array(depth + 1);
            case 't': return literal("true");
            case 'f': return literal("false");
            case 'n': return literal("null");
            default: return number();
        }
    }

    bool array(int depth) {
        if (depth > kMaxDepth) return false;
        ++p;  // '['
        ws();
        if (*p == ']') {
            ++p;
            return true;
        }
        for (;;) {
            if (!value(depth)) return false;
            ws();
            if (*p == ']') {
                ++p;
                return true;
            }
            if (*p != ',') return false;
            ++p;
        }
    }

    // Reads an object. When key != nullptr (top level only), the string value of that member is copied out;
    // *found counts how often the key appears.
    bool object(int depth, const char* key, char* out, size_t cap, int* found) {
        if (depth > kMaxDepth || *p != '{') return false;
        ++p;
        ws();
        if (*p == '}') {
            ++p;
            return true;
        }
        for (;;) {
            ws();
            char name[32];
            bool name_fits = true;
            if (!string(name, sizeof name, &name_fits)) return false;
            ws();
            if (*p != ':') return false;
            ++p;
            ws();
            if (key != nullptr && name_fits && std::strcmp(name, key) == 0) {
                ++*found;
                bool fits = true;
                if (*p == '"') {
                    if (!string(out, cap, &fits)) return false;
                    if (!fits) *found = -1000;  // too long: never a match
                } else {
                    if (!value(depth)) return false;
                    *found = -1000;  // not a string
                }
            } else if (!value(depth)) {
                return false;
            }
            ws();
            if (*p == '}') {
                ++p;
                return true;
            }
            if (*p != ',') return false;
            ++p;
        }
    }
};

}  // namespace

bool json_top_string(const char* json, const char* key, char* out, size_t cap) {
    if (json == nullptr || key == nullptr || out == nullptr || cap == 0) return false;
    out[0] = '\0';
    Reader r{json};
    r.ws();
    int found = 0;
    if (!r.object(1, key, out, cap, &found)) {
        out[0] = '\0';
        return false;
    }
    r.ws();
    if (*r.p != '\0' || found != 1) {
        out[0] = '\0';
        return false;
    }
    return true;
}

}  // namespace icvsp
