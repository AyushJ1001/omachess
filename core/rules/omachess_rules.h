/* The Rules Authority: a C API over vendored Fairy-Stockfish.
 *
 * Fairy-Stockfish is the only source of legal moves, SAN, FEN, and game
 * results in Omachess. This header is the narrowest surface the Rust core
 * needs to ask it those four questions, and nothing else: no search, no
 * evaluation, no UCI process.
 *
 * A game is a Chess Variant plus a position plus the moves played to reach
 * it. Moves are pushed and popped, so navigating a Game Record backward and
 * forward costs one engine call rather than a replay from the start.
 *
 * Returned strings are owned by the game and stay valid until the next call
 * on that same game. A game is not thread-safe; call it from one thread at a
 * time.
 */

#ifndef OMACHESS_RULES_H
#define OMACHESS_RULES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OmachessRules OmachessRules;

/* Why a game ended. In-progress games report OMACHESS_RULES_PLAYING. */
#define OMACHESS_RULES_PLAYING 0
#define OMACHESS_RULES_CHECKMATE 1
#define OMACHESS_RULES_STALEMATE 2
#define OMACHESS_RULES_INSUFFICIENT_MATERIAL 3
#define OMACHESS_RULES_FIFTY_MOVE 4
#define OMACHESS_RULES_REPETITION 5
/* A variant-specific ending (for example a goal or extinction rule). */
#define OMACHESS_RULES_VARIANT_RULE 6

/* Who a finished game belongs to. */
#define OMACHESS_RULES_WINNER_WHITE 0
#define OMACHESS_RULES_WINNER_BLACK 1
#define OMACHESS_RULES_WINNER_DRAW 2
#define OMACHESS_RULES_WINNER_NONE 3 /* the game is still being played */

/* Creates a game of `variant` (null or "" means standard chess) at
 * `start_fen` (null or "" means the variant's own starting position).
 * Returns null when the variant is unknown or the FEN is unusable. */
OmachessRules *omachess_rules_new(const char *variant, const char *start_fen);

/* Loads one compiled adapter into this disposable process. */
int omachess_rules_load_variant(const char *adapter);

/* Releases a game. Passing null is a no-op. */
void omachess_rules_free(OmachessRules *rules);

/* The FEN of the current position. */
const char *omachess_rules_fen(OmachessRules *rules);

/* The legal moves in the current position, as space-separated UCI moves
 * ("e2e4 g1f3 ..."), or "" when there are none. */
const char *omachess_rules_legal_moves(OmachessRules *rules);

/* The SAN of `uci_move` in the current position, or "" when it is illegal.
 * The position does not change. */
const char *omachess_rules_san(OmachessRules *rules, const char *uci_move);

/* Plays `uci_move`. Returns 1 when it was legal and applied, 0 otherwise;
 * an illegal move changes nothing. */
int omachess_rules_push(OmachessRules *rules, const char *uci_move);

/* Runs a depth-bounded Fairy-Stockfish search. */
int omachess_rules_bounded_search(OmachessRules *rules, int depth);

/* Takes back the last pushed move. Returns 1, or 0 at the starting
 * position. */
int omachess_rules_pop(OmachessRules *rules);

/* 0 when White is to move in the current position, 1 when Black is. */
int omachess_rules_side_to_move(OmachessRules *rules);

/* Whether the side to move is in check. */
int omachess_rules_in_check(OmachessRules *rules);
/* Result when `loser` (0 White, 1 Black) forfeits on time. */
int omachess_rules_time_forfeit_winner(OmachessRules *rules, int loser);

/* Why the game ends in the current position: an OMACHESS_RULES_* termination.
 * Repetition and the fifty-move rule end a game here rather than waiting for
 * a claim, so a game always reports the same result to both players. */
int omachess_rules_termination(OmachessRules *rules);

/* Who won in the current position: an OMACHESS_RULES_WINNER_* value. */
int omachess_rules_winner(OmachessRules *rules);

#ifdef __cplusplus
}
#endif

#endif /* OMACHESS_RULES_H */
