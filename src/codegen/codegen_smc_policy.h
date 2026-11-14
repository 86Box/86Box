/*SMC-thrash interpret fallback policy (see codeblock_t::smc_strikes and
  page_t::smc_strikes). Kept free of emulator state so it can be tested
  on its own.*/
#ifndef _CODEGEN_SMC_POLICY_H_
#define _CODEGEN_SMC_POLICY_H_

#include <stdint.h>

/*Strike threshold, base cooldown length (in tsc counts), and cap on the
  exponential backoff applied per consecutive strike.*/
#define SMC_THRASH_STRIKE_THRESHOLD 3
#define SMC_THRASH_BASE_COOLDOWN    4096
#define SMC_THRASH_MAX_STRIKES      12
/*Most page strikes a struck block takes over (4096 << 5 tsc).*/
#define SMC_THRASH_BLOCK_MAX_FROM_PAGE 8
/*Most bytes of a block's code that may differ from its newest kept version
  for it to count as patched in place rather than replaced by other code
  (codegen_block_dirty_patched()): an imm32 or disp32, or a few opcodes.*/
#define SMC_PATCH_MAX_BYTES 4

static inline uint64_t
smc_thrash_cooldown(uint8_t strikes)
{
    return (uint64_t) SMC_THRASH_BASE_COOLDOWN << (strikes - SMC_THRASH_STRIKE_THRESHOLD);
}

/*A compiled block is being rewritten. paid_back says whether it ran often
  enough from compiled code to make up for compiling it. Updates both
  strike counts and, when struck often enough, the block's cooldown.*/
static inline void
smc_thrash_rewritten(uint8_t *block_strikes, uint8_t *page_strikes, int paid_back,
                     uint64_t now, uint64_t *cooldown_until)
{
    if (!paid_back) {
        if (*block_strikes < SMC_THRASH_MAX_STRIKES)
            (*block_strikes)++;
        if (*page_strikes < SMC_THRASH_MAX_STRIKES)
            (*page_strikes)++;
        uint8_t from_page = *page_strikes < SMC_THRASH_BLOCK_MAX_FROM_PAGE ?
                            *page_strikes : SMC_THRASH_BLOCK_MAX_FROM_PAGE;
        uint8_t strikes   = *block_strikes > from_page ? *block_strikes : from_page;
        if (strikes >= SMC_THRASH_STRIKE_THRESHOLD)
            *cooldown_until = now + smc_thrash_cooldown(strikes);
    } else {
        *block_strikes = 0;
        if (*page_strikes)
            (*page_strikes)--;
    }
}

/*CODEBLOCK_NO_OPCODES code does every ALU op of the family behind a run-time
  opcode check, a few times slower per run than code compiled for the bytes
  it has. It only pays when the block's opcodes change more often than about
  once per SMC_FAMILY_MIN_RUNS runs. A block goes on to it once its score
  reaches SMC_FAMILY_SCORE: each rewrite after fewer runs adds one, each one
  after more takes one off, so a few lucky short gaps in code rewritten
  seldom do not tip it over.*/
#define SMC_FAMILY_MIN_RUNS 64
#define SMC_FAMILY_SCORE    4

/*A compiled block is being rewritten after it ran (at least SMC_FAMILY_MIN_RUNS
  times if ran_long).*/
static inline void
smc_family_rewritten(uint8_t *score, int ran_long)
{
    if (!ran_long) {
        if (*score < SMC_FAMILY_SCORE)
            (*score)++;
    } else if (*score)
        (*score)--;
}

static inline int
smc_family_ready(uint8_t score)
{
    return score >= SMC_FAMILY_SCORE;
}

/*A CODEBLOCK_NO_OPCODES block is rewritten only when one of its other bytes
  changes, so it runs long between rewrites however often its opcodes change.
  If they did change, only taking it off (compiling it for its bytes) shows
  whether they still change every few runs: then it is back on after its next
  short run, at the cost of two compiles. Each time that happens in a row,
  the block stays on through twice as many such long runs before the next
  try (up to 2^SMC_FAMILY_HOLD_MAX_LEVEL - 1), so code whose opcodes keep
  changing is not taken off at every rewrite, while code whose opcodes have
  settled since (other code put there, say) still comes off after a few.
  hold, per block, is the number of long runs still to stay on through (low
  3 bits), the number of tries in a row that brought it back (next 2) and
  SMC_FAMILY_HOLD_TRIED while off after a try.*/
#define SMC_FAMILY_HOLD_MAX_LEVEL   3
#define SMC_FAMILY_HOLD_LEVEL_SHIFT 3
#define SMC_FAMILY_HOLD_TRIED       0x20

/*A CODEBLOCK_NO_OPCODES block ran long and its opcodes changed meanwhile:
  whether to keep it on and its score as it is (1), else it comes off as the
  long run takes one off the score*/
static inline int
smc_family_hold(uint8_t *hold)
{
    if (*hold & ((1 << SMC_FAMILY_HOLD_LEVEL_SHIFT) - 1)) {
        (*hold)--;
        return 1;
    }
    *hold |= SMC_FAMILY_HOLD_TRIED;
    return 0;
}

/*A block is rewritten after a short run, or after a long one (ran_long) while
  compiled for its bytes (not CODEBLOCK_NO_OPCODES)*/
static inline void
smc_family_hold_rewritten(uint8_t *hold, int ran_long)
{
    if (ran_long)
        *hold = 0;
    else if (*hold & SMC_FAMILY_HOLD_TRIED) {
        unsigned level = (*hold >> SMC_FAMILY_HOLD_LEVEL_SHIFT) & 3;

        if (level < SMC_FAMILY_HOLD_MAX_LEVEL)
            level++;
        *hold = (uint8_t) ((level << SMC_FAMILY_HOLD_LEVEL_SHIFT) | ((1u << level) - 1));
    }
}

/*Cooldown for a block newly created on a page with the given strikes.*/
static inline uint64_t
smc_thrash_new_block_cooldown(uint8_t page_strikes, uint64_t now)
{
    if (page_strikes >= SMC_THRASH_STRIKE_THRESHOLD)
        return now + smc_thrash_cooldown(page_strikes);
    return 0;
}

#endif
