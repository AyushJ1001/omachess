use crate::game::PlayedMove;
use crate::rules::Rules;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PgnGame {
    pub tags: Vec<(String, String)>,
    pub start_fen: String,
    pub moves: Vec<PlayedMove>,
    pub result: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ImportFailure {
    pub entry: usize,
    pub title: String,
    pub reason: String,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ImportEntry {
    Imported(PgnGame),
    Failed(ImportFailure),
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ImportReport {
    Imported {
        entry: usize,
        title: String,
        id: String,
    },
    Failed(ImportFailure),
}

pub fn import(text: &str) -> Vec<ImportEntry> {
    split_entries(text)
        .into_iter()
        .enumerate()
        .map(|(index, entry)| parse_entry(index + 1, entry))
        .collect()
}

pub fn export(game: &PgnGame) -> String {
    let mut out = String::new();
    let roster = ["Event", "Site", "Date", "Round", "White", "Black", "Result"];
    let ordered = roster
        .iter()
        .filter_map(|name| game.tags.iter().find(|(key, _)| key == name))
        .chain(
            game.tags
                .iter()
                .filter(|(key, _)| !roster.contains(&key.as_str())),
        );
    for (name, value) in ordered {
        out.push('[');
        out.push_str(name);
        out.push_str(" \"");
        out.push_str(&value.replace('\\', "\\\\").replace('"', "\\\""));
        out.push_str("\"]\n");
    }
    out.push('\n');
    for (index, played) in game.moves.iter().enumerate() {
        if index > 0 {
            out.push(' ');
        }
        if played.side == "white" {
            out.push_str(&format!("{}. ", played.number));
        } else if index == 0 {
            out.push_str(&format!("{}... ", played.number));
        }
        out.push_str(&played.san);
    }
    if !game.moves.is_empty() {
        out.push(' ');
    }
    out.push_str(&game.result);
    out.push('\n');
    out
}

pub fn tag_value_from(tags: &[(String, String)], name: &str) -> String {
    tag(tags, name).unwrap_or_default().to_owned()
}

fn split_entries(text: &str) -> Vec<&str> {
    let mut starts = vec![0usize];
    let mut offset = 0usize;
    let mut saw_movetext = false;
    for line in text.split_inclusive('\n') {
        let trimmed = line.trim();
        if trimmed.starts_with('[') && saw_movetext {
            starts.push(offset);
            saw_movetext = false;
        } else if !trimmed.is_empty() && !trimmed.starts_with('[') {
            saw_movetext = true;
        }
        offset += line.len();
    }
    starts
        .iter()
        .enumerate()
        .map(|(index, start)| {
            let end = starts.get(index + 1).copied().unwrap_or(text.len());
            &text[*start..end]
        })
        .filter(|entry| !entry.trim().is_empty())
        .collect()
}

fn parse_entry(entry_number: usize, text: &str) -> ImportEntry {
    match parse_game(text) {
        Ok(game) => ImportEntry::Imported(game),
        Err(reason) => ImportEntry::Failed(ImportFailure {
            entry: entry_number,
            title: tag_value(text, "Event").unwrap_or_else(|| format!("Entry {entry_number}")),
            reason,
        }),
    }
}

fn parse_game(text: &str) -> Result<PgnGame, String> {
    let mut tags = Vec::new();
    let mut move_text = String::new();
    for line in text.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with('[') {
            let Some(space) = trimmed.find(' ') else {
                return Err("Malformed tag pair".into());
            };
            let value = parse_tag_value(trimmed).ok_or("Malformed tag pair")?;
            tags.push((trimmed[1..space].to_owned(), value));
        } else {
            move_text.push_str(trimmed);
            move_text.push('\n');
        }
    }
    let start_fen = tag(&tags, "FEN")
        .unwrap_or(omachess_store::GameRecordPayload::STANDARD_START)
        .to_owned();
    let mut rules = Rules::new("standard", Some(&start_fen))
        .ok_or_else(|| "Invalid FEN starting position".to_owned())?;
    let mut moves = Vec::new();
    let mut result = tag(&tags, "Result").unwrap_or("*").to_owned();
    for raw_token in movetext_tokens(&move_text)? {
        let token = strip_move_number(&raw_token);
        if token.is_empty() {
            continue;
        }
        if matches!(token.as_str(), "1-0" | "0-1" | "1/2-1/2" | "*") {
            result = token;
            break;
        }
        let normalized = token.trim_end_matches(['!', '?']).replace("0-0", "O-O");
        let mut matched = None;
        for candidate in rules.legal_moves() {
            let uci = format!(
                "{}{}{}",
                candidate.from,
                candidate.to,
                promotion_letter(candidate.promotion.as_deref())
            );
            if rules.san(&uci).as_deref() == Some(normalized.as_str()) {
                if matched.is_some() {
                    return Err(format!("Ambiguous move {token}"));
                }
                matched = Some(uci);
            }
        }
        let uci = matched.ok_or_else(|| format!("Illegal or unrecognised move {token}"))?;
        let san = rules.san(&uci).expect("matched SAN");
        let number = rules.move_number();
        let side = if rules.white_to_move() {
            "white"
        } else {
            "black"
        };
        assert!(rules.push(&uci));
        moves.push(PlayedMove {
            uci,
            san,
            number,
            side,
        });
    }
    if let Some(declared) = tag(&tags, "Result") {
        if declared != result {
            return Err(format!(
                "Result tag {declared} disagrees with movetext {result}"
            ));
        }
    }
    Ok(PgnGame {
        tags,
        start_fen,
        moves,
        result,
    })
}

fn movetext_tokens(text: &str) -> Result<Vec<String>, String> {
    let mut clean = String::new();
    let (mut comment, mut line_comment, mut variation_depth) = (false, false, 0usize);
    for ch in text.chars() {
        match ch {
            '\n' if line_comment => line_comment = false,
            ';' if !comment && variation_depth == 0 => line_comment = true,
            '{' if !comment => comment = true,
            '}' if comment => comment = false,
            '(' if !comment => variation_depth += 1,
            ')' if !comment && variation_depth > 0 => variation_depth -= 1,
            _ if !comment && !line_comment && variation_depth == 0 => clean.push(ch),
            _ => {}
        }
    }
    if comment {
        return Err("Unclosed comment".into());
    }
    Ok(clean
        .split_whitespace()
        .filter(|token| !token.starts_with('$') && !is_move_number(token))
        .map(str::to_owned)
        .collect())
}

fn is_move_number(token: &str) -> bool {
    token.trim_end_matches('.').parse::<u32>().is_ok()
}

fn strip_move_number(token: &str) -> String {
    let digit_count = token.chars().take_while(char::is_ascii_digit).count();
    if digit_count == 0 {
        return token.to_owned();
    }
    if token.as_bytes().get(digit_count) != Some(&b'.') {
        return token.to_owned();
    }
    token[digit_count..].trim_start_matches('.').to_owned()
}

pub fn encode_tags(tags: &[(String, String)]) -> String {
    let mut encoded = String::new();
    for (name, value) in tags {
        encoded.push_str(&format!("{}:{}{}:{}", name.len(), name, value.len(), value));
    }
    encoded
}

pub fn decode_tags(mut encoded: &str) -> Vec<(String, String)> {
    let mut tags = Vec::new();
    while !encoded.is_empty() {
        let Some((name_len, after_name_len)) = take_length(encoded) else {
            return Vec::new();
        };
        let Some(name) = after_name_len.get(..name_len) else {
            return Vec::new();
        };
        encoded = &after_name_len[name_len..];
        let Some((value_len, after_value_len)) = take_length(encoded) else {
            return Vec::new();
        };
        let Some(value) = after_value_len.get(..value_len) else {
            return Vec::new();
        };
        tags.push((name.to_owned(), value.to_owned()));
        encoded = &after_value_len[value_len..];
    }
    tags
}

fn take_length(encoded: &str) -> Option<(usize, &str)> {
    let colon = encoded.find(':')?;
    Some((encoded[..colon].parse().ok()?, &encoded[colon + 1..]))
}

fn parse_tag_value(line: &str) -> Option<String> {
    let first = line.find('"')? + 1;
    let last = line.rfind('"')?;
    (last >= first).then(|| {
        line[first..last]
            .replace("\\\"", "\"")
            .replace("\\\\", "\\")
    })
}

fn tag<'a>(tags: &'a [(String, String)], name: &str) -> Option<&'a str> {
    tags.iter()
        .find(|(key, _)| key == name)
        .map(|(_, value)| value.as_str())
}

