#include "pretty_printing.h"

#include "hlt.hpp"
#include "networking.hpp"

#include <set>
#include <map>
#include <array>
#include <fstream>
#include <sstream>
#include <random>
#include <assert.h>

using namespace std;

ofstream dbg("zzz.log");

#define debug(x) \
    dbg << #x " = " << (x) << std::endl
#define debug2(x, y) \
    dbg << #x " = " << (x) \
         << ", " #y " = " << (y) << std::endl
#define debug3(x, y, z) \
    dbg << #x " = " << (x) \
         << ", " #y " = " << (y) \
         << ", " #z " = " << (z) << std::endl


unsigned char myID;
int width;
int height;
vector<int> production;
vector<int> owner;
vector<int> strength;

using Loc = int;

Loc pack(int x, int y) {
    return x + width * y;
}
hlt::Location unpack_loc(Loc p) {
    assert(p >= 0);
    assert(p < width * height);
    hlt::Location res;
    res.x = p % width;
    res.y = p / width;
    return res;
}

array<Loc, 4> neighbors(Loc p) {
    int x = p % width;
    int y = p / width;
    array<Loc, 4> result;
    result[0] = pack(x, y == 0 ? height - 1 : y - 1);
    result[1] = pack(x == width - 1 ? 0 : x + 1, y);
    result[2] = pack(x, y == height - 1 ? 0 : y + 1);
    result[3] = pack(x == 0 ? width - 1 : x - 1, y);
    return result;
}

int opposite(int dir) {
    assert(dir >= 1 && dir <= 4);
    return ((dir - 1) ^ 2) + 1;
}


struct Opener {
    struct State {
        Opener *op;
        set<int> own;
        map<int, int> strength_diff;

        vector<map<Loc, int>> moves;

        int score;
        int evaluate(int ahead) const {
            int result = 0;
            for (Loc p : own)
                result += strength(p) + ahead * production[p];
            return result;
        }

        int strength(Loc p) const {
            if (strength_diff.count(p))
                return strength_diff.at(p);
            else
                return ::strength[p];
        }
        vector<State> successors() const {
            State s = *this;
            s.moves.emplace_back();
            for (Loc p : own)
                s.strength_diff[p] =
                    min(s.strength(p) + production[p], 255);

            vector<State> result {s};

            set<int> candidates;
            for (Loc p : own) {
                for (Loc n : neighbors(p))
                    if (!own.count(n))
                        candidates.insert(n);
            }
            for (Loc c : candidates) {
                for (int mask = 1; mask < 16; mask++) {
                    int force = 0;
                    bool valid = true;
                    map<Loc, int> moves;
                    for (int dir = 1; dir <= 4; dir++) {
                        Loc from = neighbors(c)[opposite(dir) - 1];
                        bool have_move = mask & (1 << (dir - 1));
                        if (have_move && !own.count(from)) {
                            valid = false;
                            break;
                        }
                        if (have_move) {
                            force += strength(from);
                            moves[from] = dir;
                        }
                    }
                    if (!valid)
                        continue;
                    if (force <= strength(c) && force < 128)
                        continue;

                    State s2 = s;
                    int d = force - strength(c);
                    for (auto from_dir : moves)
                        s2.strength_diff[from_dir.first] = 0;
                    if (d > 0) {
                        s2.strength_diff[c] = d;
                        s2.own.insert(c);
                    } else {
                        s2.strength_diff[c] = -d;
                    }
                    s2.moves.back() = moves;
                    result.push_back(s2);
                }
            }
            return result;
        }
    };

    State initial;

    Opener() {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Loc p = pack(x, y);
                if (owner[p] == myID) {
                    initial.own.insert(p);
                    initial.strength_diff[p] = strength[p];
                }
            }
        }
        initial.op = this;
    }

    friend ostream &operator<<(ostream &out, const Opener::State &state);

    State solve() {
        vector<State> beam{initial};
        for (int step = 0; step < 10; step++) {
            vector<State> next_beam;
            for (const State &s : beam) {
                for (const State &s2 : s.successors()) {
                    next_beam.push_back(s2);
                    next_beam.back().score = next_beam.back().evaluate(100);
                }
            }
            sort(next_beam.begin(), next_beam.end(),
                 [](const State &s1, const State &s2) {
                     return s1.score > s2.score;
                 });
            const int MAX_BEAM_WIDTH = 4;
            if (next_beam.size() > MAX_BEAM_WIDTH)
                next_beam.resize(MAX_BEAM_WIDTH);
            beam = next_beam;
        }
        return beam.front();
    }
};

ostream &operator<<(ostream &out, const Opener::State &state) {
    out << "State(" << state.own
        << ", diff=" << state.strength_diff
        << ", moves=" << state.moves << ")";
    return out;
}


void init_globals(hlt::GameMap &game_map) {
    ::width = game_map.width;
    ::height = game_map.height;
    int n = width * height;
    ::strength.resize(n);
    ::production.resize(n);
    ::owner.resize(n);
    for (Loc p = 0; p < n; p++) {
        auto &site = game_map.getSite(unpack_loc(p));
        ::strength[p] = site.strength;
        ::production[p] = site.production;
        ::owner[p] = site.owner;
    }
}


int main() {
    std::cout.sync_with_stdio(0);

    hlt::GameMap presentMap;
    getInit(::myID, presentMap);
    init_globals(presentMap);
    sendInit("asdf.");

    mt19937 engine;
    discrete_distribution<int> random_move {8, 2, 1, 0, 0};

    while(true) {
        dbg << "-------------" << endl;
        getFrame(presentMap);
        init_globals(presentMap);
        std::set<hlt::Move> moves;

        Opener opener;
        if (opener.initial.own.size() < 30) {
            debug(opener.initial);
            auto s = opener.solve();
            debug(s.moves);
            for (auto kv : s.moves.front()) {
                auto loc = unpack_loc(kv.first);
                unsigned char dir = kv.second;
                moves.insert({loc, dir});
            }
        } else {
            for(unsigned short a = 0; a < presentMap.height; a++) {
                for(unsigned short b = 0; b < presentMap.width; b++) {
                    if (presentMap.getSite({ b, a }).owner == myID) {
                        moves.insert({
                            { b, a },
                            (unsigned char) random_move(engine) });
                    }
                }
            }
        }

        sendFrame(moves);
    }

    return 0;
}
