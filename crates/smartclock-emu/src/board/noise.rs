//! The board's modeled noise: a fixed sequence from a seed, `--seed`,
//! so that a run with the same seed is the same each time.

/// A generator of noise: splitmix64 for uniforms, Box-Muller for
/// normals.
#[derive(Debug)]
pub(super) struct Noise {
    /// splitmix64's state.
    state: u64,
}

/// splitmix64's increment, its state's step a uniform.
const GAMMA: u64 = 0x9e37_79b9_7f4a_7c15;
/// The uniforms a normal takes (Box-Muller's two).
const UNIFORMS_PER_NORMAL: u64 = 2;

impl Noise {
    /// The generator from `seed`.
    pub(super) fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    /// The generator moved on past `normals` normals, as though drawn:
    /// splitmix64's state is a count.
    pub(super) fn skip(&mut self, normals: u64) {
        self.state = self
            .state
            .wrapping_add(GAMMA.wrapping_mul(normals.wrapping_mul(UNIFORMS_PER_NORMAL)));
    }

    /// A normal value, mean 0 and spread 1.
    pub(super) fn normal(&mut self) -> f64 {
        // Box-Muller from two uniforms in (0, 1]
        let first = self.uniform();
        let second = self.uniform();
        (-2.0 * first.ln()).sqrt() * (std::f64::consts::TAU * second).cos()
    }

    /// A uniform value in (0, 1], from splitmix64.
    fn uniform(&mut self) -> f64 {
        const MANTISSA_BITS: u32 = 53;
        self.state = self.state.wrapping_add(GAMMA);
        let mixed = (self.state ^ self.state >> 30).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        let mixed = (mixed ^ mixed >> 27).wrapping_mul(0x94d0_49bb_1331_11eb);
        let mixed = mixed ^ mixed >> 31;
        ((mixed >> (u64::BITS - MANTISSA_BITS)) + 1) as f64 / (1u64 << MANTISSA_BITS) as f64
    }
}

#[cfg(test)]
mod tests {
    use super::Noise;

    #[test]
    fn a_skip_is_the_draws_it_passes() {
        let mut drawn = Noise::new(7);
        for _ in 0..5 {
            drawn.normal();
        }
        let mut skipped = Noise::new(7);
        skipped.skip(5);
        assert_eq!(skipped.normal().to_bits(), drawn.normal().to_bits());
    }
}