fn tag_value(text: &str, name: &str) -> Option<String> {
    text.lines()
        .find(|line| line.trim_start().starts_with(&format!("[{name} ")))
        .and_then(parse_tag_value)
}

fn promotion_letter(role: Option<&str>) -> &'static str {
    match role {
        Some("queen") => "q",
        Some("rook") => "r",
        Some("bishop") => "b",
        Some("knight") => "n",
        _ => "",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mixed_multi_game_import_keeps_valid_entries_and_reports_the_bad_one() {
        let pgn = r#"[Event "Valid one"]
[White "Ada"]
[Black "Grace"]
[Result "*"]

1. e4 e5 2. Nf3 *

[Event "Broken"]
[Result "*"]

1. e4 e5 2. NotAMove *

[Event "Valid two"]
[Result "1-0"]

1. f3 e5 2. g4 Qh4# 0-1
"#;
        let results = import(pgn);
        assert_eq!(results.len(), 3);
        assert!(matches!(&results[0], ImportEntry::Imported(game) if game.moves.len() == 3));
        assert!(matches!(&results[1], ImportEntry::Failed(failure)
            if failure.entry == 2 && failure.title == "Broken"
                && failure.reason.contains("NotAMove")));
        assert!(matches!(&results[2], ImportEntry::Failed(failure)
            if failure.reason.contains("disagrees")));
    }

    #[test]
    fn exported_standard_game_is_importable_with_metadata_intact() {
        let source = "[Event \"Round trip\"]\n[White \"Ada\"]\n[Black \"Grace\"]\n[Result \"*\"]\n\n1. e4 e5 2. Nf3 *\n";
        let ImportEntry::Imported(game) = &import(source)[0] else {
            panic!("valid fixture")
        };
        let exported = export(game);
        let ImportEntry::Imported(round_trip) = &import(&exported)[0] else {
            panic!("export imports")
        };
        assert_eq!(round_trip.tags, game.tags);
        assert_eq!(round_trip.moves, game.moves);
    }

    #[test]
    fn entry_boundaries_and_compact_move_numbers_follow_common_pgn_forms() {
        let source = "[Site \"First\"]\n[Result \"*\"]\n\n1.e4 e5 *\n\n[Site \"Second\"]\n[Result \"*\"]\n\n1.d4 d5 *\n";
        let results = import(source);
        assert_eq!(results.len(), 2);
        assert!(matches!(&results[0], ImportEntry::Imported(game) if game.moves.len() == 2));
        assert!(matches!(&results[1], ImportEntry::Imported(game) if game.moves.len() == 2));
    }

    #[test]
    fn metadata_encoding_preserves_delimiters() {
        let tags = vec![("Event".into(), "Open; finals = day 2".into())];
        assert_eq!(decode_tags(&encode_tags(&tags)), tags);
    }

    #[test]
    fn semicolon_comments_are_ignored_to_the_end_of_the_line() {
        let source = "[Result \"*\"]\n\n1. e4 ; king pawn\n e5 *\n";
        assert!(matches!(&import(source)[0], ImportEntry::Imported(game) if game.moves.len() == 2));
    }
}
