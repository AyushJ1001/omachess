//! The command-and-event C ABI.
//!
//! The workspace owns no chess state. It creates a session, submits commands
//! describing player intent, and drains events describing the state to draw.
//! Payloads are UTF-8 JSON so their shape can grow without ABI changes.
//!
//! See `include/omachess_core.h` for the header the workspace compiles against.

use std::cell::RefCell;
use std::ffi::{c_char, CStr, CString};

use crate::rules::Rules;
use crate::session::{CommandError, Session};
use omachess_store::{BackgroundJob, BackgroundJobState, LiveStore};

fn worker_timestamp() -> String {
    format!("{}", std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_secs())
}

#[no_mangle]
pub extern "C" fn omachess_background_jobs_recover() -> i32 {
    LiveStore::open_default()
        .map(|store| store.worker().interrupt_inflight_jobs(&worker_timestamp()))
        .is_ok_and(|result| result.is_ok()) as i32
}

#[no_mangle]
pub unsafe extern "C" fn omachess_background_job_create(id: *const c_char, record_id: *const c_char, total: u32) -> i32 {
    if id.is_null() || record_id.is_null() { return 0; }
    let (Ok(id), Ok(record_id)) = (CStr::from_ptr(id).to_str(), CStr::from_ptr(record_id).to_str()) else { return 0; };
    LiveStore::open_default().map(|store| store.worker().create_job(&BackgroundJob {
        id: id.into(), kind: "computer_analysis".into(), state: BackgroundJobState::Running,
        record_id: record_id.into(), checkpoint: 0, total, controls: vec!["pause".into(), "cancel".into(), "open".into()], payload: "{}".into(), updated_at: worker_timestamp(),
    })).is_ok_and(|result| result.is_ok()) as i32
}

#[no_mangle]
pub unsafe extern "C" fn omachess_background_job_checkpoint(id: *const c_char, checkpoint: u32, state: *const c_char) -> i32 {
    if id.is_null() || state.is_null() { return 0; }
    let (Ok(id), Ok(state)) = (CStr::from_ptr(id).to_str(), CStr::from_ptr(state).to_str()) else { return 0; };
    let Some(state) = BackgroundJobState::parse_public(state) else { return 0; };
    LiveStore::open_default()
        .map(|store| store.worker().checkpoint(id, checkpoint, state, &worker_timestamp()))
        .is_ok_and(|result| result.is_ok()) as i32
}

/// An opaque handle to a workspace session.
pub struct OmachessSession {
    inner: Session,
}

pub const OMACHESS_OK: i32 = 0;
pub const OMACHESS_ERR_UNKNOWN_COMMAND: i32 = 1;
pub const OMACHESS_ERR_MALFORMED_COMMAND: i32 = 2;
pub const OMACHESS_ERR_NULL_ARGUMENT: i32 = 3;
pub const OMACHESS_ERR_INVALID_UTF8: i32 = 4;
pub const OMACHESS_ERR_REJECTED_MOVE: i32 = 5;
pub const OMACHESS_ERR_STORE: i32 = 6;

/// Checks a smoke-probe move against the Rules Authority's standard start.
///
/// A null, non-UTF-8, or illegal move returns false.
#[no_mangle]
pub unsafe extern "C" fn omachess_standard_start_move_is_legal(uci_move: *const c_char) -> i32 {
    if uci_move.is_null() {
        return 0;
    }
    let Ok(uci_move) = CStr::from_ptr(uci_move).to_str() else {
        return 0;
    };
    Rules::new("standard", None).is_some_and(|mut rules| rules.push(uci_move)) as i32
}

