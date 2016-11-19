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

        for(unsigned short a = 0; a < presentMap.height; a++) {
            for(unsigned short b = 0; b < presentMap.width; b++) {
                if (presentMap.getSite({ b, a }).owner == myID) {
                    moves.insert({
                        { b, a },
                        (unsigned char) random_move(engine) });
                }
            }
        }

        sendFrame(moves);
    }

    return 0;
}
