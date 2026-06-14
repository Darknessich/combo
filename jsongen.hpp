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
// Each builder is assembled from gen.hpp combinators (range, real, oneof, map,
// repeat) and then type-erased to a common `schema` so heterogeneous fields can
// live in one object.
//
#include <functional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "gen.hpp"
#include "json.hpp"

namespace jsongen {

// A schema is just a generator of JSON values with a uniform, erased type.
using schema = std::function<json::value(std::mt19937&)>;

// Erase a gen.hpp generator<...> (that yields json::value) into a schema.
template <class G>
schema erase(G g) {
    return [g = std::move(g)](std::mt19937& rng) { return g(rng); };
}

// --- scalars ---------------------------------------------------------------

inline schema null() {
    return [](std::mt19937&) { return json::value{nullptr}; };
}

inline schema boolean() {
    return erase(combo::gen::map(combo::gen::range(0, 1),
                                 [](long b) { return json::value{b != 0}; }));
}

// An integer-valued JSON number in [lo, hi].
inline schema integer(long lo, long hi) {
    return erase(combo::gen::map(combo::gen::range(lo, hi), [](long x) {
        return json::value{static_cast<double>(x)};
    }));
}

// A real JSON number in [lo, hi).
inline schema number(double lo, double hi) {
    return erase(combo::gen::map(combo::gen::real(), [lo, hi](double u) {
        return json::value{lo + u * (hi - lo)};
    }));
}

// A string drawn from a fixed set of options.
inline schema one_of_str(std::vector<std::string> options) {
    return erase(combo::gen::map(
        combo::gen::oneof(std::move(options)),
        [](std::string s) { return json::value{std::move(s)}; }));
}

// --- composites ------------------------------------------------------------

// An array of [lo, hi] elements, each drawn from `elem`.
inline schema array_of(schema elem, int lo, int hi) {
    return [elem = std::move(elem), lo, hi](std::mt19937& rng) {
        long n = combo::gen::range(lo, hi)(rng);
        json::array a;
        a.reserve(static_cast<std::size_t>(n));
        for (long i = 0; i < n; ++i) a.push_back(elem(rng));
        return json::value{std::move(a)};
    };
}

// An object with the given named fields (insertion order preserved).
inline schema obj(std::vector<std::pair<std::string, schema>> fields) {
    return [fields = std::move(fields)](std::mt19937& rng) {
        json::object o;
        o.reserve(fields.size());
        for (const auto& [key, gen] : fields) o.emplace_back(key, gen(rng));
        return json::value{std::move(o)};
    };
}

// Pick one of several alternatives uniformly  (dual of parser `a | b`).
inline schema one_of(std::vector<schema> alts) {
    return [alts = std::move(alts)](std::mt19937& rng) {
        std::uniform_int_distribution<std::size_t> d(0, alts.size() - 1);
        return alts[d(rng)](rng);
    };
}

// With probability `p_present` produce `inner`, otherwise null.
inline schema optional(schema inner, double p_present = 0.5) {
    return [inner = std::move(inner), p_present](std::mt19937& rng) {
        if (combo::gen::real()(rng) < p_present) return inner(rng);
        return json::value{nullptr};
    };
}

}  // namespace jsongen
