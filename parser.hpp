#pragma once
//
// parser.hpp — a tiny header-only parser combinator library (C++23).
//
// A parser CONSUMES a view of tokens and either succeeds with a value plus the
// unconsumed rest of the input, or fails with a positioned error:
//
//     parser : array_view<Token> -> std::expected<result<Token, R>, error>
//
// Combinators build bigger parsers out of smaller ones. See gen.hpp for the
// dual construction (generators that PRODUCE output from the same algebra).
//
#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace combo {

// ---------------------------------------------------------------------------
// array_view: a non-owning window onto a contiguous range of tokens. It also
// carries its absolute offset from the start of the original input, so a parser
// always knows *where* it is — which is what positioned errors need.
// ---------------------------------------------------------------------------
template <class T>
class array_view {
public:
    using value_type = T;

    constexpr array_view() noexcept = default;
    constexpr array_view(const T* data, std::size_t size,
                         std::size_t pos = 0) noexcept
        : data_(data), size_(size), pos_(pos) {}

    // Implicit view of any contiguous container (vector, string, string_view,
    // std::array, ...) whose data() yields something convertible to const T*.
    template <class C,
              class = std::enable_if_t<std::is_convertible_v<
                  decltype(std::declval<const C&>().data()), const T*>>>
    constexpr array_view(const C& c) noexcept
        : data_(c.data()), size_(c.size()), pos_(0) {}

    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr std::size_t pos() const noexcept { return pos_; }  // offset from origin
    constexpr const T& operator[](std::size_t i) const noexcept { return data_[i]; }
    constexpr const T& front() const noexcept { return data_[0]; }

    // Drop the first n tokens (clamped), advancing the absolute offset.
    constexpr array_view drop(std::size_t n) const noexcept {
        n = n < size_ ? n : size_;
        return array_view(data_ + n, size_ - n, pos_ + n);
    }

    constexpr const T* begin() const noexcept { return data_; }
    constexpr const T* end() const noexcept { return data_ + size_; }

private:
    const T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t pos_ = 0;
};

