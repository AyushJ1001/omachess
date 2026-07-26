//! Just enough JSON for the command-and-event boundary.
//!
//! Commands and events cross the C ABI as UTF-8 JSON so the payload shape can
//! grow across releases without changing the ABI itself. The payloads the
//! boundary carries are small and fully controlled by this crate and the
//! workspace, so a dependency-free reader and writer is enough.

/// Appends `value` to `out` as a quoted JSON string.
pub fn write_string(out: &mut String, value: &str) {
    out.push('"');
    for character in value.chars() {
        match character {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

/// Reads the string value of a top-level `"name": "..."` member.
///
/// Returns `None` when the member is absent or is not a string.
pub fn read_string_field(input: &str, name: &str) -> Option<String> {
    let bytes = input.as_bytes();
    let mut index = 0;
    while index < bytes.len() {
        if bytes[index] != b'"' {
            index += 1;
            continue;
        }
        let (key, after_key) = read_quoted(input, index)?;
        index = after_key;
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index >= bytes.len() || bytes[index] != b':' {
            continue;
        }
        index += 1;
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }
        if index < bytes.len() && bytes[index] == b'"' {
            let (value, after_value) = read_quoted(input, index)?;
            if key == name {
                return Some(value);
            }
            index = after_value;
        }
    }
    None
}

/// Reads the quoted string starting at `start`, returning it and the index
/// just past its closing quote.
fn read_quoted(input: &str, start: usize) -> Option<(String, usize)> {
    let bytes = input.as_bytes();
    let mut value = String::new();
    let mut index = start + 1;
    while index < bytes.len() {
        match bytes[index] {
            b'"' => return Some((value, index + 1)),
            b'\\' => {
                index += 1;
                let escaped = *bytes.get(index)?;
                match escaped {
                    b'n' => value.push('\n'),
                    b'r' => value.push('\r'),
                    b't' => value.push('\t'),
                    b'u' => {
                        let hex = input.get(index + 1..index + 5)?;
                        let code = u32::from_str_radix(hex, 16).ok()?;
                        value.push(char::from_u32(code)?);
                        index += 4;
                    }
                    other => value.push(other as char),
                }
                index += 1;
            }
            _ => {
                let rest = input.get(index..)?;
                let character = rest.chars().next()?;
                value.push(character);
                index += character.len_utf8();
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_a_named_string_field() {
        let input = r#"{"type": "flip_board", "reason": "shortcut"}"#;
        assert_eq!(read_string_field(input, "type").as_deref(), Some("flip_board"));
        assert_eq!(read_string_field(input, "reason").as_deref(), Some("shortcut"));
    }

    #[test]
    fn ignores_values_that_look_like_keys() {
        let input = r#"{"reason": "type", "type": "flip_board"}"#;
        assert_eq!(read_string_field(input, "type").as_deref(), Some("flip_board"));
    }

    #[test]
    fn missing_and_non_string_fields_read_as_none() {
        assert_eq!(read_string_field(r#"{"count": 3}"#, "type"), None);
        assert_eq!(read_string_field("not json", "type"), None);
    }

    #[test]
    fn escapes_round_trip() {
        let mut written = String::new();
        write_string(&mut written, "a\"b\\c\nd");
        let document = format!("{{\"value\": {written}}}");
        assert_eq!(read_string_field(&document, "value").as_deref(), Some("a\"b\\c\nd"));
    }
}
