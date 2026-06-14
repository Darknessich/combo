//
// examples/maze.cpp — a maze generator built from gen.hpp.
//
// Two demos:
//
//   1. The literal dual of the JSON grammar: a grid is just nested repetition
//      over a choice of cells. `repeat(repeat(choice, W), H)` mirrors a JSON
//      "array of arrays". On its own this only makes random NOISE.
//
//   2. A real maze (recursive backtracker). The algorithm is grid-stateful,
//      but every random DECISION it makes is drawn through a gen combinator
//      (gen::range to pick a start, gen::oneof to pick the next direction).
//
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gen.hpp"

namespace maze {

// ===========================================================================
// Demo 1: the combinator dual — nested repetition produces a grid.
// ===========================================================================
std::string noise_grid(int W, int H, std::mt19937& rng) {
    namespace g = combo::gen;

    auto wall = g::constant('#');
    auto floor = g::constant(' ');
    auto cell = wall | floor;            // dual of parser:  a | b
    auto row = g::repeat(cell, W);       // dual of parser:  many(p)
    auto grid = g::repeat(row, H);       // ... nested, like an array of arrays

    auto cells = grid(rng);              // std::vector<std::vector<char>>

    std::string out;
    for (const auto& r : cells) {
        for (char c : r) out.push_back(c);
        out.push_back('\n');
    }
    return out;
}

// ===========================================================================
// Demo 2: a proper maze.
// ===========================================================================

// Per-cell walls, indexed by direction: 0=N, 1=E, 2=S, 3=W.
struct Grid {
    int W, H;
    std::vector<std::array<bool, 4>> walls;

    explicit Grid(int w, int h)
        : W(w), H(h), walls(static_cast<std::size_t>(w) * h,
                            {true, true, true, true}) {}

    int idx(int x, int y) const { return y * W + x; }
};

constexpr int DX[4] = {0, 1, 0, -1};
constexpr int DY[4] = {-1, 0, 1, 0};
constexpr int OPP[4] = {2, 3, 0, 1};

Grid carve(int W, int H, std::mt19937& rng) {
    namespace g = combo::gen;
    Grid m(W, H);
    std::vector<char> visited(static_cast<std::size_t>(W) * H, 0);

    // Pick a starting cell — a random *value*, drawn from a generator.
    int x = static_cast<int>(g::range(0, W - 1)(rng));
    int y = static_cast<int>(g::range(0, H - 1)(rng));

    std::vector<int> stack{m.idx(x, y)};
    visited[m.idx(x, y)] = 1;

    while (!stack.empty()) {
        int cur = stack.back();
        int cx = cur % W, cy = cur / W;

        std::vector<int> open;  // directions to unvisited neighbours
        for (int d = 0; d < 4; ++d) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx >= 0 && nx < W && ny >= 0 && ny < H &&
                !visited[m.idx(nx, ny)])
                open.push_back(d);
        }

        if (open.empty()) {
            stack.pop_back();
            continue;
        }

        // Choose where to go next — a generator over the available directions.
        int d = static_cast<int>(g::oneof(open)(rng));
        int nx = cx + DX[d], ny = cy + DY[d];

        m.walls[cur][d] = false;             // knock down the shared wall
        m.walls[m.idx(nx, ny)][OPP[d]] = false;
        visited[m.idx(nx, ny)] = 1;
        stack.push_back(m.idx(nx, ny));
    }
    return m;
}

// Render with box characters: each cell is 3 wide + 1 for the wall column.
std::string render(const Grid& m) {
    std::string out;
    for (int x = 0; x < m.W; ++x) out += "+---";
    out += "+\n";

    for (int y = 0; y < m.H; ++y) {
        std::string line = "|", below = "+";
        for (int x = 0; x < m.W; ++x) {
            const auto& w = m.walls[m.idx(x, y)];
            line += "   ";
            line += w[1] ? '|' : ' ';          // east wall
            below += w[2] ? "---" : "   ";      // south wall
            below += '+';
        }
        out += line + "\n" + below + "\n";
    }
    return out;
}

}  // namespace maze

int main(int argc, char** argv) {
    int W = 20, H = 10;
    unsigned seed = 42;
    if (argc > 1) W = std::atoi(argv[1]);
    if (argc > 2) H = std::atoi(argv[2]);
    if (argc > 3) seed = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10));
    if (W < 1) W = 1;
    if (H < 1) H = 1;

    std::mt19937 rng(seed);

    std::cout << "=== combinator dual: nested repeat(choice) = random grid ===\n";
    std::cout << maze::noise_grid(W, H, rng);

    std::cout << "\n=== recursive backtracker (randomness via gen combinators) ===\n";
    auto m = maze::carve(W, H, rng);
    std::cout << maze::render(m);
    return 0;
}
