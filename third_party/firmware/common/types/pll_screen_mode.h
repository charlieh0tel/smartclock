/* pll_screen_mode and the types beside it, as the images' smartclock.h declare
 * them (each includes this header where they stand):
 * docs/firmware/rewrite.md, "Shared C".  Where a comment cites code, it
 * is the Z3805A 3543B's, which the 58503A's copy of the comment came
 * from. */

/* The status screen's mode, pll_mode's result from the stage and
 * pll.manual, which the screen and the :ROSCillator:HOLDover
 * queries read (docs/screen-format-strings.md, "Mode suffixes"). */
typedef enum __attribute__((packed)) pll_screen_mode {
  PLL_MODE_NONE = 0,
  PLL_MODE_HOLDOVER_MANUAL = 1,
  PLL_MODE_HOLDOVER = 2,
  PLL_MODE_RECOVERY = 3,
  PLL_MODE_POWERUP = 4,
  PLL_MODE_LOCKED = 5,
} pll_screen_mode;
