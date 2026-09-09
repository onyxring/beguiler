#pragma once

#include <string_view>
#include <algorithm>
#include <functional>
#include <string>
#include <type_traits>

// Feature-test for std::format. <version> is where __cpp_lib_format is guaranteed to appear;
// pull it in when the toolchain has it so the guard below is reliable across standard libraries
// (some only define __cpp_lib_format after <version>/<format>, not via <string>).
#ifdef __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif

using namespace std;

// ─── format() ────────────────────────────────────────────────────────────────
// A stand-in for std::format covering exactly what this codebase uses: positional
// `{N}` and sequential `{}` substitution, no format specifiers (there is not one
// `{N:...}` in the tree). <format> is otherwise the only C++20 library feature
// here, and it is the newest one — requiring GCC 13 / LLVM 17 — so implementing
// these few lines lowers the toolchain bar to plain C++17.
//
// `{{` and `}}` escape to a literal brace, as in std::format.
namespace bglfmt {
    inline string toText(const string& v){ return v; }
    inline string toText(string_view v)  { return string(v); }
    inline string toText(const char* v)  { return v ? string(v) : string(); }
    inline string toText(char v)         { return string(1, v); }
    inline string toText(bool v)         { return v ? "true" : "false"; }
    template<typename T>
    inline enable_if_t<is_arithmetic_v<decay_t<T>> && !is_same_v<decay_t<T>, char>
                       && !is_same_v<decay_t<T>, bool>, string>
    toText(T v){ return to_string(v); }
    // Anything with a conversion to std::string (e.g. token) formats through it.
    template<typename T>
    inline enable_if_t<!is_arithmetic_v<decay_t<T>> && is_convertible_v<T, string>, string>
    toText(const T& v){ return string(v); }
}

// When the standard library provides std::format (C++20+), use it: `using namespace std` above
// brings it into scope, and it is a byte-identical drop-in for every call here (all format strings
// are compile-time literals; the full test suite passes unchanged either way). The shim is then
// compiled out, so the two `format`s never collide under `using namespace std` — the build works at
// any -std, old or new. On toolchains without <format>, the shim below keeps the bar at C++17.
#ifndef __cpp_lib_format
template<typename... Args>
inline string format(string_view pattern, Args&&... args){
    const string parts[] = { bglfmt::toText(std::forward<Args>(args))..., string() };
    const size_t count = sizeof...(Args);
    string out;
    size_t next = 0;                       // for the sequential `{}` form
    for(size_t i = 0; i < pattern.size(); i++){
        char c = pattern[i];
        if(c == '{'){
            if(i + 1 < pattern.size() && pattern[i+1] == '{'){ out += '{'; i++; continue; }
            size_t close = pattern.find('}', i + 1);
            if(close == string_view::npos){ out += c; continue; }   // unterminated: literal
            string_view spec = pattern.substr(i + 1, close - i - 1);
            size_t idx;
            if(spec.empty()) idx = next++;
            else {
                idx = 0;
                for(char d : spec){
                    if(d < '0' || d > '9'){ idx = string_view::npos; break; }
                    idx = idx * 10 + size_t(d - '0');
                }
            }
            if(idx == string_view::npos){ out += c; continue; }     // not an index: literal
            if(idx < count) out += parts[idx];
            i = close;
            continue;
        }
        if(c == '}' && i + 1 < pattern.size() && pattern[i+1] == '}'){ out += '}'; i++; continue; }
        out += c;
    }
    return out;
}
#endif  // __cpp_lib_format

constexpr size_t chk(string_view str) {
    const long long p = 131;
    const long long m = 4294967291; // 2^32 - 5, largest 32 bit prime
    long long total = 0;
    long long current_multiplier = 1;
    // Bound by size(), not by a '\0' sentinel: a string_view is not null-terminated, so
    // reading str[str.size()] is out of bounds. libc++ happened to tolerate it; libstdc++
    // has a bounds assert in operator[] that is not constexpr-callable, which turned this
    // into ~28 compile errors on GCC. The hash value is unchanged — the terminator was
    // never folded in.
    for (size_t i = 0; i < str.size(); ++i){
        total = (total + current_multiplier * str[i]) % m;
        current_multiplier = (current_multiplier * p) % m;
    }
    return total;
}