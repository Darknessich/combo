#pragma once
//
// json.hpp — a small JSON value model, serialiser and parser.
//
// The parser is built entirely from parser.hpp combinators: sequencing
// (>>, <<, seq), choice (|), repetition (many1, sep_by), grouping (between)
// and value transformation (map). The grammar is recursive (arrays/objects
// contain values), tied together through a function pointer so the parser
// type stays finite. Failures carry a positioned combo::error.
//
#include <cstdlib>
#include <expected>
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

// --- string, expressed as grammar -----------------------------------------
//
//   string    = '"' (escape | unescaped)* '"'
//   escape    = '\' ( '"' | '\' | '/' | 'n' | 't' | 'r' | 'b' | 'f'
//                    | 'u' hex hex hex hex )
//   unescaped = any char except '"' and '\'

// Encode a Unicode code point (BMP only) as UTF-8.
inline std::string utf8_encode(int cp) {
    std::string s;
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
    return s;
}

inline const auto hex_digit = combo::satisfy(
    [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    },
    "hex digit");

inline const auto unicode_escape = combo::map(
    combo::sym('u') >> combo::count(4, hex_digit), [](std::vector<char> hs) {
        int cp = 0;
        for (char h : hs)
            cp = cp * 16 + (h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10);
        return utf8_encode(cp);
    });

// A single escape char (after the backslash) mapped to its decoded text.
inline auto escaped(char c, std::string decoded) {
    return combo::map(combo::sym(c),
                      [decoded = std::move(decoded)](char) { return decoded; });
}

inline const auto escape =
    combo::sym('\\') >>
    combo::label(escaped('"', "\"") | escaped('\\', "\\") | escaped('/', "/") |
                     escaped('n', "\n") | escaped('t', "\t") |
                     escaped('r', "\r") | escaped('b', "\b") |
                     escaped('f', "\f") | unicode_escape,
                 "escape sequence");

inline const auto unescaped = combo::stringify(combo::satisfy(
    [](char c) { return c != '"' && c != '\\'; }, "string character"));

// A bare string literal (no leading-whitespace handling; callers add lexeme).
inline const auto jstring_raw =
    combo::between(combo::sym('"'),
                   combo::stringify(combo::many(escape | unescaped)),
                   combo::sym('"'));

inline const auto jstring =
    combo::map(jstring_raw, [](std::string s) { return value{std::move(s)}; });

// --- number, expressed as grammar -----------------------------------------
//
//   number = '-'? digit+ ('.' digit+)? ([eE] [+-]? digit+)?

inline const auto digit =
    combo::satisfy([](char c) { return c >= '0' && c <= '9'; }, "digit");
inline const auto digits = combo::many1(digit);

inline const auto int_part = combo::cat(combo::opt(combo::sym('-')), digits);
inline const auto frac_part =
    combo::cat(combo::sym('.'), digits) | combo::pure(std::string{});
inline const auto exp_part =
    combo::cat(combo::one_of("eE"), combo::opt(combo::one_of("+-")), digits) |
    combo::pure(std::string{});

inline const auto jnumber =
    combo::map(combo::cat(int_part, frac_part, exp_part), [](std::string s) {
        return value{std::strtod(s.c_str(), nullptr)};
    });

inline const auto jbool =
    combo::map(combo::literal("true"),
               [](std::string) { return value{true}; }) |
    combo::map(combo::literal("false"),
               [](std::string) { return value{false}; });

inline const auto jnull = combo::map(combo::literal("null"),
                                     [](std::string) { return value{nullptr}; });

inline const auto jarray = combo::map(
    combo::between(combo::tok('['), combo::sep_by(jvalue, combo::tok(',')),
                   combo::tok(']')),
    [](array a) { return value{std::move(a)}; });

inline const auto jmember = combo::map(
    combo::seq(combo::lexeme(jstring_raw), combo::tok(':'), jvalue),
    [](std::tuple<std::string, char, value> t) {
        return std::pair<std::string, value>{std::move(std::get<0>(t)),
                                             std::move(std::get<2>(t))};
    });

inline const auto jobject = combo::map(
    combo::between(combo::tok('{'), combo::sep_by(jmember, combo::tok(',')),
                   combo::tok('}')),
    [](object m) { return value{std::move(m)}; });

inline combo::parse_result<char, value> parse_value(combo::array_view<char> in) {
    // Skip whitespace once, then commit to one of the value alternatives.
    static const auto p = combo::ws >> combo::label(
        jnull | jbool | jstring | jnumber | jarray | jobject, "value");
    return p(in);
}

// Parse a whole document (trailing whitespace allowed). Yields the value or a
// positioned combo::error.
inline std::expected<value, combo::error> parse(std::string_view text) {
    auto p = jvalue << combo::ws;
    return combo::parse(p, text);
}

}  // namespace json
