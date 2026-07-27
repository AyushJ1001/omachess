/* The Omachess command-and-event C ABI.
 *
 * The workspace owns no chess state. It creates a session, submits commands
 * describing player intent, and drains events describing the state to draw.
 * Both payloads are UTF-8 JSON so their shape can grow across releases
 * without changing this ABI.
 *
 * Commands:
 *   {"type":"describe_board"}  ask for the current board without changing it
 *   {"type":"flip_board"}      swap which side is at the bottom
 *   {"type":"play_move","from":"e2","to":"e4"}
 *                              play a move; add "promotion":"queen|rook|
 *                              bishop|knight" for a promotion
 *   {"type":"navigate","to":"backward|forward|start|end"}
 *                              move which position of the game is displayed
 *   {"type":"restore_record"}  reload the Game Record offered after restart
 *   {"type":"dismiss_restore"} decline the restore offer
 *
 * Events:
 *   {"type":"board_changed",...}
 *   {"type":"restore_available","recordId":"...","plyCount":N,
 *    "label":"Restore previous game"}
 *                              offered after describe_board when workspace
 *                              residue points at a prior Game Record
 *   {"type":"restore_cleared"} the restore offer is gone
 *
 *   The 64 squares are in reading order for the current orientation: the
 *   top-left drawn square first, the bottom-right last.
 *
 *   "moves" holds every move the player may make in the displayed position,
 *   grouped by the squares it joins, and is empty while an earlier position is
 *   being reviewed or once the game has a result. "cursor" counts how many
 *   moves of "moveList" the displayed position includes.
 *
 *   Every chess answer in an event comes from Fairy-Stockfish. A workspace
 *   that computes one for itself can only drift from the game.
 *
 * A session is not thread-safe; call it from one thread at a time.
 */

#ifndef OMACHESS_CORE_H
#define OMACHESS_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OmachessSession OmachessSession;

#define OMACHESS_OK 0
#define OMACHESS_ERR_UNKNOWN_COMMAND 1
#define OMACHESS_ERR_MALFORMED_COMMAND 2
#define OMACHESS_ERR_NULL_ARGUMENT 3
#define OMACHESS_ERR_INVALID_UTF8 4
#define OMACHESS_ERR_REJECTED_MOVE 5
#define OMACHESS_ERR_STORE 6

/* Creates a session against the Live Store at the fixed XDG location. Returns
 * NULL when the store cannot be opened (fail-closed migration, I/O error).
 * Call omachess_last_error for the reason. The caller owns a non-NULL handle
 * and must release it with omachess_session_free. */
OmachessSession *omachess_session_new(void);

/* The most recent omachess_session_new failure message, or NULL. Valid until
 * the next failing omachess_session_new call. Do not free. */
const char *omachess_last_error(void);

/* Releases a session. Passing NULL is a no-op. */
void omachess_session_free(OmachessSession *session);

/* Submits one command as NUL-terminated UTF-8 JSON. Returns OMACHESS_OK, or an
 * OMACHESS_ERR_* code. A rejected command changes no state and queues no
 * events. */
int32_t omachess_session_submit(OmachessSession *session, const char *command_json);

/* Removes the oldest queued event and returns it as UTF-8 JSON, or NULL when
 * the queue is empty. The caller owns the string and must release it with
 * omachess_string_free. */
char *omachess_session_poll_event(OmachessSession *session);

/* Releases a string returned by this ABI. Passing NULL is a no-op. */
void omachess_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* OMACHESS_CORE_H */
