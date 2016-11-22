#include "pretty_printing.h"

#include "hlt.hpp"
#include "networking.hpp"

#include <set>
#include <map>
#include <array>
#include <algorithm>
#include <iterator>
#include <fstream>
#include <sstream>
#include <iomanip>
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

bool experiment = false;

int myID;
int width;
int height;
int area;
vector<int> production;
vector<int> owner;
vector<int> strength;

enum class Dir {
    still = 0,
    north = 1,
    east = 2,
    south = 3,
    west = 4,
};

ostream& operator<<(ostream &out, Dir d) {
    switch(d) {
    case Dir::still: out << "x"; break;
    case Dir::north: out << "N"; break;
    case Dir::east: out << "E"; break;
    case Dir::south: out << "S"; break;
    case Dir::west: out << "W"; break;
    }
    return out;
}

const Dir all_moves[] = { Dir::north, Dir::east, Dir::south, Dir::west };

class Loc {
public:
    Loc() = default;
    Loc(int value): value(value) {}
    operator int() const {
        return value;
    }
    Loc operator++(int postfix) {
        (void)postfix;  // unused
        return value++;
    }
    static Loc pack(int x, int y) {
        return x + width * y;
    }
    hlt::Location as_hlt_loc() const {
        assert(value >= 0);
        assert(value < area);
        hlt::Location res;
        res.x = value % width;
        res.y = value / width;
        return res;
    }
private:
    int value;
};


ostream& operator<<(ostream &out, Loc loc) {
    auto hlt_loc = loc.as_hlt_loc();
    out << "<" << hlt_loc.x << "," << hlt_loc.y << ">";
    return out;
}

array<Loc, 4> neighbors(Loc p) {
    int x = p % width;
    int y = p / width;
    array<Loc, 4> result;
    result[0] = Loc::pack(x, y == 0 ? height - 1 : y - 1);
    result[1] = Loc::pack(x == width - 1 ? 0 : x + 1, y);
    result[2] = Loc::pack(x, y == height - 1 ? 0 : y + 1);
    result[3] = Loc::pack(x == 0 ? width - 1 : x - 1, y);
    return result;
}

Dir opposite(Dir dir) {
    assert(dir != Dir::still);
    return (Dir)((((int)dir - 1) ^ 2) + 1);
}

// TODO: more efficient
Loc move_dst(Loc src, Dir d) {
    assert(d != Dir::still);
    return neighbors(src)[(int)d - 1];
}
Loc move_src(Loc dst, Dir d) {
    return move_dst(dst, opposite(d));
}

void init_globals(hlt::GameMap &game_map) {
    ::width = game_map.width;
    ::height = game_map.height;
    ::area = width * height;
    ::strength.resize(area);
    ::production.resize(area);
    ::owner.resize(area);
    for (Loc p = 0; p < area; p++) {
        auto &site = game_map.getSite(p.as_hlt_loc());
        ::strength[p] = site.strength;
        ::production[p] = site.production;
        ::owner[p] = site.owner;
    }
}


void send_moves(map<Loc, Dir> moves) {
    std::set<hlt::Move> hlt_moves;
    for (auto kv : moves) {
        Loc p = kv.first;
        Dir d = kv.second;
        assert(owner[p] == myID);
        if (d != Dir::still)
            hlt_moves.insert({p.as_hlt_loc(), (unsigned char)d});
    }
    sendFrame(hlt_moves);
}


void show(const vector<int> &board) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dbg << setw(2) << board[Loc::pack(x, y)] << " ";
        dbg << endl;
    }
}


vector<int> distance_to_border;

void precompute() {
    distance_to_border = vector<int>(area, 10000);
    for (Loc p = 0; p < area; p++) {
        if (owner[p] != myID)
            continue;
        bool internal = true;
        for (Loc n : neighbors(p))
            if (owner[n] != myID)
                internal = false;
        if (!internal) {
            distance_to_border[p] = 0;
        }
    }
    for (int i = 0; i < width + height; i++)
        for (Loc p = 0; p < area; p++)
            for (Loc n : neighbors(p))
                distance_to_border[p] = min(
                    distance_to_border[p],
                    distance_to_border[n] + 1);
}


struct Plan {
    Loc target;
    vector<map<Loc, Dir>> moves;  // grouped by turns

    vector<Loc> footprint;
    int initial_strength = 0;
    int prod = 0;
    int waste = 0;
    int wait_time;

    Plan(Loc target, vector<map<Loc, Dir>> moves)
        : target(target),
          moves(moves),
          footprint {target} {
        int turn = 0;
        for (const auto ms : moves) {
            for (auto kv : ms) {
                Loc from = kv.first;
                footprint.push_back(from);
                initial_strength += strength[from] + turn * production[from];
                prod += production[from];

                if (distance_to_border[from] <=
                    distance_to_border[move_dst(from, kv.second)]) {
                    waste += production[from];
                }
            }
            turn++;
        }
        sort(begin(footprint), end(footprint));
        wait_time = compute_wait_time();
    }

    int compute_wait_time() const {
        if (initial_strength > strength[target])
            return 0;
        if (prod == 0)
            return 1000;
        int t = 0;
        int s = initial_strength;
        while (s <= strength[target]) {
            s += prod;
            t++;
        }
        return t;
    }

