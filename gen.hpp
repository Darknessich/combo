#pragma once
//
// gen.hpp — generator combinators: the categorical dual of parser.hpp.
//
// Where a parser CONSUMES input to produce a value, a generator PRODUCES a
// value by drawing from a source of randomness:
//
//     generator : RNG& -> T
//
// The combinator algebra mirrors the parser one almost name-for-name, which is
// the whole point — building structured output is "running a grammar in
// reverse".
//
//      parser              generator (dual)
//      ------              ----------------
//      pure(x)             constant(x)        always yield x
//      sym / satisfy       range / oneof      pick an atom
//      a | b               a | b              choose an alternative
//      map(p, f)           map(g, f)          transform the value
//      bind(p, f)          bind(g, f)         context-dependent step
//      seq(p...)           seq(g...)          a tuple of values
//      many(p)             repeat(g, n)       a sequence of values
//
#include <algorithm>
#include <cstddef>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace combo::gen {

// ---------------------------------------------------------------------------
// generator<F>: wraps any callable RNG& -> T.
// ---------------------------------------------------------------------------
template <class F>
struct generator {
    F run;
    template <class RNG>
    auto operator()(RNG& rng) const {
        return run(rng);
    }
};
template <class F>
generator(F) -> generator<F>;

template <class F>
constexpr auto make_gen(F f) {
    return generator<std::decay_t<F>>{std::move(f)};
}

template <class G>
struct is_gen : std::false_type {};
template <class F>
struct is_gen<generator<F>> : std::true_type {};

template <class G>
concept Gen = is_gen<std::remove_cvref_t<G>>::value;

// ===========================================================================
// Atoms  (dual of: pure / sym / satisfy)
// ===========================================================================

// constant(v) : always produce v.            (dual of parser pure)
template <class T>
constexpr auto constant(T v) {
    return make_gen([v = std::move(v)](auto&) { return v; });
}

// range(lo, hi) : a uniform integer in [lo, hi].
inline auto range(long lo, long hi) {
    return make_gen([lo, hi](auto& rng) {
        std::uniform_int_distribution<long> d(lo, hi);
        return d(rng);
    });
}

// real() : a uniform double in [0, 1).
inline auto real() {
    return make_gen([](auto& rng) {
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return d(rng);
    });
}

// oneof(values) : pick one element of a list.   (dual of parser choice of sym)
template <class T>
auto oneof(std::vector<T> xs) {
    return make_gen([xs = std::move(xs)](auto& rng) {
        std::uniform_int_distribution<std::size_t> d(0, xs.size() - 1);
        return xs[d(rng)];
    });
}

// ===========================================================================
// Functor / monad  (dual of: map / bind)
// ===========================================================================

template <Gen G, class F>
constexpr auto map(G g, F f) {
    return make_gen(
        [g = std::move(g), f = std::move(f)](auto& rng) { return f(g(rng)); });
}

template <Gen G, class F>
constexpr auto bind(G g, F f) {
    return make_gen([g = std::move(g), f = std::move(f)](auto& rng) {
        return f(g(rng))(rng);
    });
}

// ===========================================================================
// Choice  (dual of: a | b)
// ===========================================================================

// a | b : flip a coin and run one of them (both must yield the same type).
template <Gen A, Gen B>
constexpr auto operator|(A a, B b) {
    return make_gen([a = std::move(a), b = std::move(b)](auto& rng) {
        std::uniform_int_distribution<int> coin(0, 1);
        return coin(rng) == 0 ? a(rng) : b(rng);
    });
}

// ===========================================================================
// Repetition / sequencing  (dual of: many / seq)
// ===========================================================================

// repeat(g, n) : produce n values into a vector.   (dual of parser many)
template <Gen G>
auto repeat(G g, std::size_t n) {
    return make_gen([g = std::move(g), n](auto& rng) {
        using T = std::remove_cvref_t<decltype(g(rng))>;
        std::vector<T> out;
        out.reserve(n);
        for (std::size_t i = 0; i < n; ++i) out.push_back(g(rng));
        return out;
    });
}

// seq(g...) : produce one value from each, collected into a tuple.
template <Gen G>
constexpr auto seq(G g) {
    return map(std::move(g), [](auto v) {
        return std::tuple<std::remove_cvref_t<decltype(v)>>{std::move(v)};
    });
}
template <Gen G, Gen H, Gen... Rest>
constexpr auto seq(G g, H h, Rest... rest) {
    auto tail = seq(std::move(h), std::move(rest)...);
    return make_gen([g = std::move(g), tail = std::move(tail)](auto& rng) {
        auto head = g(rng);
        return std::tuple_cat(
            std::tuple<std::remove_cvref_t<decltype(head)>>{std::move(head)},
            tail(rng));
    });
}

// shuffle(values) : produce a randomly permuted copy of the list.
template <class T>
auto shuffle(std::vector<T> xs) {
    return make_gen([xs = std::move(xs)](auto& rng) {
        auto out = xs;  // a generator is reusable, so don't mutate the capture
        std::shuffle(out.begin(), out.end(), rng);
        return out;
    });
}

}  // namespace combo::gen
