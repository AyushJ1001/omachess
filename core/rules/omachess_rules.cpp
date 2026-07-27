/* The Rules Authority: vendored Fairy-Stockfish behind a C API.
 *
 * This file is the only place in Omachess that knows how to ask
 * Fairy-Stockfish a question. It adds no chess knowledge of its own — every
 * answer here is the engine's answer, translated into the C shapes described
 * in omachess_rules.h. Because it is the Rules Authority, nothing
 * above it can drift from it.
 *
 * It is modelled on the engine's own library bindings (src/ffishjs.cpp),
 * which is the upstream-sanctioned way to use Fairy-Stockfish in process.
 */

#include <deque>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "apiutil.h"
#include "bitboard.h"
#include "movegen.h"
#include "piece.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "uci.h"
#include "variant.h"

#include "omachess_rules.h"

using namespace Stockfish;

namespace {

// The engine keeps its piece tables, variant registry, and notation settings
// in process-wide globals, so only one game may be inside it at a time.
std::mutex &engine_mutex()
{
    static std::mutex mutex;
    return mutex;
}

// The engine's global tables, built exactly once per process.
void initialise_engine_once()
{
    static std::once_flag once;
    std::call_once(once, [] {
        pieceMap.init();
        variants.init();
        UCI::init(Options);
        Bitboards::init();
        Position::init();
        Bitbases::init();
        // A position counts the nodes it visits against a thread even when
        // nobody is searching, so the engine needs its one thread. Omachess
        // never searches with it, so its transposition table is shrunk to the
        // smallest the engine allows rather than left at the search default.
        Threads.set(1);
        Options["Hash"] = std::string("1");
    });
}

const Variant *find_variant(const std::string &name)
{
    const std::string key = name.empty() || name == "standard" ? "chess" : name;
    const auto found = variants.find(key);
    return found == variants.end() ? nullptr : found->second;
}

} // namespace

extern "C" int omachess_rules_load_variant(const char *adapter)
{
    if (!adapter)
        return 0;
    std::lock_guard<std::mutex> lock(engine_mutex());
    initialise_engine_once();
    std::istringstream input(adapter);
    variants.parse_istream<false>(input);
    return variants.find("omachess") != variants.end() ? 1 : 0;
}

// One game: a variant, the position reached so far, and the moves that
// reached it. The move stack is what makes pop cheap.
struct OmachessRules {
    const Variant *variant = nullptr;
    std::unique_ptr<std::deque<StateInfo>> states;
    Position position;
    std::vector<Move> played;
    // Backs the strings the C API hands out, so callers borrow rather than
    // free.
    std::string scratch;
};

namespace {

// Holds the engine for the duration of one call and points its global piece
// and notation tables at this game's variant. Every entry point takes one, so
// two games are never inside the engine at once and each one always sees its
// own rules.
class Borrowed {
public:
    explicit Borrowed(const OmachessRules *rules)
        : lock(engine_mutex())
    {
        select_variant(rules->variant);
    }

    // Points the engine's global piece and notation tables at `variant`.
    // Rebuilding them is expensive, so a game that is already the one in the
    // engine costs nothing — which is every call in a single-game workspace.
    static void select_variant(const Variant *variant)
    {
        if (current == variant)
            return;
        UCI::init_variant(variant);
        current = variant;
    }

private:
    static const Variant *current;

    std::lock_guard<std::mutex> lock;
};

const Variant *Borrowed::current = nullptr;

} // namespace

OmachessRules *omachess_rules_new(const char *variant_name, const char *start_fen)
{
    const std::lock_guard<std::mutex> lock(engine_mutex());
    initialise_engine_once();

    const Variant *variant = find_variant(variant_name ? variant_name : "");
    if (!variant)
        return nullptr;
    Borrowed::select_variant(variant);

    const std::string fen = start_fen && *start_fen ? std::string(start_fen) : variant->startFen;
    // The engine validates the position; a FEN it cannot use is not a game.
    if (FEN::validate_fen(fen, variant, false) != FEN::FEN_OK)
        return nullptr;

    auto *rules = new OmachessRules();
    rules->variant = variant;
    rules->states = std::make_unique<std::deque<StateInfo>>(1);
    rules->position.set(variant, fen, false, &rules->states->back(), Threads.main());
    return rules;
}

void omachess_rules_free(OmachessRules *rules)
{
    delete rules;
}

const char *omachess_rules_fen(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    rules->scratch = rules->position.fen();
    return rules->scratch.c_str();
}

const char *omachess_rules_legal_moves(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    rules->scratch.clear();
    for (const ExtMove &move : MoveList<LEGAL>(rules->position)) {
        if (!rules->scratch.empty())
            rules->scratch += ' ';
        rules->scratch += UCI::move(rules->position, move);
    }
    return rules->scratch.c_str();
}

