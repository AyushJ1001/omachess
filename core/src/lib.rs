//! The Omachess core: the owner of chess state for the workspace.
//!
//! Everything the workspace draws comes from here, over the command-and-event
//! C ABI in [`ffi`]. The workspace holds no chess state of its own.

pub mod board;
pub mod ffi;
pub mod game;
mod json;
pub mod rules;
pub mod session;
