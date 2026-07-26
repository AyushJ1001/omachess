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
/// Returns `None` when the member is absent, is not a string, or when the
/// input is not a valid complete JSON object.
pub fn read_string_field(input: &str, name: &str) -> Option<String> {
    let bytes = input.as_bytes();
    let mut index = 0;

    // Skip leading whitespace
    while index < bytes.len() && bytes[index].is_ascii_whitespace() {
        index += 1;
    }

    // Expect opening brace
    if index >= bytes.len() || bytes[index] != b'{' {
        return None;
    }
    index += 1;

    let mut result = None;
    let mut depth = 0; // Track nesting depth relative to the top-level object
    let mut expect_comma_or_close = false;

    while index < bytes.len() {
        // Skip whitespace
        while index < bytes.len() && bytes[index].is_ascii_whitespace() {
            index += 1;
        }

        if index >= bytes.len() {
            return None; // Truncated
        }

        // Check for closing brace at top level
        if bytes[index] == b'}' && depth == 0 {
            index += 1;
            // Skip trailing whitespace
            while index < bytes.len() && bytes[index].is_ascii_whitespace() {
                index += 1;
            }
            // Reject trailing non-whitespace
            if index < bytes.len() {
                return None;
            }
            return result;
        }

        // Handle comma separator
        if expect_comma_or_close {
            if bytes[index] == b',' {
                index += 1;
                expect_comma_or_close = false;
                continue;
            } else if bytes[index] != b'}' {
                return None; // Expected comma or close brace
            }
            // Will handle closing brace on next iteration
            continue;
        }

        // At top level, expect a key-value pair
        if depth == 0 {
            if bytes[index] != b'"' {
                return None;
            }
            let (key, after_key) = read_quoted(input, index)?;
            index = after_key;

            // Skip whitespace before colon
            while index < bytes.len() && bytes[index].is_ascii_whitespace() {
                index += 1;
            }

            if index >= bytes.len() || bytes[index] != b':' {
                return None;
            }
            index += 1;

            // Skip whitespace after colon
            while index < bytes.len() && bytes[index].is_ascii_whitespace() {
                index += 1;
            }

            if index >= bytes.len() {
                return None;
            }

            // Read the value
            if bytes[index] == b'"' {
                let (value, after_value) = read_quoted(input, index)?;
                if key == name && result.is_none() {
                    result = Some(value);
                }
                index = after_value;
                expect_comma_or_close = true;
            } else if bytes[index] == b'{' || bytes[index] == b'[' {
                // Nested object or array - skip it by tracking depth
                let open = bytes[index];
                let close = if open == b'{' { b'}' } else { b']' };
                index += 1;
                depth = 1;

                while index < bytes.len() && depth > 0 {
                    match bytes[index] {
                        b'"' => {
                            let (_, after) = read_quoted(input, index)?;
                            index = after;
                        }
                        b'{' | b'[' => {
                            depth += 1;
                            index += 1;
                        }
                        b'}' | b']' => {
                            depth -= 1;
                            index += 1;
                        }
                        _ => index += 1,
                    }
                }

                if depth != 0 {
                    return None; // Truncated nested structure
                }
                expect_comma_or_close = true;
            } else {
                // Other value types (number, boolean, null) - skip them
                while index < bytes.len() {
                    let b = bytes[index];
                    if b.is_ascii_whitespace() || b == b',' || b == b'}' {
                        break;
                    }
                    index += 1;
                }
                expect_comma_or_close = true;
            }
        } else {
            // Inside nested structure - should not happen in refactored logic
            return None;
        }
    }

    None // Truncated - no closing brace
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
                    b'"' => value.push('"'),
                    b'\\' => value.push('\\'),
                    b'/' => value.push('/'),
                    b'n' => value.push('\n'),
                    b'r' => value.push('\r'),
                    b't' => value.push('\t'),
                    b'b' => value.push('\u{0008}'),
                    b'f' => value.push('\u{000C}'),
                    b'u' => {
                        let hex = input.get(index + 1..index + 5)?;
                        let code = u32::from_str_radix(hex, 16).ok()?;
                        value.push(char::from_u32(code)?);
                        index += 4;
                    }
                    _ => return None, // Invalid escape sequence
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

    #[test]
    fn rejects_nested_objects_with_matching_field() {
        // Nested "type" field should be ignored, only top-level matters
        let input = r#"{"nested": {"type": "nested_value"}, "type": "top_level"}"#;
        assert_eq!(read_string_field(input, "type").as_deref(), Some("top_level"));

        // Only nested "type", no top-level one
        let input = r#"{"nested": {"type": "nested_value"}}"#;
        assert_eq!(read_string_field(input, "type"), None);
    }

    #[test]
    fn rejects_nested_arrays_with_matching_field() {
        let input = r#"{"items": [{"type": "item1"}, {"type": "item2"}], "type": "list"}"#;
        assert_eq!(read_string_field(input, "type").as_deref(), Some("list"));

        let input = r#"{"items": [{"type": "item1"}]}"#;
        assert_eq!(read_string_field(input, "type"), None);
    }

    #[test]
    fn rejects_invalid_escape_sequences() {
        assert_eq!(read_string_field(r#"{"type": "invalid\xescape"}"#, "type"), None);
        assert_eq!(read_string_field(r#"{"type": "invalid\zescape"}"#, "type"), None);
        assert_eq!(read_string_field(r#"{"type": "invalid\aescape"}"#, "type"), None);
    }

    #[test]
    fn rejects_truncated_json() {
        // Missing closing brace
        assert_eq!(read_string_field(r#"{"type": "flip_board""#, "type"), None);
        // Missing closing quote
        assert_eq!(read_string_field(r#"{"type": "flip_board}"#, "type"), None);
        // Incomplete nested object
        assert_eq!(read_string_field(r#"{"nested": {"type": "x"}, "type": "y""#, "type"), None);
        // Incomplete nested array
        assert_eq!(read_string_field(r#"{"items": [1, 2, "type": "x"}"#, "type"), None);
    }

    #[test]
    fn rejects_trailing_content() {
        assert_eq!(read_string_field(r#"{"type": "flip_board"} garbage"#, "type"), None);
        assert_eq!(read_string_field(r#"{"type": "flip_board"}{"type": "second"}"#, "type"), None);
        assert_eq!(read_string_field(r#"{"type": "flip_board"}]"#, "type"), None);
    }

    #[test]
    fn accepts_leading_and_trailing_whitespace() {
        assert_eq!(
            read_string_field(r#"  {"type": "flip_board"}  "#, "type").as_deref(),
            Some("flip_board")
        );
        assert_eq!(
            read_string_field("\n\t{\"type\": \"flip_board\"}\n\t", "type").as_deref(),
            Some("flip_board")
        );
    }

    #[test]
    fn rejects_non_object_json() {
        assert_eq!(read_string_field(r#"["type", "flip_board"]"#, "type"), None);
        assert_eq!(read_string_field(r#""type""#, "type"), None);
        assert_eq!(read_string_field(r#"null"#, "type"), None);
        assert_eq!(read_string_field(r#"42"#, "type"), None);
    }
}
