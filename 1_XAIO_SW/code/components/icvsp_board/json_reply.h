#pragma once
// A small, strict JSON reader for the backend's short replies (ACKs and error objects). No allocation.
#include <cstddef>

namespace icvsp {

// Reads the whole text as one JSON object and copies the string value of the top-level member `key` into
// out (UTF-8, NUL-terminated). Returns false when the text is not exactly one valid JSON object (truncated,
// trailing garbage, bad escapes or numbers, nested deeper than 16 levels), when the key is missing, appears
// more than once at the top level, is not a string, or its value does not fit in cap - 1 bytes.
// Members inside nested objects or arrays and text inside strings never match.
bool json_top_string(const char* json, const char* key, char* out, size_t cap);

}  // namespace icvsp
