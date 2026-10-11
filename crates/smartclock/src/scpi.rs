//! SCPI command spelling: the long form a table writes against the
//! short, long or in-between forms a receiver accepts.

use crate::command::Dialect;
use crate::command::Spec;

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
                // A numeric suffix, as in SERial1, follows the short form
                // as well as the long: SER1 and SERIAL1 both name it.
                let (token, given) = numbered(token);
                let (keyword, number) = numbered(keyword);
                let token = token.to_ascii_uppercase();
                let short: String = keyword
                    .chars()
                    .filter(|letter| !letter.is_ascii_lowercase())
                    .collect();
                given == number
                    && keyword.to_ascii_uppercase().starts_with(&token)
                    && token.starts_with(&short)
            })
    };
    matches(&keywords) || (keywords.first() == Some(&OPTIONAL_HEADER) && matches(&keywords[1..]))
}

/// A keyword and its numeric suffix, if it has one.
fn numbered(keyword: &str) -> (&str, &str) {
    let letters = keyword.trim_end_matches(|c: char| c.is_ascii_digit());
    (letters, &keyword[letters.len()..])
}

/// A command's header: what comes before its first space.
fn header(command: &str) -> &str {
    command.split_whitespace().next().unwrap_or_default()
}

/// A command's argument: what follows its first space.
fn argument(command: &str) -> &str {
    command
        .split_once(char::is_whitespace)
        .map_or("", |(_, argument)| argument.trim())
}

/// Whether `written`'s header names the same command as `entry`'s, as
/// the command table writes it: each keyword short, long or between,
/// and both queries or neither, since a query and its setter are
/// different entries.
pub fn same_header(written: &str, entry: &str) -> bool {
    let (written, entry) = (header(written), header(entry));
    written.ends_with('?') == entry.ends_with('?') && spells(written, entry.trim_end_matches('?'))
}

/// The table entry `command` is, spelled as a receiver would accept
/// it.  An entry that carries its own argument, such as
/// `:GPS:POSition:SURVey:STATe ONCE`, is tried first and needs that
/// argument; then an entry taking whatever argument follows.
pub fn lookup(dialect: Dialect, command: &str) -> Option<&'static Spec> {
    let specs = dialect.specs();
    let given = argument(command);
    specs
        .iter()
        .find(|s| {
            let carried = argument(s.scpi);
            !carried.is_empty()
                && carried.eq_ignore_ascii_case(given)
                && same_header(command, s.scpi)
        })
        .or_else(|| {
            specs
                .iter()
                .find(|s| argument(s.scpi).is_empty() && same_header(command, s.scpi))
        })
}

#[cfg(test)]
mod tests {
    use super::lookup;
    use super::same_header;
    use super::spells;
    use crate::command::CommandId;
    use crate::command::Dialect;

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
        assert!(spells(
            ":SYST:COMM:SER1:BAUD?",
            ":SYSTem:COMMunicate:SERial1:BAUD"
        ));
        assert!(spells(
            ":SYSTEM:COMMUNICATE:SERIAL1:BAUD?",
            ":SYSTem:COMMunicate:SERial1:BAUD"
        ));
        assert!(!spells(
            ":SYST:COMM:SER2:BAUD?",
            ":SYSTem:COMMunicate:SERial1:BAUD"
        ));
    }

    #[test]
    fn a_query_and_its_setter_are_different_headers() {
        assert!(same_header(":PTIME:TINT?", ":PTIMe:TINTerval?"));
        assert!(same_header(":ptim:tint?", ":PTIMe:TINTerval?"));
        assert!(!same_header(":PTIM:TINT", ":PTIMe:TINTerval?"));
        assert!(!same_header(":PTIM:TINT?", ":PTIMe:TINTerval"));
    }

    #[test]
    fn a_command_is_found_in_any_spelling_the_receiver_takes() {
        let found = |dialect, command| lookup(dialect, command).map(|s| s.id);
        assert_eq!(
            found(Dialect::Z3801, ":PTIME:TINT?"),
            Some(CommandId::Tinterval)
        );
        assert_eq!(
            found(Dialect::Hp58503, ":SYNC:TINT?"),
            Some(CommandId::Tinterval)
        );
        assert_eq!(
            found(Dialect::Hp58503, ":syst:err?"),
            Some(CommandId::Error)
        );
        assert_eq!(
            found(Dialect::Hp58503, ":GPS:SAT:TRAC:EMAN 10"),
            Some(CommandId::ElevationMaskSet)
        );
        assert_eq!(
            found(Dialect::Hp58503, ":GPS:POS:SURV:STAT once"),
            Some(CommandId::SurveyOnce)
        );
        assert_eq!(found(Dialect::Hp58503, ":SYNC:TIN?"), None);
        assert_eq!(found(Dialect::Hp58503, ":SYNC:TINTervals?"), None);
    }
}
