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

    // Checks one raw UTF-8 sequence starting at p (first byte already known to be >= 0x80) and moves past it.
    // Rejects stray continuation bytes, overlong forms, surrogates (U+D800..DFFF) and anything above U+10FFFF.
    bool utf8(char* out, size_t cap, size_t& n, bool* fits) {
        const unsigned char b0 = static_cast<unsigned char>(p[0]);
        int len;
        unsigned char lo = 0x80, hi = 0xBF;  // allowed range of the second byte
        if (b0 >= 0xC2 && b0 <= 0xDF) len = 2;
        else if (b0 == 0xE0) { len = 3; lo = 0xA0; }
        else if (b0 >= 0xE1 && b0 <= 0xEC) len = 3;
        else if (b0 == 0xED) { len = 3; hi = 0x9F; }
        else if (b0 >= 0xEE && b0 <= 0xEF) len = 3;
        else if (b0 == 0xF0) { len = 4; lo = 0x90; }
        else if (b0 >= 0xF1 && b0 <= 0xF3) len = 4;
        else if (b0 == 0xF4) { len = 4; hi = 0x8F; }
        else return false;
        for (int i = 1; i < len; ++i) {
            const unsigned char b = static_cast<unsigned char>(p[i]);
            if (i == 1 ? (b < lo || b > hi) : (b < 0x80 || b > 0xBF)) return false;  // also stops at '\0'
        }
        for (int i = 0; i < len; ++i) put(out, cap, n, fits, p[i]);
        p += len;
        return true;
    }

    // Stores one decoded byte; with match != nullptr it compares against match instead of storing.
    const char* match = nullptr;
    bool matching = true;
    void put(char* out, size_t cap, size_t& n, bool* fits, char c) {
        if (match != nullptr) {
            if (matching && match[n] == c) ++n;
            else matching = false;
            return;
        }
        if (out == nullptr) return;
        if (n + 1 < cap) out[n++] = c;
        else *fits = false;
    }

    // Reads a string at p (which is on the opening quote). With out != nullptr the decoded UTF-8 text is
    // stored; *fits turns false when it is longer than cap - 1 bytes. With cmp != nullptr nothing is stored and
    // *equal tells whether the decoded text is exactly cmp. A decoded NUL (\u0000) is refused: every caller
    // uses C strings, so it would silently cut the value short ("stored\u0000junk" would read as "stored").
    bool string(char* out, size_t cap, bool* fits, const char* cmp = nullptr, bool* equal = nullptr) {
        if (*p != '"') return false;
        ++p;
        size_t n = 0;
        match = cmp;
        matching = true;
        for (;;) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c == '\0' || c < 0x20) return false;  // cut off, or a raw control character
            if (c >= 0x80) {
                if (!utf8(out, cap, n, fits)) return false;
                continue;
            }
            ++p;
            if (c == '"') break;
            if (c != '\\') {
                put(out, cap, n, fits, static_cast<char>(c));
                continue;
            }
            const char e = *p++;
            switch (e) {
                case '"': put(out, cap, n, fits, '"'); break;
                case '\\': put(out, cap, n, fits, '\\'); break;
                case '/': put(out, cap, n, fits, '/'); break;
                case 'b': put(out, cap, n, fits, '\b'); break;
                case 'f': put(out, cap, n, fits, '\f'); break;
                case 'n': put(out, cap, n, fits, '\n'); break;
                case 'r': put(out, cap, n, fits, '\r'); break;
                case 't': put(out, cap, n, fits, '\t'); break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp) || cp == 0) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {  // surrogate pair
                        uint32_t lo;
                        if (p[0] != '\\' || p[1] != 'u') return false;
                        p += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return false;
                    }
                    char u[4];
                    int k;
                    if (cp < 0x80) { u[0] = static_cast<char>(cp); k = 1; }
                    else if (cp < 0x800) { u[0] = static_cast<char>(0xC0 | cp >> 6); u[1] = static_cast<char>(0x80 | (cp & 0x3F)); k = 2; }
                    else if (cp < 0x10000) {
                        u[0] = static_cast<char>(0xE0 | cp >> 12); u[1] = static_cast<char>(0x80 | (cp >> 6 & 0x3F));
                        u[2] = static_cast<char>(0x80 | (cp & 0x3F)); k = 3;
                    } else {
                        u[0] = static_cast<char>(0xF0 | cp >> 18); u[1] = static_cast<char>(0x80 | (cp >> 12 & 0x3F));
                        u[2] = static_cast<char>(0x80 | (cp >> 6 & 0x3F)); u[3] = static_cast<char>(0x80 | (cp & 0x3F)); k = 4;
                    }
                    for (int i = 0; i < k; ++i) put(out, cap, n, fits, u[i]);
                    break;
                }
                default: return false;
            }
        }
        if (cmp != nullptr) {
            *equal = matching && cmp[n] == '\0';
            match = nullptr;
            return true;
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
            bool is_key = false;  // key names are compared while decoding: no length limit of their own
            if (!(key != nullptr ? string(nullptr, 0, nullptr, key, &is_key) : string(nullptr, 0, nullptr))) return false;
            ws();
            if (*p != ':') return false;
            ++p;
            ws();
            if (is_key) {
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