/// Runs one isolated Variant Definition engine stage.
#[no_mangle]
pub unsafe extern "C" fn omachess_variant_validation_worker(
    stage: *const c_char,
    fen: *const c_char,
) -> i32 {
    if stage.is_null() || fen.is_null() {
        return 0;
    }
    let (Ok(stage), Ok(fen)) = (CStr::from_ptr(stage).to_str(), CStr::from_ptr(fen).to_str())
    else {
        return 0;
    };
    if std::env::var("OMACHESS_VARIANT_VALIDATION_WORKER").as_deref() == Ok("hang") {
        std::thread::sleep(std::time::Duration::from_secs(30));
    }
    let Some((adapter, fen)) = fen.split_once("\n--OMACHESS-FEN--\n") else {
        return 0;
    };
    if !Rules::load_variant_adapter(adapter) {
        return 0;
    }
    match stage {
        "consistency" => 1,
        "smoke" => {
            let Some(mut rules) = Rules::new("omachess", Some(fen)) else {
                return 0;
            };
            let moves = rules.legal_moves();
            let searched = rules.bounded_search(1);
            (!moves.is_empty() && searched) as i32
        }
        _ => 0,
    }
}

thread_local! {
    static LAST_ERROR: RefCell<Option<CString>> = const { RefCell::new(None) };
}

fn set_last_error(message: impl Into<String>) {
    let message = CString::new(message.into())
        .unwrap_or_else(|_| CString::new("Live Store error").expect("literal has no interior NUL"));
    LAST_ERROR.with(|slot| *slot.borrow_mut() = Some(message));
}

/// Creates a session against the Live Store at the fixed XDG location.
///
/// Returns null when the Live Store cannot be opened (for example a failed
/// fail-closed migration). Call `omachess_last_error` for the reason. The
/// caller owns a non-null handle and must release it with
/// `omachess_session_free`.
#[no_mangle]
pub extern "C" fn omachess_session_new() -> *mut OmachessSession {
    match Session::open_default() {
        Ok(inner) => Box::into_raw(Box::new(OmachessSession { inner })),
        Err(error) => {
            set_last_error(error.to_string());
            std::ptr::null_mut()
        }
    }
}

/// The most recent open failure message, or null when none is recorded.
///
/// The returned pointer is valid until the next failing `omachess_session_new`
/// call. It must not be freed by the caller.
#[no_mangle]
pub extern "C" fn omachess_last_error() -> *const c_char {
    LAST_ERROR.with(|slot| match slot.borrow().as_ref() {
        Some(message) => message.as_ptr(),
        None => std::ptr::null(),
    })
}

/// Releases a session. Passing null is a no-op.
///
/// # Safety
/// `session` must be a handle from `omachess_session_new` that has not already
/// been freed.
#[no_mangle]
pub unsafe extern "C" fn omachess_session_free(session: *mut OmachessSession) {
    if !session.is_null() {
        drop(Box::from_raw(session));
    }
}

/// Submits one command as a NUL-terminated UTF-8 JSON string.
///
/// Returns `OMACHESS_OK`, or an `OMACHESS_ERR_*` code when the command was
/// rejected. A rejected command changes no state and queues no events.
///
/// # Safety
/// `session` must be a live handle and `command_json` a valid NUL-terminated
/// string for the duration of the call.
#[no_mangle]
pub unsafe extern "C" fn omachess_session_submit(
    session: *mut OmachessSession,
    command_json: *const c_char,
) -> i32 {
    let (Some(session), false) = (session.as_mut(), command_json.is_null()) else {
        return OMACHESS_ERR_NULL_ARGUMENT;
    };
    let Ok(command) = CStr::from_ptr(command_json).to_str() else {
        return OMACHESS_ERR_INVALID_UTF8;
    };
    match session.inner.submit(command) {
        Ok(()) => OMACHESS_OK,
        Err(CommandError::UnknownCommand) => OMACHESS_ERR_UNKNOWN_COMMAND,
        Err(CommandError::MalformedCommand) => OMACHESS_ERR_MALFORMED_COMMAND,
        Err(CommandError::RejectedMove) => OMACHESS_ERR_REJECTED_MOVE,
        Err(CommandError::Store) => OMACHESS_ERR_STORE,
    }
}

