#pragma once
//
// json.hpp — a small JSON value model, serialiser and parser.
//
// The parser is built entirely from parser.hpp combinators: sequencing
// (>>, <<, seq), choice (|), repetition (many1, sep_by), grouping (between)
// and value transformation (map). The grammar is recursive (arrays/objects
// contain values), tied together through a function pointer so the parser
// type stays finite.
//
#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "parser.hpp"

namespace json {

// ---------------------------------------------------------------------------
// The JSON value model.
// ---------------------------------------------------------------------------
struct value;
using array = std::vector<value>;
using object = std::vector<std::pair<std::string, value>>;  // insertion order

struct value {
    std::variant<std::nullptr_t, bool, double, std::string, array, object> data;

    value() : data(nullptr) {}
    value(std::nullptr_t) : data(nullptr) {}
    value(bool b) : data(b) {}
    value(double d) : data(d) {}
    value(std::string s) : data(std::move(s)) {}
    value(array a) : data(std::move(a)) {}
    value(object o) : data(std::move(o)) {}
};

// ---------------------------------------------------------------------------
// Serialisation.
// ---------------------------------------------------------------------------
inline void dump_string(std::ostream& os, const std::string& s) {
    os << '"';
    for (char c : s) {
        switch (c) {
            case '"': os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n"; break;
            case '\t': os << "\\t"; break;
            case '\r': os << "\\r"; break;
            default: os << c;
        }
    }
    os << '"';
}

inline std::ostream& operator<<(std::ostream& os, const value& v) {
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                os << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                os << (x ? "true" : "false");
            } else if constexpr (std::is_same_v<T, double>) {
                os << x;
            } else if constexpr (std::is_same_v<T, std::string>) {
                dump_string(os, x);
            } else if constexpr (std::is_same_v<T, array>) {
                os << '[';
                for (std::size_t i = 0; i < x.size(); ++i)
                    os << (i ? "," : "") << x[i];
                os << ']';
            } else {  // object
                os << '{';
                for (std::size_t i = 0; i < x.size(); ++i) {
                    if (i) os << ',';
                    dump_string(os, x[i].first);
                    os << ':' << x[i].second;
                }
                os << '}';
            }
        },
        v.data);
    return os;
}

// Pretty-print with two-space indentation.
inline void pretty(std::ostream& os, const value& v, int depth = 0) {
    auto pad = [&](int d) {
        for (int i = 0; i < d; ++i) os << "  ";
    };
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, array>) {
                if (x.empty()) {
                    os << "[]";
                    return;
                }
                os << "[\n";
                for (std::size_t i = 0; i < x.size(); ++i) {
                    pad(depth + 1);
                    pretty(os, x[i], depth + 1);
                    os << (i + 1 < x.size() ? ",\n" : "\n");
                }
                pad(depth);
                os << "]";
            } else if constexpr (std::is_same_v<T, object>) {
                if (x.empty()) {
                    os << "{}";
                    return;
                }
                os << "{\n";
                for (std::size_t i = 0; i < x.size(); ++i) {
                    pad(depth + 1);
                    dump_string(os, x[i].first);
                    os << ": ";
                    pretty(os, x[i].second, depth + 1);
                    os << (i + 1 < x.size() ? ",\n" : "\n");
                }
                pad(depth);
                os << "}";
            } else {
                os << value{x};  // scalar: reuse compact form
            }
        },
        v.data);
}

// ---------------------------------------------------------------------------
// The parser. parse_value is forward-declared so arrays/objects can refer to
// it, breaking the recursion through a function pointer.
// ---------------------------------------------------------------------------
inline combo::parse_result<char, value> parse_value(combo::array_view<char> in);

inline const auto jvalue = combo::make_parser(&parse_value);

// A quoted string with the usual JSON escapes (\uXXXX limited to the BMP).
inline const auto jstring_raw = combo::lexeme(combo::make_parser([](auto in) {
    using R = combo::parse_result<char, std::string>;
    std::size_t i = 0, n = in.size();
    if (!(i < n && in[i] == '"')) return R{};
    ++i;
    std::string s;
    while (i < n && in[i] != '"') {
        char c = in[i++];
        if (c != '\\') {
            s.push_back(c);
            continue;
        }
        if (i >= n) return R{};
        char e = in[i++];
        switch (e) {
            case '"': s.push_back('"'); break;
            case '\\': s.push_back('\\'); break;
            case '/': s.push_back('/'); break;
            case 'n': s.push_back('\n'); break;
            case 't': s.push_back('\t'); break;
            case 'r': s.push_back('\r'); break;
            case 'b': s.push_back('\b'); break;
            case 'f': s.push_back('\f'); break;
            case 'u': {
                if (i + 4 > n) return R{};
                int cp = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = in[i++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= h - '0';
                    else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    else return R{};
                }
                if (cp < 0x80) {
                    s.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: return R{};
        }
    }
    if (!(i < n && in[i] == '"')) return R{};
    ++i;
    return R{combo::result<char, std::string>{in.drop(i), std::move(s)}};
}));

inline const auto jstring =
    combo::map(jstring_raw, [](std::string s) { return value{std::move(s)}; });

// A JSON number, scanned then converted via strtod.
inline const auto jnumber = combo::lexeme(combo::make_parser([](auto in) {
    using R = combo::parse_result<char, value>;
    std::size_t i = 0, n = in.size();
    auto is_digit = [&](std::size_t k) {
        return k < n && in[k] >= '0' && in[k] <= '9';
    };
    if (i < n && in[i] == '-') ++i;
    if (!is_digit(i)) return R{};
    while (is_digit(i)) ++i;
    if (i < n && in[i] == '.') {
        ++i;
        if (!is_digit(i)) return R{};
        while (is_digit(i)) ++i;
    }
    if (i < n && (in[i] == 'e' || in[i] == 'E')) {
        ++i;
        if (i < n && (in[i] == '+' || in[i] == '-')) ++i;
        if (!is_digit(i)) return R{};
        while (is_digit(i)) ++i;
    }
    std::string num(in.begin(), in.begin() + i);
    double d = std::strtod(num.c_str(), nullptr);
    return R{combo::result<char, value>{in.drop(i), value{d}}};
}));

inline const auto jbool =
    combo::map(combo::lexeme(combo::literal("true")),
               [](std::string) { return value{true}; }) |
    combo::map(combo::lexeme(combo::literal("false")),
               [](std::string) { return value{false}; });

inline const auto jnull = combo::map(combo::lexeme(combo::literal("null")),
                                     [](std::string) { return value{nullptr}; });

inline const auto jarray = combo::map(
    combo::between(combo::tok('['), combo::sep_by(jvalue, combo::tok(',')),
                   combo::tok(']')),
    [](array a) { return value{std::move(a)}; });

inline const auto jmember = combo::map(
    combo::seq(jstring_raw, combo::tok(':'), jvalue),
    [](std::tuple<std::string, char, value> t) {
        return std::pair<std::string, value>{std::move(std::get<0>(t)),
                                             std::move(std::get<2>(t))};
    });

inline const auto jobject = combo::map(
    combo::between(combo::tok('{'), combo::sep_by(jmember, combo::tok(',')),
                   combo::tok('}')),
    [](object m) { return value{std::move(m)}; });

inline combo::parse_result<char, value> parse_value(combo::array_view<char> in) {
    static const auto p =
        jnull | jbool | jstring | jnumber | jarray | jobject;
    return p(in);
}

// Parse a whole document (trailing whitespace allowed).
inline std::optional<value> parse(std::string_view text) {
    auto p = jvalue << combo::ws;
    return combo::parse(p, text);
}

}  // namespace json
