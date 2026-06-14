//
// tests/tests.cpp — lightweight assertions for both halves of the library.
//
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include <sstream>

#include "parser.hpp"
#include "gen.hpp"
#include "json.hpp"
#include "jsongen.hpp"

static int checks = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            std::cerr << "FAILED: " #cond " (line " << __LINE__ << ")\n"; \
            std::exit(1);                                                 \
        }                                                                 \
    } while (0)

void test_parsers() {
    using namespace combo;

    // sym / parse (full consume)
    CHECK(parse(sym('a'), "a") == std::optional<char>{'a'});
    CHECK(!parse(sym('a'), "b"));
    CHECK(!parse(sym('a'), "aa"));  // leftover -> fail full-consume

    // choice
    auto ab = sym('a') | sym('b');
    CHECK(parse(ab, "b") == std::optional<char>{'b'});

    // map
    auto digit = satisfy([](char c) { return c >= '0' && c <= '9'; });
    auto to_int = map(digit, [](char c) { return c - '0'; });
    CHECK(parse(to_int, "7") == std::optional<int>{7});

    // many1 + map: an unsigned integer
    auto number = map(many1(digit), [](std::vector<char> ds) {
        int n = 0;
        for (char d : ds) n = n * 10 + (d - '0');
        return n;
    });
    CHECK(parse(number, "12345") == std::optional<int>{12345});
    CHECK(!parse(number, ""));

    // >> and << keep the right / left value
    CHECK(parse(sym('(') >> number, "(42") == std::optional<int>{42});
    CHECK(parse(number << sym(')'), "42)") == std::optional<int>{42});

    // sep_by + between: a comma list inside brackets
    auto list = between(sym('['), sep_by(number, sym(',')), sym(']'));
    auto r = parse(list, "[1,2,3]");
    CHECK(r && (*r == std::vector<int>{1, 2, 3}));
    auto empty = parse(list, "[]");
    CHECK(empty && empty->empty());

    // seq -> tuple
    auto pair = seq(digit, sym('-'), digit);
    auto rp = parse(pair, "4-9");
    CHECK(rp && std::get<0>(*rp) == '4' && std::get<2>(*rp) == '9');

    // whitespace handling via tok
    auto spaced = tok('a') >> tok('b');
    CHECK(parse(spaced << ws, "  a   b  ") == std::optional<char>{'b'});
}

void test_generators() {
    namespace g = combo::gen;
    std::mt19937 rng(123);

    // constant
    CHECK(g::constant(7)(rng) == 7);

    // range stays in bounds
    auto r = g::range(10, 20);
    for (int i = 0; i < 1000; ++i) {
        long v = r(rng);
        CHECK(v >= 10 && v <= 20);
    }

    // oneof returns an element of the set
    std::vector<int> set{2, 4, 6, 8};
    auto pick = g::oneof(set);
    for (int i = 0; i < 1000; ++i) {
        int v = static_cast<int>(pick(rng));
        CHECK(v == 2 || v == 4 || v == 6 || v == 8);
    }

    // repeat yields the requested count
    auto coins = g::repeat(g::range(0, 1), 50);
    CHECK(coins(rng).size() == 50);

    // shuffle is a permutation
    std::vector<int> xs{1, 2, 3, 4, 5};
    auto perm = g::shuffle(xs)(rng);
    std::sort(perm.begin(), perm.end());
    CHECK(perm == xs);

    // map / seq
    auto doubled = g::map(g::constant(21), [](int x) { return x * 2; });
    CHECK(doubled(rng) == 42);
    auto t = g::seq(g::constant('a'), g::constant(3))(rng);
    CHECK(std::get<0>(t) == 'a' && std::get<1>(t) == 3);

    // determinism: same seed -> same stream
    std::mt19937 a(7), b(7);
    auto gen = g::repeat(g::range(0, 1000), 20);
    CHECK(gen(a) == gen(b));
}

// The parser and the generator are duals: anything the generator emits must
// parse back. Exercise the whole loop generate -> serialise -> parse.
void test_json_roundtrip() {
    using namespace jsongen;

    schema doc = obj({
        {"id", integer(1, 1000)},
        {"flag", boolean()},
        {"ratio", number(-10.0, 10.0)},
        {"items", array_of(integer(0, 9), 0, 5)},
        {"label", one_of_str({"x", "y", "z"})},
        {"maybe", optional(integer(0, 1), 0.5)},
        {"choice", one_of({null(), boolean(), integer(0, 9)})},
    });

    std::mt19937 rng(2024);
    for (int i = 0; i < 500; ++i) {
        json::value v = doc(rng);
        std::ostringstream oss;
        oss << v;
        auto reparsed = json::parse(oss.str());
        CHECK(reparsed.has_value());  // generated JSON is always valid input
    }

    // A couple of hand-written documents parse to the expected shape.
    auto a = json::parse(R"([1, 2.5, true, null, "hi"])");
    CHECK(a && std::holds_alternative<json::array>(a->data));
    CHECK(a && std::get<json::array>(a->data).size() == 5);

    auto o = json::parse(R"({"k": {"nested": []}})");
    CHECK(o && std::holds_alternative<json::object>(o->data));

    // Malformed input is rejected.
    CHECK(!json::parse("{"));
    CHECK(!json::parse("[1,2,]"));
    CHECK(!json::parse("nul"));
}

int main() {
    test_parsers();
    test_generators();
    test_json_roundtrip();
    std::cout << "all " << checks << " checks passed\n";
    return 0;
}
