#pragma once
//
// parser.hpp — a tiny header-only parser combinator library.
//
// A parser CONSUMES a view of tokens and (maybe) produces a value together
// with the unconsumed rest of the input:
//
//     parser : array_view<Token> -> optional<result<Token, R>>
//
// Combinators build bigger parsers out of smaller ones. See gen.hpp for the
// dual construction (generators that PRODUCE output from the same algebra).
//
#include <algorithm>
#include <cstddef>
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
// array_view: a non-owning window onto a contiguous range of tokens.
// ---------------------------------------------------------------------------
template <class T>
class array_view {
public:
    using value_type = T;

    constexpr array_view() noexcept = default;
    constexpr array_view(const T* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    // Implicit view of any contiguous container (vector, string, string_view,
    // std::array, ...) whose data() yields something convertible to const T*.
    template <class C,
              class = std::enable_if_t<std::is_convertible_v<
                  decltype(std::declval<const C&>().data()), const T*>>>
    constexpr array_view(const C& c) noexcept
        : data_(c.data()), size_(c.size()) {}

    constexpr bool empty() const noexcept { return size_ == 0; }
    constexpr std::size_t size() const noexcept { return size_; }
    constexpr const T& operator[](std::size_t i) const noexcept { return data_[i]; }
    constexpr const T& front() const noexcept { return data_[0]; }

    // Drop the first n tokens (clamped), returning the tail.
    constexpr array_view drop(std::size_t n) const noexcept {
        n = n < size_ ? n : size_;
        return array_view(data_ + n, size_ - n);
    }

    constexpr const T* begin() const noexcept { return data_; }
    constexpr const T* end() const noexcept { return data_ + size_; }

private:
    const T* data_ = nullptr;
    std::size_t size_ = 0;
};

// Convenience: build a char view from a string-like value.
inline array_view<char> view(std::string_view s) noexcept {
    return {s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// result / parse_result: a successful parse = leftover input + a value.
// ---------------------------------------------------------------------------
template <class Token, class R>
struct result {
    array_view<Token> rest;
    R value;
};

template <class Token, class R>
using parse_result = std::optional<result<Token, R>>;

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

// Type-erased parser, useful for recursive grammars (see examples/json.cpp).
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
    if (in.empty()) return R{};
    return R{result<Tok, Tok>{in.drop(1), in.front()}};
});

// Consume one token if it satisfies a predicate.
template <class Pred>
constexpr auto satisfy(Pred pred) {
    return make_parser([pred = std::move(pred)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using R = parse_result<Tok, Tok>;
        if (!in.empty() && pred(in.front()))
            return R{result<Tok, Tok>{in.drop(1), in.front()}};
        return R{};
    });
}

// Match a specific token by equality.
template <class T>
constexpr auto sym(T t) {
    return satisfy([t](const auto& x) { return x == t; });
}

// Succeed without consuming anything, yielding v.
template <class V>
constexpr auto pure(V v) {
    return make_parser([v = std::move(v)](auto in) {
        using Tok = typename decltype(in)::value_type;
        return parse_result<Tok, V>{result<Tok, V>{in, v}};
    });
}

// Always fail, yielding nothing of type V.
template <class V>
constexpr auto fail() {
    return make_parser([](auto in) {
        using Tok = typename decltype(in)::value_type;
        return parse_result<Tok, V>{};
    });
}

// Match a literal sequence of chars; yields the matched string.
inline auto literal(std::string_view s) {
    return make_parser([s = std::string(s)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using R = parse_result<Tok, std::string>;
        if (in.size() < s.size()) return R{};
        for (std::size_t i = 0; i < s.size(); ++i)
            if (in[i] != s[i]) return R{};
        return R{result<Tok, std::string>{in.drop(s.size()), s}};
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
        if (!r) return R{};
        return R{result<Tok, V>{r->rest, f(std::move(r->value))}};
    });
}

// bind: feed the value into f, which returns the next parser to run.
template <Parser P, class F>
constexpr auto bind(P p, F f) {
    return make_parser([p = std::move(p), f = std::move(f)](auto in) {
        auto r = p(in);
        using RT = std::remove_cvref_t<decltype(f(std::move(r->value))(r->rest))>;
        if (!r) return RT{};
        auto next = f(std::move(r->value));
        return next(r->rest);
    });
}

// ===========================================================================
// Sequencing & choice
// ===========================================================================

// a | b : try a, otherwise try b (both must yield the same type).
template <Parser A, Parser B>
constexpr auto operator|(A a, B b) {
    return make_parser([a = std::move(a), b = std::move(b)](auto in) {
        auto r = a(in);
        if (r) return r;
        return b(in);
    });
}

// a >> b : run both, keep b's value.
template <Parser A, Parser B>
constexpr auto operator>>(A a, B b) {
    return make_parser([a = std::move(a), b = std::move(b)](auto in) {
        auto ra = a(in);
        using RB = std::remove_cvref_t<decltype(b(ra->rest))>;
        if (!ra) return RB{};
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
        if (!ra) return R{};
        auto rb = b(ra->rest);
        if (!rb) return R{};
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
        if (!r1) return R{};
        auto r2 = tail(r1->rest);
        if (!r2) return R{};
        return R{result<Tok, Tup>{
            r2->rest, std::tuple_cat(std::tuple<Head>{std::move(r1->value)},
                                     std::move(r2->value))}};
    });
}

// ===========================================================================
// Repetition
// ===========================================================================

// many(p) : zero or more p, collected into a vector (always succeeds).
template <Parser P>
constexpr auto many(P p) {
    return make_parser([p = std::move(p)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        std::vector<V> acc;
        auto cur = in;
        for (;;) {
            auto r = p(cur);
            if (!r || r->rest.size() == cur.size()) break;  // stop, no progress
            acc.push_back(std::move(r->value));
            cur = r->rest;
        }
        return parse_result<Tok, std::vector<V>>{
            result<Tok, std::vector<V>>{cur, std::move(acc)}};
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
        if (!first) return R{};
        std::vector<V> acc;
        acc.push_back(std::move(first->value));
        auto cur = first->rest;
        for (;;) {
            auto r = p(cur);
            if (!r || r->rest.size() == cur.size()) break;
            acc.push_back(std::move(r->value));
            cur = r->rest;
        }
        return R{result<Tok, std::vector<V>>{cur, std::move(acc)}};
    });
}

// opt(p) : zero or one p, yielding std::optional<V>.
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
template <Parser P, Parser S>
constexpr auto sep_by(P p, S s) {
    return make_parser([p = std::move(p), s = std::move(s)](auto in) {
        using Tok = typename decltype(in)::value_type;
        using V = std::remove_cvref_t<decltype(p(in)->value)>;
        using R = parse_result<Tok, std::vector<V>>;
        std::vector<V> acc;
        auto first = p(in);
        if (!first)
            return R{result<Tok, std::vector<V>>{in, std::move(acc)}};
        acc.push_back(std::move(first->value));
        auto cur = first->rest;
        for (;;) {
            auto rs = s(cur);
            if (!rs) break;
            auto rp = p(rs->rest);
            if (!rp) break;  // dangling separator: don't commit it
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

// ===========================================================================
// Lexing helpers (char streams)
// ===========================================================================

inline const auto ws = many(satisfy([](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}));

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

// Run p over s, returning the raw parse result (rest + value).
template <Parser P>
auto run(P p, std::string_view s) {
    return p(view(s));
}

// Run p and require that the whole input is consumed; yields optional<V>.
template <Parser P>
auto parse(P p, std::string_view s) {
    auto r = p(view(s));
    using V = std::remove_cvref_t<decltype(r->value)>;
    if (r && r->rest.empty()) return std::optional<V>{std::move(r->value)};
    return std::optional<V>{};
}

}  // namespace combo