    map<Loc, Dir> initial_moves() const {
        map<Loc, Dir> result;
        int turn = wait_time;
        for (const auto &ms : moves) {
            for (auto kv : ms) {
                assert(result.count(kv.first) == 0);
                if (turn == 0)
                    result.insert(kv);
                else
                    result.insert({kv.first, Dir::still});
            }
            turn++;
        }
        return result;
    }

    double score() const {
        // TODO: prefer moves toward the border
        return 1.0 * production[target] /
            (strength[target] + waste + wait_time * production[target] + 1e-6);
    }
};

// http://stackoverflow.com/a/17050528/6335232
template<typename T>
vector<vector<T>> cartesian_product(const vector<vector<T>>& v) {
    vector<vector<T>> s = {{}};
    for (auto& u : v) {
        vector<vector<T>> r;
        for (const auto& x : s) {
            for (auto y : u) {
                r.push_back(x);
                r.back().push_back(y);
            }
        }
        s = move(r);
    }
    return s;
}


vector<map<Loc, Dir>> generate_approaches(const set<Loc> &targets) {
    set<Loc> froms;
    for (Loc t : targets)
        for (Loc n : neighbors(t))
            if (owner[n] == myID)
                froms.insert(n);
    vector<vector<pair<Loc, Dir>>> choices;
    for (Loc from : froms) {
        choices.push_back({{-1, Dir::still}});
        for (Dir d : all_moves)
            if (targets.count(move_dst(from, d)))
                choices.back().emplace_back(from, d);
    }
    vector<map<Loc, Dir>> result;
    for (const auto &comb : cartesian_product(choices)) {
        result.emplace_back(begin(comb), end(comb));
        result.back().erase(-1);
        if (result.back().empty())
            result.pop_back();
    }
    return result;
}


template<typename BACK_INSERTER>
void generate_capture_plans(Loc target, BACK_INSERTER emit) {
    assert(owner[target] != myID);
    for (const auto &app : generate_approaches({target})) {
        *emit++ = Plan {target, {app}};

        // Advanced planning against the enemy is pointless.
        // Skip for performance.
        if (owner[target] != 0)
            continue;

        set<Loc> layer2;
        for (auto kv : app)
            layer2.insert(kv.first);
        for (const auto &app2 : generate_approaches(layer2))
            *emit++ = Plan {target, {app2, app}};
    }
}


map<Loc, Dir> generate_capture_moves() {
    vector<Plan> plans;

    for (Loc target = 0; target < area; target++)
        if (owner[target] != myID)
            generate_capture_plans(target, back_inserter(plans));

    debug(plans.size());

    map<Loc, Dir> moves;

    while (!plans.empty()) {
        debug(plans.size());
        auto best = max_element(
            begin(plans), end(plans),
            [](const Plan &p1, const Plan &p2) {
                return p1.score() < p2.score();
            });
        for (auto kv : best->initial_moves()) {
            assert(moves.count(kv.first) == 0);
            moves.insert(kv);
        }

        debug3(best->moves, best->wait_time, best->score());

        auto f = best->footprint;
        plans.erase(
            remove_if(
                begin(plans), end(plans),
                [&f](const Plan &p) {
                    vector<Loc> overlap;
                    set_intersection(
                        begin(f), end(f),
                        begin(p.footprint), end(p.footprint),
                        back_inserter(overlap));
                    return !overlap.empty();
                }),
            end(plans));
    }
    return moves;
}


map<Loc, Dir> generate_reinforcement_moves() {
    // TODO: avoid interference with capture plans
    // (currently we just overwrite reinforcement moves with captures)
    map<Loc, Dir> moves;
    for (Loc p = 0; p < area; p++) {
        if (owner[p] != myID || distance_to_border[p] == 0)
            continue;
        if (strength[p] < 6 * production[p])
            continue;
        float best_score = -1e10;
        Dir best_dir = Dir::still;
        for (Dir d : all_moves) {
            Loc to = move_dst(p, d);
            if (distance_to_border[to] >= distance_to_border[p])
                continue;
            float score = 1000 - abs(strength[to] - 128);
            if (score > best_score) {
                best_score = score;
                best_dir = d;
            }
        }
        moves[p] = best_dir;
    }
    return moves;
}


int main(int argc, char *argv[]) {
    if (argc > 1 && argv[1] == string("experiment"))
        ::experiment = true;

    std::cout.sync_with_stdio(0);

    hlt::GameMap presentMap;
    unsigned char myID;
    getInit(myID, presentMap);
    ::myID = myID;
    init_globals(presentMap);
    precompute();
    sendInit(experiment ? "exp" : "asdf,");

    mt19937 engine;
    discrete_distribution<int> random_move {8, 2, 1, 0, 0};

    while(true) {
        dbg << "-------------" << endl;
        getFrame(presentMap);
        init_globals(presentMap);
        precompute();

        map<Loc, Dir> moves = generate_reinforcement_moves();
        debug(moves);
        auto cap = generate_capture_moves();
        debug(cap);
        moves.insert(begin(cap), end(cap));

        send_moves(moves);
    }

    return 0;
}
