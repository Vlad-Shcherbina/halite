#include "pretty_printing.h"

#include "hlt.hpp"
#include "networking.hpp"

#include <set>
#include <map>
#include <array>
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


int myID;
int width;
int height;
int area;
vector<int> production;
vector<int> owner;
vector<int> strength;

using Loc = int;
using Dir = int;

Loc pack(int x, int y) {
    return x + width * y;
}
hlt::Location unpack_loc(Loc p) {
    assert(p >= 0);
    assert(p < area);
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

Dir opposite(Dir dir) {
    assert(dir >= 1 && dir <= 4);
    return ((dir - 1) ^ 2) + 1;
}


void init_globals(hlt::GameMap &game_map) {
    ::width = game_map.width;
    ::height = game_map.height;
    ::area = width * height;
    ::strength.resize(area);
    ::production.resize(area);
    ::owner.resize(area);
    for (Loc p = 0; p < area; p++) {
        auto &site = game_map.getSite(unpack_loc(p));
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
        assert(d >= 0 && d <= 4);
        hlt_moves.insert({unpack_loc(p), (unsigned char)d});
    }
    sendFrame(hlt_moves);
}


void show(const vector<int> &board) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dbg << setw(2) << board[pack(x, y)] << " ";
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


map<Loc, Dir> generate_attack_moves() {
    map<Loc, Dir> moves;

    set<Loc> candidates;
    for (Loc p = 0; p < area; p++) {
        if (distance_to_border[p])
            continue;
        for (Loc n : neighbors(p)) {
            if (owner[n] != myID)
                candidates.insert(n);
        }
    }

    while (true) {
        map<Loc, Dir> best_moves;
        float best_score = -1;

        for (Loc c : candidates) {
            int threat = strength[c];

            vector<vector<Dir>> dss = {
                {1}, {2}, {3}, {4},
                {1, 2}, {2, 3}, {3, 4}, {4, 1}};

            for (auto ds : dss) {
                int attack = 0;
                int prod_loss = 0;
                map<Loc, Dir> mv;
                bool valid = true;
                for (Dir d : ds) {
                    Loc from = neighbors(c)[opposite(d) - 1];
                    if (owner[from] != myID || strength[from] == 0)
                        valid = false;
                    if (moves.count(from))
                        valid = false;
                    mv[from] = d;
                    attack += strength[from];
                    prod_loss += production[from];
                }
                if (!valid)
                    continue;
                if (attack > threat) {
                    float score = production[c] / (threat + prod_loss + 1);
                    if (score > best_score) {
                        best_score = score;
                        best_moves = mv;
                    }
                }
            }
        }

        if (best_score < 0) {
            break;
        }

        for (auto kv : best_moves) {
            Loc to = neighbors(kv.first)[kv.second - 1];
            candidates.erase(to);
        }
        moves.insert(begin(best_moves), end(best_moves));
    }

    return moves;
}


map<Loc, Dir> generate_reinforcement_moves() {
    map<Loc, Dir> moves;
    for (Loc p = 0; p < area; p++) {
        if (owner[p] != myID || distance_to_border[p] == 0)
            continue;
        if (strength[p] < 6 * production[p])
            continue;
        float best_score = -1e10;
        Dir best_dir = 0;
        for (Dir d : CARDINALS) {
            Loc to = neighbors(p)[d - 1];
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


int main() {
    std::cout.sync_with_stdio(0);

    hlt::GameMap presentMap;
    unsigned char myID;
    getInit(myID, presentMap);
    ::myID = myID;
    init_globals(presentMap);
    precompute();
    sendInit("asdf,");

    mt19937 engine;
    discrete_distribution<int> random_move {8, 2, 1, 0, 0};

    while(true) {
        dbg << "-------------" << endl;
        getFrame(presentMap);
        init_globals(presentMap);
        precompute();

        map<Loc, Dir> moves = generate_attack_moves();
        debug(moves);
        auto re = generate_reinforcement_moves();
        debug(re);
        moves.insert(begin(re), end(re));

        send_moves(moves);
    }

    return 0;
}
