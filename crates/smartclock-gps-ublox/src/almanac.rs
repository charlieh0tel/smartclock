//! The almanac the GPS satellites broadcast, gathered from the pages of
//! subframes 4 and 5 ("Almanac" in the crate's documentation).

use smartclock_gps::record::Almanac;
use smartclock_gps::time::ALMANAC_WEEKS;
use smartclock_gps::time::GpsWeek;
use smartclock_gps::units::PRNS;
use smartclock_gps::units::Prn;
use smartclock_gps::units::PrnSet;
use smartclock_ubx::subframe::Body;
use smartclock_ubx::subframe::Subframe;

/// The data ID of a page of L1 C/A's own data (IS-GPS-200N, section
/// 20.3.3.5.1.1).
const DATA_ID: u8 = 1;
/// The satellites whose health subframe 5, page 25 gives: 1 to 24.
const EARLY_SATELLITES: usize = 24;
/// A health that says all is well, of an almanac page's eight bits or
/// a page 25's six (section 20.3.3.5.1.3).
const HEALTHY: u8 = 0;

/// What a satellite's almanac page says, of what a record holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Page {
    /// `toa`, raw as broadcast.
    time_of_almanac: u8,
    /// Its eight bits of health.
    health: u8,
}

/// Subframe 5, page 25's time and week of the almanac.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
struct Reference {
    /// `toa`, raw as broadcast.
    time_of_almanac: u8,
    /// `WNa`: the week modulo [`ALMANAC_WEEKS`].
    week: u8,
}

impl Reference {
    /// Whether it is of an upload before `other`'s: an earlier week, or
    /// the same week and an earlier time.  The weeks' eight bits wrap
    /// at [`ALMANAC_WEEKS`], so their difference is taken signed: two
    /// uploads are far less than 128 weeks apart.
    fn before(self, other: Self) -> bool {
        let weeks = self.week.wrapping_sub(other.week).cast_signed();
        weeks < 0 || (weeks == 0 && self.time_of_almanac < other.time_of_almanac)
    }
}

/// The pages of an almanac gathered so far, each the last broadcast,
/// all since subframe 5, page 25 last gave a later time or week.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct Gathered {
    /// Each satellite's page, satellite 1 first; `None` for one not yet
    /// received.
    pages: [Option<Page>; PRNS as usize],
    /// Subframe 5, page 25's time and week; `None` until it comes.
    reference: Option<Reference>,
    /// Each satellite's six bits of health from the pages 25,
    /// satellite 1 first: 1 to 24 from subframe 5's, 25 to 32 from
    /// subframe 4's; `None` until its page comes.
    health: [Option<u8>; PRNS as usize],
}

impl Gathered {
    /// No page yet.
    pub(crate) const EMPTY: Self = Self {
        pages: [None; PRNS as usize],
        reference: None,
        health: [None; PRNS as usize],
    };

    /// Keeps what `subframe` says of the almanac, if it is a page of it.
    pub(crate) fn take(&mut self, subframe: &Subframe) {
        if subframe.data_id != DATA_ID {
            return;
        }
        match subframe.body {
            Body::Almanac(almanac) => {
                if let Some(page) = Prn::new(almanac.satellite)
                    .and_then(|prn| self.pages.get_mut(usize::from(prn.get() - 1)))
                {
                    *page = Some(Page {
                        time_of_almanac: almanac.time_of_almanac.raw,
                        health: almanac.health,
                    });
                }
            }
            Body::AlmanacHealth(page) => {
                let reference = Reference {
                    time_of_almanac: page.time_of_almanac.raw,
                    week: page.week,
                };
                // An older upload's, from a satellite not yet changed
                // over to the one held.
                if self.reference.is_some_and(|held| reference.before(held)) {
                    return;
                }
                // A newer upload: a page's own time does not tell its
                // week, so the pages before this one are not its.
                if self.reference.is_some_and(|held| held != reference) {
                    *self = Self::EMPTY;
                }
                self.reference = Some(reference);
                for (kept, health) in self.health.iter_mut().zip(page.health) {
                    *kept = Some(health);
                }
            }
            Body::Configurations(page) => {
                for (kept, health) in self.health[EARLY_SATELLITES..].iter_mut().zip(page.health) {
                    *kept = Some(health);
                }
            }
            Body::Subframe1(_)
            | Body::Subframe2(_)
            | Body::Subframe3(_)
            | Body::IonosphereUtc(_)
            | Body::Page(_) => {}
        }
    }

    /// The almanac of subframe 5, page 25's time, its week resolved to
    /// the one nearest `near`.  It holds the satellites whose page has
    /// that time, and calls healthy those whose page gives a health of
    /// zero and whose page 25 does too or has not come.  `None` without
    /// a subframe 5, page 25, or with no page of its time.
    pub(crate) fn almanac(&self, near: GpsWeek) -> Option<Almanac> {
        let reference = self.reference?;
        let week = near.nearest(u16::from(reference.week), ALMANAC_WEEKS)?;
        let (held, healthy) = (1..=PRNS)
            .filter_map(Prn::new)
            .zip(self.pages.iter().zip(self.health))
            .filter_map(|(prn, (page, health))| {
                page.filter(|page| page.time_of_almanac == reference.time_of_almanac)
                    .map(|page| {
                        let healthy =
                            page.health == HEALTHY && health.is_none_or(|six| six == HEALTHY);
                        (prn, healthy)
                    })
            })
            .fold(
                (PrnSet::EMPTY, PrnSet::EMPTY),
                |(held, healthy), (prn, well)| {
                    (
                        held.with(prn),
                        if well { healthy.with(prn) } else { healthy },
                    )
                },
            );
        if held.is_empty() {
            return None;
        }
        let mut almanac = Almanac::new(week, reference.time_of_almanac, held);
        almanac.healthy = healthy;
        Some(almanac)
    }
}
