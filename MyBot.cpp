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

Dir opposite(Dir dir) {
    assert(dir != Dir::still);
    return (Dir)((((int)dir - 1) ^ 2) + 1);
}

Dir turn_cw(Dir d) {
    assert(d != Dir::still);
    return (Dir)((int)d % 4 + 1);
}

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
    int x() const {
        assert(value >= 0 && value < area);
        return value % width;
    }
    int y() const {
        assert(value >= 0 && value < area);
        return value / width;
    }
    Loc offset(int dx, int dy) const {
        return pack(
            (x() + dx + width) % width,
            (y() + dy + height) % height);
    }
    hlt::Location as_hlt_loc() const {
        hlt::Location res;
        res.x = x();
        res.y = y();
        return res;
    }
private:
    int value;
};


ostream& operator<<(ostream &out, Loc loc) {
    out << "<" << loc.x() << "," << loc.y() << ">";
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
        if (initial_strength > strength[target] ||
            initial_strength == 255)
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


vector<map<Loc, Dir>> generate_approaches(
    const set<Loc> &targets, const set<Loc> &forbidden) {

    set<Loc> froms;
    for (Loc t : targets)
        for (Loc n : neighbors(t))
            if (owner[n] == myID && forbidden.count(n) == 0)
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
void generate_capture_plans(
    Loc target, const set<Loc> &forbidden, BACK_INSERTER emit) {

    assert(owner[target] == 0);
    for (const auto &app : generate_approaches({target}, forbidden)) {
        *emit++ = Plan {target, {app}};

        set<Loc> layer2;
        for (auto kv : app)
            layer2.insert(kv.first);
        for (const auto &app2 : generate_approaches(layer2, forbidden))
            *emit++ = Plan {target, {app2, app}};
    }
}


map<Loc, Dir> generate_capture_moves(const set<Loc> &forbidden) {
    vector<Plan> plans;

    for (Loc target = 0; target < area; target++)
        if (owner[target] == 0)
            generate_capture_plans(target, forbidden, back_inserter(plans));

    map<Loc, Dir> moves;

    while (!plans.empty()) {
        auto best = max_element(
            begin(plans), end(plans),
            [](const Plan &p1, const Plan &p2) {
                return p1.score() < p2.score();
            });
        for (auto kv : best->initial_moves()) {
            assert(moves.count(kv.first) == 0);
            moves.insert(kv);
        }

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


struct DiamondOutcome {
    int strength = -1;
    int owner = -1;
    int evaluate(Loc p) const {
        if (owner == 0)
            return 0;
        int res = strength + ::production[p];
        if (owner == myID)
            return res;
        else
            return -res;
    }
};

template<typename F>
DiamondOutcome simulate_diamond(Loc p, const F &get_move) {
    const int MAX_ID = 7;
    assert(owner[p] < MAX_ID);

    DiamondOutcome result;

    int arrive[MAX_ID] = {0};
    bool attack[MAX_ID] = {false};
    int damage[MAX_ID] = {0};  // from neighbors

    attack[owner[p]] = true;
    if (get_move(p) == Dir::still) {
        arrive[owner[p]] += strength[p];
        if (owner[p])
            arrive[owner[p]] += production[p];
    } else {
        arrive[owner[p]] = 0;
        damage[owner[p]] += strength[p];
    }

    for (Dir d : all_moves) {
        Loc q = move_src(p, d);
        Dir move = get_move(q);
        if (move == d)
            arrive[owner[q]] += strength[q];
        if ((move == d || move == Dir::still) && owner[q]) {
            attack[owner[q]] = true;
            if (move == Dir::still)
                damage[owner[q]] += strength[q] + production[q];
        }

        Dir d2 = turn_cw(d);
        Loc q2 = move_src(q, d2);
        move = get_move(q2);
        if (owner[q2] && (move == d || move == d2)) {
            attack[owner[q2]] = true;
            damage[owner[q2]] += strength[q2];
        }

        q2 = move_src(q, d);
        move = get_move(q2);
        if (owner[q2] && move == d) {
            attack[owner[q2]] = true;
            damage[owner[q2]] += strength[q2];
        }
    }
    assert(damage[0] == 0);

    // cout << vector<int>(arrive, arrive + MAX_ID) << endl;
    // cout << vector<int>(damage, damage + MAX_ID) << endl;

    int survive[MAX_ID];
    for (int i = 0; i < MAX_ID; i++)
        survive[i] = min(arrive[i], 255);

    for (int i = 0; i < MAX_ID; i++) {
        if (survive[i] <= 0)
            continue;
        for (int j = 0; j < MAX_ID; j++) {
            if (i != j) {
                survive[i] -= arrive[j];
                if (i)
                    survive[i] -= damage[j];
            }
        }
    }

    int cnt = 0;
    for (int i = 0; i < MAX_ID; i++) {
        if (survive[i] > 0) {
            cnt++;
            result.owner = i;
            result.strength = survive[i];
        }
    }
    // cout << vector<int>(survive, survive + MAX_ID) << endl;
    // cout << endl;
    assert(cnt <= 1);

    if (cnt == 0) {
        result.strength = 0;
        result.owner = owner[p];
        for (int i = 1; i < MAX_ID; i++) {
            if (attack[i] && i != owner[p]) {
                result.owner = 0;
                break;
            }
        }
    }

    return result;
}


vector<int> input_board() {
    vector<int> result(area);
    for (int &x : result)
        cin >> x;
    return result;
}

int test_simulate_diamond() {
    int errors = 0;
    int i = 0;
    while (cin >> ::width) {
        cin >> ::height;
        ::area = width * height;
        cout << "test#" << i << ": " << ::width << " x " << ::height << endl;

        string s;
        cin >> s;
        assert(s == "production");
        ::production = input_board();

        cin >> s;
        assert(s == "owner");
        ::owner = input_board();

        cin >> s;
        assert(s == "strength");
        ::strength = input_board();

        cin >> s;
        assert(s == "moves");
        auto moves = input_board();

        cin >> s;
        assert(s == "next_owner");
        auto next_owner = input_board();

        cin >> s;
        assert(s == "next_strength");
        auto next_strength = input_board();

        auto get_move = [&moves](Loc p) { return (Dir)moves[p]; };
        for (Loc p = 0; p < area; p++) {
            auto res = simulate_diamond(p, get_move);
            if (res.owner != next_owner[p] || res.strength != next_strength[p]) {
                cout << p << endl;
                cout << "production       owner       strength            moves"
                     << endl;
                for (int i = -2; i <= 2; i++) {
                    for (int j = -2; j <= 2; j++)
                        cout << setw(2) << production[p.offset(j, i)] << " ";
                    cout << "  ";
                    for (int j = -2; j <= 2; j++)
                        cout << owner[p.offset(j, i)] << " ";
                    cout << "  ";
                    for (int j = -2; j <= 2; j++)
                        cout << setw(3) << strength[p.offset(j, i)] << " ";
                    cout << "  ";
                    for (int j = -2; j <= 2; j++)
                        cout << (Dir)moves[p.offset(j, i)] << " ";
                    cout << endl;
                }
                cout << "Expected: "
                     << next_owner[p] << ", "
                     << next_strength[p] << endl;
                cout << "Got:      "
                     << res.owner << ", "
                     << res.strength << endl;
                cout << endl;
                errors++;
                terminate();
            }
        }

        i++;
    }

    if (errors) {
        cout << errors << " errors" << endl;
        return 1;
    } else {
        cout << "ok" << endl;
        return 0;
    }
}


vector<Loc> enumerate_neighborhood(Loc p, int radius) {
    assert(radius >= 0);
    vector<Loc> result;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius + abs(dy); dx <= radius - abs(dy); dx++) {
            result.push_back(p.offset(dx, dy));
        }
    }
    return result;
}


vector<Dir> moves_scratch;
struct GetMoveScratch {
    Dir operator()(Loc p) const { return moves_scratch[p]; }
} get_move_scratch;


class OpponentModel {
public:
    float evaluate_board(const map<Loc, Dir> &moves) const {
        for (auto kv : moves)
            moves_scratch[kv.first] = kv.second;
        float result = 0.0;
        for (int p = 0; p < area; p++)
            result += simulate_diamond(p, get_move_scratch).evaluate(p);
        return result;
    }
};


vector<Loc> list_combat_pieces() {
    vector<Loc> result;
    for (Loc p = 0; p < area; p++) {
        if (owner[p] != myID)
            continue;
        bool combat = false;
        for (Loc n : enumerate_neighborhood(p, 2)) {
            if (owner[n] && owner[n] != owner[p])
                combat = true;
        }
        if (combat)
            result.push_back(p);
    }
    return result;
}


map<Loc, Dir> generate_combat_moves(const vector<Loc> &combat_pieces) {
    map<Loc, Dir> result;
    for (Loc p : combat_pieces)
        result[p] = Dir::still;

    auto base_score = OpponentModel().evaluate_board(result);

    for (int pass = 0; pass < 3; pass++) {
        for (auto p : combat_pieces) {
            auto ns = enumerate_neighborhood(p, 2);

            moves_scratch[p] = Dir::still;
            Dir best_move = Dir::still;
            int best_score = 0;
            for (Loc n : ns)
                best_score += simulate_diamond(n, get_move_scratch).evaluate(n);

            for (Dir d : all_moves) {
                moves_scratch[p] = d;
                int score = 0;
                for (Loc n : ns)
                    score += simulate_diamond(n, get_move_scratch).evaluate(n);
                if (score > best_score) {
                    debug2(score, best_score);
                    best_score = score;
                    best_move = d;
                }
            }
            moves_scratch[p] = best_move;
            result[p] = best_move;
        }
    }

    auto final_score = OpponentModel().evaluate_board(result);
    debug2(base_score, final_score);
    assert(final_score >= base_score);

    return result;
}


int main(int argc, char *argv[]) {
    if (argc > 1 && argv[1] == string("test")) {
        return test_simulate_diamond();
    }

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

    while (true) {
        dbg << "-------------" << endl;
        getFrame(presentMap);
        init_globals(presentMap);
        precompute();

        auto combat_pieces = list_combat_pieces();

        map<Loc, Dir> moves = generate_reinforcement_moves();
        debug(moves);
        auto cap = generate_capture_moves(
            {begin(combat_pieces), end(combat_pieces)});
        debug(cap);
        moves.insert(begin(cap), end(cap));

        ::moves_scratch = vector<Dir>(area, Dir::still);
        for (auto kv : moves)
            moves_scratch[kv.first] = kv.second;

        auto combat_moves = generate_combat_moves(combat_pieces);
        debug(combat_moves);
        moves.insert(begin(combat_moves), end(combat_moves));

        send_moves(moves);
    }

    return 0;
}
