#pragma once

#include <string_view>
#include <algorithm>
#include <functional>
#include <string>
#include <type_traits>

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

constexpr size_t chk(string_view str) {
    const long long p = 131;
    const long long m = 4294967291; // 2^32 - 5, largest 32 bit prime
    long long total = 0;
    long long current_multiplier = 1;
    for (int i = 0; str[i] != '\0'; ++i){
        total = (total + current_multiplier * str[i]) % m;
        current_multiplier = (current_multiplier * p) % m;
    }
    return total;
}