const char *omachess_rules_san(OmachessRules *rules, const char *uci_move)
{
    const Borrowed borrowed(rules);

    rules->scratch.clear();
    if (!uci_move)
        return rules->scratch.c_str();

    std::string text(uci_move);
    // to_move only matches legal moves, so this is the legality check too.
    const Move move = UCI::to_move(rules->position, text);
    if (move != MOVE_NONE)
        rules->scratch = SAN::move_to_san(rules->position, move, NOTATION_SAN);
    return rules->scratch.c_str();
}

int omachess_rules_push(OmachessRules *rules, const char *uci_move)
{
    const Borrowed borrowed(rules);

    if (!uci_move)
        return 0;

    std::string text(uci_move);
    const Move move = UCI::to_move(rules->position, text);
    if (move == MOVE_NONE)
        return 0;

    // A deque never invalidates references to the states already in it, which
    // the engine's own state chain relies on.
    rules->states->emplace_back();
    rules->position.do_move(move, rules->states->back());
    rules->played.push_back(move);
    return 1;
}

int omachess_rules_bounded_search(OmachessRules *rules, int depth)
{
    if (!rules || depth < 1 || depth > 4)
        return 0;
    Borrowed borrowed(rules);
    Search::LimitsType limits;
    limits.depth = depth;
    Threads.start_thinking(rules->position, rules->states, limits, false);
    Threads.main()->wait_for_search_finished();
    return 1;
}

int omachess_rules_pop(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    if (rules->played.empty())
        return 0;
    rules->position.undo_move(rules->played.back());
    rules->played.pop_back();
    rules->states->pop_back();
    return 1;
}

int omachess_rules_side_to_move(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    return rules->position.side_to_move() == WHITE ? 0 : 1;
}

int omachess_rules_in_check(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    return rules->position.checkers() ? 1 : 0;
}

int omachess_rules_time_forfeit_winner(OmachessRules *rules, int loser)
{
    const Borrowed borrowed(rules);
    const Color winner = loser == 0 ? BLACK : WHITE;
    if (has_insufficient_material(winner, rules->position))
        return OMACHESS_RULES_WINNER_DRAW;
    return winner == WHITE ? OMACHESS_RULES_WINNER_WHITE : OMACHESS_RULES_WINNER_BLACK;
}

namespace {

// The engine's game-end questions, asked in the order its own library
// bindings ask them, and reported as a termination plus a winner.
struct Ending {
    int termination = OMACHESS_RULES_PLAYING;
    Value result = VALUE_DRAW;
};

Ending ending_of(Position &position)
{
    Ending ending;

    if (position.is_immediate_game_end(ending.result)) {
        ending.termination = OMACHESS_RULES_VARIANT_RULE;
        return ending;
    }

    if (has_insufficient_material(WHITE, position) && has_insufficient_material(BLACK, position)) {
        ending.termination = OMACHESS_RULES_INSUFFICIENT_MATERIAL;
        ending.result = VALUE_DRAW;
        return ending;
    }

    if (MoveList<LEGAL>(position).size() == 0) {
        const bool in_check = bool(position.checkers());
        ending.termination = in_check ? OMACHESS_RULES_CHECKMATE : OMACHESS_RULES_STALEMATE;
        ending.result = in_check ? position.checkmate_value() : position.stalemate_value();
        return ending;
    }

    if (position.is_optional_game_end(ending.result)) {
        // Both endings live behind the same engine question; the halfmove
        // clock is what separates them.
        ending.termination = position.rule50_count() > 2 * position.n_move_rule() - 1
            ? OMACHESS_RULES_FIFTY_MOVE
            : OMACHESS_RULES_REPETITION;
        return ending;
    }

    return ending;
}

} // namespace

int omachess_rules_termination(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    return ending_of(rules->position).termination;
}

int omachess_rules_winner(OmachessRules *rules)
{
    const Borrowed borrowed(rules);

    const Ending ending = ending_of(rules->position);
    if (ending.termination == OMACHESS_RULES_PLAYING)
        return OMACHESS_RULES_WINNER_NONE;
    if (ending.result == VALUE_DRAW)
        return OMACHESS_RULES_WINNER_DRAW;

    // Terminal values are from the point of view of the side to move, so
    // Black's turn inverts them into White's.
    const bool white_won =
        rules->position.side_to_move() == WHITE ? ending.result > VALUE_DRAW : ending.result < VALUE_DRAW;
    return white_won ? OMACHESS_RULES_WINNER_WHITE : OMACHESS_RULES_WINNER_BLACK;
}
