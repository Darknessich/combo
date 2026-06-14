//
// examples/json_gen.cpp — schema-driven JSON generation (the producer side).
//
// We declare a schema, generate random documents from it, and then feed each
// generated document straight back into json::parse. The generator and the
// parser are duals, so this round-trip (generate -> serialise -> parse) is the
// duality made concrete: every document the generator emits is, by
// construction, valid input for the parser.
//
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "json.hpp"
#include "jsongen.hpp"

int main(int argc, char** argv) {
    using namespace jsongen;

    unsigned seed = 1;
    int count = 3;
    if (argc > 1) seed = static_cast<unsigned>(std::stoul(argv[1]));
    if (argc > 2) count = std::stoi(argv[2]);

    // A schema: "what a user document looks like".
    schema user = obj({
        {"id", integer(1, 100000)},
        {"name", one_of_str({"Alice", "Bob", "Carol", "Dave", "Eve"})},
        {"active", boolean()},
        {"score", number(0.0, 1.0)},
        {"roles", array_of(one_of_str({"admin", "editor", "viewer"}), 0, 3)},
        {"address", obj({
                        {"city", one_of_str({"NY", "LA", "SF", "Berlin"})},
                        {"zip", integer(10000, 99999)},
                    })},
        {"nickname", optional(one_of_str({"neo", "trinity", "morpheus"}), 0.6)},
    });

    std::mt19937 rng(seed);

    for (int i = 0; i < count; ++i) {
        json::value doc = user(rng);

        // Serialise...
        std::ostringstream oss;
        oss << doc;
        std::string text = oss.str();

        // ...then parse it back: the dual loop must close.
        auto reparsed = json::parse(text);

        std::cout << "--- document " << (i + 1) << " ---\n";
        json::pretty(std::cout, doc);
        std::cout << "\nround-trip through parser: "
                  << (reparsed ? "OK" : "FAILED") << "\n\n";

        if (!reparsed) return 1;
    }

    std::cout << count << " documents generated, all parse back cleanly.\n";
    return 0;
}
