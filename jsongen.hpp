#pragma once
//
// jsongen.hpp — a schema-driven JSON *generator*, built on gen.hpp.
//
// This is the producing dual of json.hpp's parser. You describe the shape of a
// document — fields, value types, ranges — and get a generator that emits
// random JSON matching it (think property-based testing / fuzzing input).
//
//     auto user = obj({
//         {"id",   integer(1, 1000)},
//         {"name", one_of_str({"Alice", "Bob"})},
//         {"tags", array_of(one_of_str({"a", "b"}), 0, 3)},
//     });
//     std::mt19937 rng(seed);
//     json::value doc = user(rng);   // a fresh random document
//
// A `schema` is itself a gen.hpp generator, so the whole module is expressed by
// composing generator combinators (map, bind, repeat, range, oneof) — the same
// algebra json.hpp consumes with.
//
#include <cstddef>
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "gen.hpp"
#include "json.hpp"

namespace jsongen {

namespace g = combo::gen;

// A type-erased generator of JSON values. Being a generator itself, it composes
// with every gen.hpp combinator.
using schema = g::generator<std::function<json::value(std::mt19937&)>>;

// Erase any json::value generator (or RNG -> value callable) into a schema.
template <class G>
schema erase(G gen) {
    return schema{std::function<json::value(std::mt19937&)>{
        [gen = std::move(gen)](std::mt19937& rng) { return gen(rng); }}};
}

// --- scalars ---------------------------------------------------------------

inline schema null() { return erase(g::constant(json::value{nullptr})); }

inline schema boolean() {
    return erase(g::map(g::range(0, 1),
                        [](long b) { return json::value{b != 0}; }));
}

// An integer-valued JSON number in [lo, hi].
inline schema integer(long lo, long hi) {
    return erase(g::map(g::range(lo, hi), [](long x) {
        return json::value{static_cast<double>(x)};
    }));
}

// A real JSON number in [lo, hi).
inline schema number(double lo, double hi) {
    return erase(g::map(g::real(), [lo, hi](double u) {
        return json::value{lo + u * (hi - lo)};
    }));
}

// A string drawn from a fixed set of options.
inline schema one_of_str(std::vector<std::string> options) {
    return erase(g::map(g::oneof(std::move(options)),
                        [](std::string s) { return json::value{std::move(s)}; }));
}

// --- composites ------------------------------------------------------------

// Pick one of several alternatives uniformly  (dual of parser `a | b`):
// roll an index, then run that schema.
inline schema one_of(std::vector<schema> alts) {
    return erase(g::bind(g::range(0, static_cast<long>(alts.size()) - 1),
                         [alts = std::move(alts)](long i) { return alts[i]; }));
}

// An array of [lo, hi] elements, each drawn from `elem`  (dual of parser many):
// roll a length, repeat the element generator, wrap as a JSON array.
inline schema array_of(schema elem, int lo, int hi) {
    return erase(g::bind(g::range(lo, hi), [elem = std::move(elem)](long n) {
        return g::map(g::repeat(elem, static_cast<std::size_t>(n)),
                      [](json::array a) { return json::value{std::move(a)}; });
    }));
}

// With probability `p` produce `inner`, otherwise null  (dual of parser opt).
inline schema optional(schema inner, double p = 0.5) {
    return erase(g::bind(g::real(),
                         [inner = std::move(inner), p](double u) -> schema {
                             return u < p ? inner : null();
                         }));
}

// An object with the given named fields (insertion order preserved). A record
// is a fold over its fields: run each field's generator and collect the pairs.
inline schema obj(std::vector<std::pair<std::string, schema>> fields) {
    return erase([fields = std::move(fields)](std::mt19937& rng) {
        json::object o;
        o.reserve(fields.size());
        for (const auto& [key, field] : fields) o.emplace_back(key, field(rng));
        return json::value{std::move(o)};
    });
}

}  // namespace jsongen
