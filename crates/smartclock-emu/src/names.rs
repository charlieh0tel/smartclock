//! A name table: the values of an enum each with the one name a script's
//! steps, the trace's records and the panel page all give it.

/// A type whose values are named by one fixed table.
pub(crate) trait Named: Copy + PartialEq + 'static {
    /// Every value, each with its name, in the order they are shown.
    const NAMES: &'static [(Self, &'static str)];

    /// The value `name` names, if any.
    fn from_name(name: &str) -> Option<Self> {
        Self::NAMES
            .iter()
            .find(|(_, named)| *named == name)
            .map(|(value, _)| *value)
    }

    /// Its name.
    fn name(self) -> &'static str {
        Self::NAMES
            .iter()
            .find(|(value, _)| *value == self)
            .map_or("", |(_, name)| name)
    }

    /// Every value, in the table's order.
    fn all() -> impl Iterator<Item = Self> {
        Self::NAMES.iter().map(|(value, _)| *value)
    }

    /// Every name, in the table's order.
    fn all_names() -> impl Iterator<Item = &'static str> {
        Self::NAMES.iter().map(|(_, name)| *name)
    }

    /// Every name, comma-separated, for a message.
    fn names() -> String {
        Self::all_names().collect::<Vec<_>>().join(", ")
    }
}

#[cfg(test)]
mod tests {
    use super::Named;

    /// A table of two.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    enum Pair {
        One,
        Two,
    }

    impl Named for Pair {
        const NAMES: &'static [(Self, &'static str)] = &[(Self::One, "one"), (Self::Two, "two")];
    }

    #[test]
    fn each_value_reads_back_from_its_name_and_the_rest_from_none() {
        for &(value, name) in Pair::NAMES {
            assert_eq!(value.name(), name);
            assert_eq!(Pair::from_name(name), Some(value));
        }
        assert_eq!(Pair::from_name("three"), None);
        assert_eq!(Pair::all().collect::<Vec<_>>(), [Pair::One, Pair::Two]);
        assert_eq!(Pair::names(), "one, two");
    }
}