// Convenience: build a char view from a string-like value.
inline array_view<char> view(std::string_view s) noexcept {
    return {s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// error: why and where a parse failed. `pos` is filled by combinators; `line`
// and `col` (1-based) are computed by the top-level runner from the input.
// ---------------------------------------------------------------------------
struct error {
    std::size_t pos = 0;
    std::size_t line = 0;
    std::size_t col = 0;
    std::string msg;
};

inline std::unexpected<error> err(std::size_t pos, std::string msg) {
    return std::unexpected(error{pos, 0, 0, std::move(msg)});
}

inline std::string to_string(const error& e) {
    return std::to_string(e.line) + ":" + std::to_string(e.col) + ": " + e.msg;
}

// ---------------------------------------------------------------------------
// result / parse_result: a successful parse = leftover input + a value;
// a failed parse = an error.
// ---------------------------------------------------------------------------
template <class Token, class R>
struct result {
    array_view<Token> rest;
    R value;
};

template <class Token, class R>
using parse_result = std::expected<result<Token, R>, error>;

// ---------------------------------------------------------------------------
// parser<F>: wraps any callable array_view<Token> -> parse_result<Token, R>.
// The wrapper exists so combinator operators only apply to *our* parsers.
// ---------------------------------------------------------------------------
template <class F>
struct parser {
    F run;
    template <class Token>
    constexpr auto operator()(array_view<Token> in) const {
        return run(in);
    }
};
template <class F>
parser(F) -> parser<F>;

template <class F>
constexpr auto make_parser(F f) {
    return parser<std::decay_t<F>>{std::move(f)};
}

template <class P>
struct is_parser : std::false_type {};
template <class F>
struct is_parser<parser<F>> : std::true_type {};

template <class P>
concept Parser = is_parser<std::remove_cvref_t<P>>::value;

// Type-erased parser, useful for recursive grammars (see json.hpp).
template <class Token, class R>
using fn_parser =
    parser<std::function<parse_result<Token, R>(array_view<Token>)>>;

// ===========================================================================
// Primitive parsers
// ===========================================================================

// Consume exactly one token; fail at end of input.
inline const auto item = make_parser([](auto in) {
    using Tok = typename decltype(in)::value_type;
    using R = parse_result<Tok, Tok>;
    if (in.empty()) return R{err(in.pos(), "unexpected end of input")};
    return R{result<Tok, Tok>{in.drop(1), in.front()}};
});

// Consume one token if it satisfies a predicate. `what` names it for errors.
template <class Pred>
constexpr auto satisfy(Pred pred, std::string what = "valid input") {
    return make_parser([pred = std::move(pred), what = std::move(what)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using R = parse_result<Tok, Tok>;
        if (in.empty())
            return R{err(in.pos(), "expected " + what + ", got end of input")};
        if (pred(in.front()))
            return R{result<Tok, Tok>{in.drop(1), in.front()}};
        return R{err(in.pos(), "expected " + what)};
    });
}

// Match a specific token by equality.
template <class T>
constexpr auto sym(T t) {
    std::string what;
    if constexpr (std::is_same_v<T, char>)
        what = std::string("'") + t + "'";
    else
        what = "a specific symbol";
    return satisfy([t](const auto& x) { return x == t; }, std::move(what));
}

// Match one char out of a set, e.g. one_of("+-").
inline auto one_of(std::string set, std::string name = {}) {
    std::string what = name.empty() ? "one of \"" + set + "\"" : std::move(name);
    return satisfy(
        [set = std::move(set)](char c) { return set.find(c) != std::string::npos; },
        std::move(what));
}

// Succeed without consuming anything, yielding v.
template <class V>
constexpr auto pure(V v) {
    return make_parser([v = std::move(v)](auto in) {
        using Tok = typename decltype(in)::value_type;
        return parse_result<Tok, V>{result<Tok, V>{in, v}};
    });
}

// Always fail with the given message.
template <class V>
constexpr auto fail(std::string msg = "failure") {
    return make_parser([msg = std::move(msg)](auto in) {
        using Tok = typename decltype(in)::value_type;
        return parse_result<Tok, V>{err(in.pos(), msg)};
    });
}

// Match a literal sequence of chars; yields the matched string.
inline auto literal(std::string_view s) {
    return make_parser([s = std::string(s)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using R = parse_result<Tok, std::string>;
        for (std::size_t i = 0; i < s.size(); ++i)
            if (i >= in.size() || in[i] != s[i])
                return R{err(in.pos(), "expected \"" + s + "\"")};
        return R{result<Tok, std::string>{in.drop(s.size()), s}};
    });
}

// Give a parser a friendly name in diagnostics. Following Parsec's `<?>`, the
// message is only replaced for failures that consumed no input; a deeper,
// committed error (e.g. an unterminated string) keeps its specific message.
template <Parser P>
constexpr auto label(P p, std::string name) {
    return make_parser([p = std::move(p), name = std::move(name)](auto in) {
        auto r = p(in);
        if (!r && r.error().pos == in.pos()) {
            error e = r.error();
            e.msg = "expected " + name;
            return decltype(r){std::unexpected(std::move(e))};
        }
        return r;
    });
}

// ===========================================================================
// Functor / monad
// ===========================================================================

// map: transform the produced value with f.
template <Parser P, class F>
constexpr auto map(P p, F f) {
    return make_parser([p = std::move(p), f = std::move(f)](auto in) {
        using Tok = typename decltype(in)::value_type;
        auto r = p(in);
        using V = std::remove_cvref_t<decltype(f(std::move(r->value)))>;
        using R = parse_result<Tok, V>;
        if (!r) return R{std::unexpected(r.error())};
        return R{result<Tok, V>{r->rest, f(std::move(r->value))}};
    });
}

// bind: feed the value into f, which returns the next parser to run.
template <Parser P, class F>
constexpr auto bind(P p, F f) {
    return make_parser([p = std::move(p), f = std::move(f)](auto in) {
        auto r = p(in);
        using RT = std::remove_cvref_t<decltype(f(std::move(r->value))(r->rest))>;
        if (!r) return RT{std::unexpected(r.error())};
        return f(std::move(r->value))(r->rest);
    });
}

// ===========================================================================
// Sequencing & choice
// ===========================================================================

// a | b : try a, otherwise try b. If both fail, report whichever error got
// further into the input (a more informative diagnostic).
template <Parser A, Parser B>
constexpr auto operator|(A a, B b) {
    return make_parser([a = std::move(a), b = std::move(b)](auto in) {
        auto ra = a(in);
        if (ra) return ra;
        auto rb = b(in);
        if (rb) return rb;
        return decltype(ra){std::unexpected(
            rb.error().pos > ra.error().pos ? rb.error() : ra.error())};
    });
}

// a >> b : run both, keep b's value.
template <Parser A, Parser B>
constexpr auto operator>>(A a, B b) {
    return make_parser([a = std::move(a), b = std::move(b)](auto in) {
        auto ra = a(in);
        using RB = std::remove_cvref_t<decltype(b(ra->rest))>;
        if (!ra) return RB{std::unexpected(ra.error())};
        return b(ra->rest);
    });
}

// a << b : run both, keep a's value.
template <Parser A, Parser B>
constexpr auto operator<<(A a, B b) {
    return make_parser([a = std::move(a), b = std::move(b)](auto in) {
        using Tok = typename decltype(in)::value_type;
        auto ra = a(in);
        using V = std::remove_cvref_t<decltype(ra->value)>;
        using R = parse_result<Tok, V>;
        if (!ra) return R{std::unexpected(ra.error())};
        auto rb = b(ra->rest);
        if (!rb) return R{std::unexpected(rb.error())};
        return R{result<Tok, V>{rb->rest, std::move(ra->value)}};
    });
}

// seq(p...) : run all in order, collect values into a std::tuple.
template <Parser P>
constexpr auto seq(P p) {
    return map(std::move(p), [](auto v) {
        return std::tuple<std::remove_cvref_t<decltype(v)>>{std::move(v)};
    });
}
template <Parser P, Parser Q, Parser... Rest>
constexpr auto seq(P p, Q q, Rest... rest) {
    auto tail = seq(std::move(q), std::move(rest)...);
    return make_parser([p = std::move(p), tail = std::move(tail)](auto in) {
        using Tok = typename decltype(in)::value_type;
        auto r1 = p(in);
        using Head = std::remove_cvref_t<decltype(r1->value)>;
        using Tail = std::remove_cvref_t<decltype(tail(r1->rest)->value)>;
        using Tup = decltype(std::tuple_cat(std::declval<std::tuple<Head>>(),
                                            std::declval<Tail>()));
        using R = parse_result<Tok, Tup>;
        if (!r1) return R{std::unexpected(r1.error())};
        auto r2 = tail(r1->rest);
        if (!r2) return R{std::unexpected(r2.error())};
        return R{result<Tok, Tup>{
            r2->rest, std::tuple_cat(std::tuple<Head>{std::move(r1->value)},
                                     std::move(r2->value))}};
    });
}

// ===========================================================================
// Repetition
// ===========================================================================

// many(p) : zero or more p, collected into a vector. Stops when p fails without
// consuming; but if p fails *after* consuming input, that is a real error.
template <Parser P>
constexpr auto many(P p) {
    return make_parser([p = std::move(p)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::vector<V>>;
        std::vector<V> acc;
        auto cur = in;
        for (;;) {
            auto r = p(cur);
            if (!r) {
                if (r.error().pos > cur.pos()) return R{std::unexpected(r.error())};
                break;  // failed without consuming -> done
            }
            if (r->rest.size() == cur.size()) break;  // no progress
            acc.push_back(std::move(r->value));
            cur = r->rest;
        }
        return R{result<Tok, std::vector<V>>{cur, std::move(acc)}};
    });
}

// many1(p) : one or more p.
template <Parser P>
constexpr auto many1(P p) {
    return make_parser([p = std::move(p)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::vector<V>>;
        auto first = p(in);
        if (!first) return R{std::unexpected(first.error())};
        std::vector<V> acc;
        acc.push_back(std::move(first->value));
        auto cur = first->rest;
        for (;;) {
            auto r = p(cur);
            if (!r) {
                if (r.error().pos > cur.pos()) return R{std::unexpected(r.error())};
                break;
            }
            if (r->rest.size() == cur.size()) break;
            acc.push_back(std::move(r->value));
            cur = r->rest;
        }
        return R{result<Tok, std::vector<V>>{cur, std::move(acc)}};
    });
}

// opt(p) : zero or one p, yielding std::optional<V> (always succeeds).
template <Parser P>
constexpr auto opt(P p) {
    return make_parser([p = std::move(p)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::optional<V>>;
        auto r = p(in);
        if (r)
            return R{result<Tok, std::optional<V>>{
                r->rest, std::optional<V>{std::move(r->value)}}};
        return R{result<Tok, std::optional<V>>{in, std::optional<V>{}}};
    });
}

// sep_by(p, s) : zero or more p separated by s (no trailing separator).
//
// Error handling follows the "consumed input" rule: if the first element fails
// without consuming anything, the list is simply empty; but once an element has
// started — or a separator has been seen — a failure is a real error. This is
// what makes "[1, ]" or "{\"a\": }" report the missing value rather than a
// confusing "expected ']'".
template <Parser P, Parser S>
constexpr auto sep_by(P p, S s) {
    return make_parser([p = std::move(p), s = std::move(s)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::vector<V>>;
        std::vector<V> acc;
        auto first = p(in);
        if (!first) {
            if (first.error().pos > in.pos())  // started but broke -> real error
                return R{std::unexpected(first.error())};
            return R{result<Tok, std::vector<V>>{in, std::move(acc)}};  // empty
        }
        acc.push_back(std::move(first->value));
        auto cur = first->rest;
        for (;;) {
            auto rs = s(cur);
            if (!rs) break;  // no more separators -> done
            auto rp = p(rs->rest);
            if (!rp) return R{std::unexpected(rp.error())};  // separator, no element
            acc.push_back(std::move(rp->value));
            cur = rp->rest;
        }
        return R{result<Tok, std::vector<V>>{cur, std::move(acc)}};
    });
}

// between(open, p, close) : open >> p << close.
template <Parser O, Parser P, Parser C>
constexpr auto between(O o, P p, C c) {
    return (std::move(o) >> std::move(p)) << std::move(c);
}

// count(n, p) : exactly n occurrences of p.
template <Parser P>
constexpr auto count(std::size_t n, P p) {
    return make_parser([p = std::move(p), n](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::vector<V>>;
        std::vector<V> acc;
        acc.reserve(n);
        auto cur = in;
        for (std::size_t i = 0; i < n; ++i) {
            auto r = p(cur);
            if (!r) return R{std::unexpected(r.error())};
            acc.push_back(std::move(r->value));
            cur = r->rest;
        }
        return R{result<Tok, std::vector<V>>{cur, std::move(acc)}};
    });
}

// ===========================================================================
// String assembly: turn parser results into / concatenate them as strings.
// ===========================================================================

namespace detail {
inline std::string as_text(char c) { return std::string(1, c); }
inline std::string as_text(std::string s) { return s; }
inline std::string as_text(const std::optional<char>& o) {
    return o ? std::string(1, *o) : std::string{};
}
inline std::string as_text(const std::optional<std::string>& o) {
    return o ? *o : std::string{};
}
inline std::string as_text(const std::vector<char>& v) {
    return std::string(v.begin(), v.end());
}
inline std::string as_text(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += x;
    return s;
}
}  // namespace detail

// stringify(p) : render p's value (char / optional / vector / string) as text.
template <Parser P>
constexpr auto stringify(P p) {
    return map(std::move(p), [](const auto& v) { return detail::as_text(v); });
}

// cat(p...) : run all in order and concatenate their values as text.
template <Parser... Ps>
constexpr auto cat(Ps... ps) {
    return map(seq(std::move(ps)...), [](auto tup) {
        std::string s;
        std::apply([&](const auto&... xs) { ((s += detail::as_text(xs)), ...); },
                   tup);
        return s;
    });
}

// ===========================================================================
// Lexing helpers (char streams)
// ===========================================================================

inline const auto ws = many(satisfy(
    [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; },
    "whitespace"));

// lexeme(p) : skip leading whitespace, then run p.
template <Parser P>
constexpr auto lexeme(P p) {
    return ws >> std::move(p);
}

// tok(c) : a single character as a whitespace-tolerant token.
template <class T>
constexpr auto tok(T c) {
    return lexeme(sym(c));
}

// ===========================================================================
// Runners
// ===========================================================================

// Translate a byte offset into a 1-based (line, column).
inline std::pair<std::size_t, std::size_t> line_col(std::string_view s,
                                                    std::size_t pos) {
    std::size_t line = 1, col = 1;
    pos = std::min(pos, s.size());
    for (std::size_t i = 0; i < pos; ++i) {
        if (s[i] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
    }
    return {line, col};
}

namespace detail {
inline error locate(std::string_view s, error e) {
    auto [line, col] = line_col(s, e.pos);
    e.line = line;
    e.col = col;
    return e;
}
}  // namespace detail

// Run p over s, returning the raw parse result (rest + value), with line/col
// filled in on failure.
template <Parser P>
auto run(P p, std::string_view s) {
    auto r = p(view(s));
    using T = std::remove_cvref_t<decltype(r)>;
    if (!r) return T{std::unexpected(detail::locate(s, r.error()))};
    return r;
}

// Run p and require that the whole input is consumed; yields expected<V, error>.
template <Parser P>
auto parse(P p, std::string_view s) {
    auto r = p(view(s));
    using V = std::remove_cvref_t<decltype(r->value)>;
    using Out = std::expected<V, error>;
    if (!r) return Out{std::unexpected(detail::locate(s, r.error()))};
    if (!r->rest.empty())
        return Out{std::unexpected(detail::locate(
            s, error{r->rest.pos(), 0, 0, "unexpected trailing input"}))};
    return Out{std::move(r->value)};
}

}  // namespace combo
