//
// examples/json.cpp — JSON parsing demo (the consumer side of the algebra).
//
// The grammar itself lives in json.hpp, assembled from combo.hpp combinators.
//
#include <iostream>
#include <string>

#include "json.hpp"

int main(int argc, char** argv) {
    std::string text =
        R"({
            "name": "combo",
            "version": 0.1,
            "stable": false,
            "tags": ["parser", "combinator", "c++20"],
            "nested": {"a": [1, 2, 3], "b": null},
            "unicode": "café ☃"
        })";

    if (argc > 1) text = argv[1];

    std::cout << "--- input ---\n" << text << "\n\n";

    auto v = json::parse(text);
    if (!v) {
        std::cerr << "parse error\n";
        return 1;
    }

    std::cout << "--- reserialised (compact) ---\n" << *v << "\n\n";
    std::cout << "--- reserialised (pretty) ---\n";
    json::pretty(std::cout, *v);
    std::cout << "\n";
    return 0;
}
