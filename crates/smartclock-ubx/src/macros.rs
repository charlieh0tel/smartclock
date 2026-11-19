//! The two shapes an input message's field takes beside a number: a
//! bit field, and a value of a list the document gives.

/// A bit field (`X1`, `X2`, `X4`) as its bits, so that whatever a
/// caller sets is what is sent, a reserved bit too.  Its named bits
/// and the values of its multi-bit fields, already shifted into place,
/// are constants of the type, combined with [`union`](Self::union).
macro_rules! bit_field_type {
    ($(#[$meta:meta])* $name:ident($bits:ty)) => {
        $(#[$meta])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
        pub struct $name(pub $bits);

        impl $name {
            /// No bit set.
            pub const NONE: Self = Self(0);

            /// The bits of both.
            #[must_use]
            pub const fn union(self, other: Self) -> Self {
                Self(self.0 | other.0)
            }

            /// Whether every bit of `other` is set.
            #[must_use]
            pub const fn contains(self, other: Self) -> bool {
                self.0 & other.0 == other.0
            }
        }
    };
}

/// A field whose values the document lists: a variant for each, and
/// [`Other`](Self::Other) for one it does not give, so a value read is
/// written back unchanged.
macro_rules! value_type {
    (
        $(#[$meta:meta])* $name:ident($repr:ty) {
            $($(#[$variant_meta:meta])* $variant:ident = $value:literal,)*
        }
    ) => {
        $(#[$meta])*
        #[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
        pub enum $name {
            $($(#[$variant_meta])* $variant,)*
            /// A value the document does not give.
            Other($repr),
        }

        impl $name {
            /// The variant of a field's value.
            #[must_use]
            pub const fn new(value: $repr) -> Self {
                match value {
                    $($value => Self::$variant,)*
                    other => Self::Other(other),
                }
            }

            /// The field's value.
            #[must_use]
            pub const fn value(self) -> $repr {
                match self {
                    $(Self::$variant => $value,)*
                    Self::Other(other) => other,
                }
            }
        }
    };
}
