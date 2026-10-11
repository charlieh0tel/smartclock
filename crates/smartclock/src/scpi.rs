//! SCPI command spelling: the long form a table writes against the
//! short, long or in-between forms a receiver accepts.

/// The optional header a path may leave out.
const OPTIONAL_HEADER: &str = "SOURce";

/// Whether `written`, a command as a manual or the command table
/// writes it, names the tree's `path`.  Each keyword may be written
/// short, long, or anywhere between, in any case; a leading `:SOURce`
/// of the path may be left out; arguments and a trailing `?` are
/// ignored.
pub fn spells(written: &str, path: &str) -> bool {
    let written = written
        .split_whitespace()
        .next()
        .unwrap_or_default()
        .trim_end_matches('?');
    if written.starts_with('*') || path.starts_with('*') {
        return written.eq_ignore_ascii_case(path);
    }
    let tokens: Vec<&str> = written.trim_start_matches(':').split(':').collect();
    let keywords: Vec<&str> = path.trim_start_matches(':').split(':').collect();
    let matches = |keywords: &[&str]| {
        keywords.len() == tokens.len()
            && tokens.iter().zip(keywords).all(|(token, keyword)| {
                let token = token.to_ascii_uppercase();
                let short: String = keyword
                    .chars()
                    .filter(|letter| !letter.is_ascii_lowercase())
                    .collect();
                keyword.to_ascii_uppercase().starts_with(&token) && token.starts_with(&short)
            })
    };
    matches(&keywords) || (keywords.first() == Some(&OPTIONAL_HEADER) && matches(&keywords[1..]))
}

#[cfg(test)]
mod tests {
    use super::spells;

    #[test]
    fn a_keyword_is_spelled_short_long_or_between_and_source_is_optional() {
        assert!(spells(":SYST:ERR?", ":SYSTem:ERRor"));
        assert!(spells(":DIAG:LOG:COUN?", ":DIAGnostic:LOG:COUNt"));
        assert!(spells(
            ":ROSC:HOLD:DUR?",
            ":SOURce:ROSCillator:HOLDover:DURation"
        ));
        assert!(spells("*idn?", "*IDN"));
        assert!(!spells(":SY:ERR?", ":SYSTem:ERRor"));
        assert!(!spells(":SYSTem:ERRors?", ":SYSTem:ERRor"));
        assert!(!spells(":SYSTem", ":SYSTem:ERRor"));
    }
}