/// Removes the oldest queued event and returns it as UTF-8 JSON, or null when
/// the queue is empty.
///
/// The caller owns the returned string and must release it with
/// `omachess_string_free`.
///
/// # Safety
/// `session` must be a live handle.
#[no_mangle]
pub unsafe extern "C" fn omachess_session_poll_event(session: *mut OmachessSession) -> *mut c_char {
    let Some(session) = session.as_mut() else {
        return std::ptr::null_mut();
    };
    match session.inner.poll_event() {
        // Events are generated by this crate and never contain interior NULs.
        Some(event) => CString::new(event)
            .map(CString::into_raw)
            .unwrap_or(std::ptr::null_mut()),
        None => std::ptr::null_mut(),
    }
}

/// Releases a string returned by this ABI. Passing null is a no-op.
///
/// # Safety
/// `text` must come from `omachess_session_poll_event` and must not have been
/// freed already.
#[no_mangle]
pub unsafe extern "C" fn omachess_string_free(text: *mut c_char) {
    if !text.is_null() {
        drop(CString::from_raw(text));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    // rusqlite/Open uses process-wide XDG; serialize and isolate these tests.
    static XDG_LOCK: Mutex<()> = Mutex::new(());

    struct IsolatedDataHome {
        _dir: tempfile::TempDir,
        _guard: std::sync::MutexGuard<'static, ()>,
    }

    fn isolate_xdg() -> IsolatedDataHome {
        let guard = XDG_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        let dir = tempfile::TempDir::new().unwrap();
        std::env::set_var("XDG_DATA_HOME", dir.path());
        IsolatedDataHome {
            _dir: dir,
            _guard: guard,
        }
    }

    unsafe fn drain(session: *mut OmachessSession) -> Vec<String> {
        let mut events = Vec::new();
        loop {
            let event = omachess_session_poll_event(session);
            if event.is_null() {
                return events;
            }
            events.push(CStr::from_ptr(event).to_str().unwrap().to_owned());
            omachess_string_free(event);
        }
    }

    #[test]
    fn a_session_round_trips_a_command_into_an_event() {
        let _xdg = isolate_xdg();
        unsafe {
            let session = omachess_session_new();
            assert!(!session.is_null());
            let command = CString::new(r#"{"type":"describe_board"}"#).unwrap();
            assert_eq!(
                omachess_session_submit(session, command.as_ptr()),
                OMACHESS_OK
            );
            let events = drain(session);
            assert!(
                events
                    .iter()
                    .any(|event| event.contains(r#""orientation":"white""#)),
                "describe_board must answer with the board: {events:?}"
            );
            assert!(
                events
                    .iter()
                    .any(|event| event.contains(r#""type":"library_changed""#)),
                "describe_board must list the Personal Library: {events:?}"
            );
            assert!(
                events
                    .iter()
                    .any(|event| event.contains(r#""type":"tabs_changed""#)),
                "describe_board must report open tabs: {events:?}"
            );
            omachess_session_free(session);
        }
    }

    #[test]
    fn rejected_commands_report_a_code_and_produce_no_events() {
        let _xdg = isolate_xdg();
        unsafe {
            let session = omachess_session_new();
            assert!(!session.is_null());
            let command = CString::new(r#"{"type":"castle"}"#).unwrap();
            assert_eq!(
                omachess_session_submit(session, command.as_ptr()),
                OMACHESS_ERR_UNKNOWN_COMMAND
            );
            assert!(drain(session).is_empty());
            omachess_session_free(session);
        }
    }

    #[test]
    fn null_arguments_are_reported_rather_than_dereferenced() {
        let _xdg = isolate_xdg();
        unsafe {
            let command = CString::new(r#"{"type":"flip_board"}"#).unwrap();
            assert_eq!(
                omachess_session_submit(std::ptr::null_mut(), command.as_ptr()),
                OMACHESS_ERR_NULL_ARGUMENT
            );
            let session = omachess_session_new();
            assert!(!session.is_null());
            assert_eq!(
                omachess_session_submit(session, std::ptr::null()),
                OMACHESS_ERR_NULL_ARGUMENT
            );
            omachess_session_free(session);
            omachess_session_free(std::ptr::null_mut());
            omachess_string_free(std::ptr::null_mut());
        }
    }
}
