// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdint>
#include <gtest/gtest.h>

extern "C" {
#include "../../src/codegen/codegen_smc_policy.h"
}

namespace {

struct Block {
    uint8_t  strikes  = 0;
    uint64_t cooldown = 0;
};

void
strike(Block &b, uint8_t &page, uint64_t now)
{
    smc_thrash_rewritten(&b.strikes, &page, 0, now, &b.cooldown);
}

void
pay(Block &b, uint8_t &page, uint64_t now)
{
    smc_thrash_rewritten(&b.strikes, &page, 1, now, &b.cooldown);
}

} // namespace

TEST(CodegenSmcPolicy, NoCooldownBelowThreshold)
{
    Block   b;
    uint8_t page = 0;
    for (int i = 1; i < SMC_THRASH_STRIKE_THRESHOLD; i++) {
        strike(b, page, 1000);
        EXPECT_EQ(b.strikes, i);
        EXPECT_EQ(page, i);
        EXPECT_EQ(b.cooldown, 0u);
    }
    strike(b, page, 1000);
    EXPECT_EQ(b.cooldown, 1000u + SMC_THRASH_BASE_COOLDOWN);
}

TEST(CodegenSmcPolicy, CooldownDoublesPerStrikeAndSaturates)
{
    Block   b;
    uint8_t page = 0;
    for (int i = 1; i <= 40; i++) {
        strike(b, page, 0);
        int s = i < SMC_THRASH_MAX_STRIKES ? i : SMC_THRASH_MAX_STRIKES;
        EXPECT_EQ(b.strikes, s);
        EXPECT_EQ(page, s);
        if (s >= SMC_THRASH_STRIKE_THRESHOLD) {
            EXPECT_EQ(b.cooldown, (uint64_t) SMC_THRASH_BASE_COOLDOWN << (s - SMC_THRASH_STRIKE_THRESHOLD));
        }
    }
}

TEST(CodegenSmcPolicy, PaybackClearsBlockButOnlyDecaysPage)
{
    Block   b;
    uint8_t page = 0;
    for (int i = 0; i < 6; i++)
        strike(b, page, 0);
    b.cooldown = 1234;
    pay(b, page, 0);
    EXPECT_EQ(b.strikes, 0);
    EXPECT_EQ(page, 5);
    EXPECT_EQ(b.cooldown, 1234u); // payback leaves the cooldown alone

    for (int i = 0; i < 10; i++)
        pay(b, page, 0);
    EXPECT_EQ(page, 0); // never wraps below zero
}

TEST(CodegenSmcPolicy, StruckBlockTakesPageStrikesUpToCap)
{
    // A fresh block struck once on a busy page backs off as the page says,
    // but no further than SMC_THRASH_BLOCK_MAX_FROM_PAGE.
    for (int p = 0; p <= SMC_THRASH_MAX_STRIKES; p++) {
        Block   b;
        uint8_t page = (uint8_t) p;
        strike(b, page, 0);
        int page_after = p < SMC_THRASH_MAX_STRIKES ? p + 1 : p;
        int from_page  = page_after < SMC_THRASH_BLOCK_MAX_FROM_PAGE ? page_after : SMC_THRASH_BLOCK_MAX_FROM_PAGE;
        int s          = from_page > 1 ? from_page : 1;
        EXPECT_EQ(page, page_after);
        if (s >= SMC_THRASH_STRIKE_THRESHOLD)
            EXPECT_EQ(b.cooldown, (uint64_t) SMC_THRASH_BASE_COOLDOWN << (s - SMC_THRASH_STRIKE_THRESHOLD)) << "page " << p;
        else
            EXPECT_EQ(b.cooldown, 0u) << "page " << p;
    }
}

TEST(CodegenSmcPolicy, BlockOwnStrikesBeatPageCap)
{
    Block   b;
    uint8_t page = 0;
    for (int i = 0; i < SMC_THRASH_MAX_STRIKES; i++)
        strike(b, page, 0);
    page = 0;
    strike(b, page, 0);
    EXPECT_EQ(b.cooldown, (uint64_t) SMC_THRASH_BASE_COOLDOWN << (SMC_THRASH_MAX_STRIKES - SMC_THRASH_STRIKE_THRESHOLD));
}

TEST(CodegenSmcPolicy, NewBlockCooldownFollowsPage)
{
    for (int p = 0; p <= SMC_THRASH_MAX_STRIKES; p++) {
        uint64_t c = smc_thrash_new_block_cooldown((uint8_t) p, 500);
        if (p < SMC_THRASH_STRIKE_THRESHOLD)
            EXPECT_EQ(c, 0u);
        else
            EXPECT_EQ(c, 500u + ((uint64_t) SMC_THRASH_BASE_COOLDOWN << (p - SMC_THRASH_STRIKE_THRESHOLD)));
    }
}

TEST(CodegenSmcPolicy, CooldownStaysBounded)
{
    // Worst case: about 2M tsc, a few ms of interpretation even at low clocks.
    EXPECT_LE(smc_thrash_new_block_cooldown(255 > SMC_THRASH_MAX_STRIKES ? SMC_THRASH_MAX_STRIKES : 255, 0),
              (uint64_t) SMC_THRASH_BASE_COOLDOWN << 9);
}

TEST(CodegenSmcPolicy, CodeAtFreshAddressesIsCaughtByThePage)
{
    // Generated code that never revisits a block address: every block is
    // new and struck once. Only the page count lets later blocks start in
    // cooldown.
    uint8_t page = 0;
    int     cooled_new = 0;
    for (int i = 0; i < 20; i++) {
        Block b;
        b.cooldown = smc_thrash_new_block_cooldown(page, 0);
        if (b.cooldown)
            cooled_new++;
        strike(b, page, 0);
    }
    EXPECT_EQ(page, SMC_THRASH_MAX_STRIKES);
    EXPECT_EQ(cooled_new, 20 - SMC_THRASH_STRIKE_THRESHOLD);
}

TEST(CodegenSmcPolicy, NormalCodeRecoversThePage)
{
    // After a thrash burst, blocks that pay back bring the page under the
    // threshold, so new blocks compile immediately again.
    uint8_t page = SMC_THRASH_MAX_STRIKES;
    Block   b;
    int     paid = 0;
    while (smc_thrash_new_block_cooldown(page, 0)) {
        pay(b, page, 0);
        paid++;
    }
    EXPECT_EQ(paid, SMC_THRASH_MAX_STRIKES - SMC_THRASH_STRIKE_THRESHOLD + 1);
}

TEST(CodegenSmcPolicy, FamilyNeedsRepeatedShortRewrites)
{
    uint8_t score = 0;
    for (int i = 0; i < SMC_FAMILY_SCORE; i++) {
        EXPECT_FALSE(smc_family_ready(score));
        smc_family_rewritten(&score, 0);
    }
    EXPECT_TRUE(smc_family_ready(score));
    // Capped: one long rewrite is enough to drop back below.
    for (int i = 0; i < 100; i++)
        smc_family_rewritten(&score, 0);
    EXPECT_EQ(score, SMC_FAMILY_SCORE);
    smc_family_rewritten(&score, 1);
    EXPECT_FALSE(smc_family_ready(score));
}

TEST(CodegenSmcPolicy, FamilyIgnoresOccasionalShortRewrites)
{
    // Short and long rewrites taking turns never reach the score.
    uint8_t score = 0;
    for (int i = 0; i < 100; i++) {
        smc_family_rewritten(&score, 0);
        EXPECT_FALSE(smc_family_ready(score));
        smc_family_rewritten(&score, 1);
    }
    EXPECT_EQ(score, 0);
    smc_family_rewritten(&score, 1);
    EXPECT_EQ(score, 0);
}

namespace {
// Long runs a family block stays on through before the next try, after
// `tries` tries in a row that each brought it back.
int
family_holds_after(uint8_t &hold)
{
    int kept = 0;
    while (smc_family_hold(&hold))
        kept++;
    EXPECT_TRUE(hold & SMC_FAMILY_HOLD_TRIED);
    return kept;
}
}

TEST(CodegenSmcPolicy, FamilyHoldFirstLongRunTries)
{
    uint8_t hold = 0;
    EXPECT_FALSE(smc_family_hold(&hold));
    EXPECT_TRUE(hold & SMC_FAMILY_HOLD_TRIED);
}

TEST(CodegenSmcPolicy, FamilyHoldDoublesWhileTriesComeBack)
{
    uint8_t hold = 0;
    EXPECT_EQ(family_holds_after(hold), 0);
    for (int level = 1; level <= 6; level++) {
        smc_family_hold_rewritten(&hold, 0); // came back after a short run
        int want = (1 << (level < SMC_FAMILY_HOLD_MAX_LEVEL ? level : SMC_FAMILY_HOLD_MAX_LEVEL)) - 1;
        EXPECT_EQ(family_holds_after(hold), want);
    }
}

TEST(CodegenSmcPolicy, FamilyHoldResetsOnLongRunOff)
{
    uint8_t hold = 0;
    for (int i = 0; i < 4; i++) {
        family_holds_after(hold);
        smc_family_hold_rewritten(&hold, 0);
    }
    // Off and ran long: its opcodes no longer change every few runs.
    smc_family_hold_rewritten(&hold, 1);
    EXPECT_EQ(hold, 0);
    EXPECT_FALSE(smc_family_hold(&hold));
}

TEST(CodegenSmcPolicy, FamilyHoldShortRunWithoutTryLeavesLevel)
{
    // A short run of a block that was not off for a try changes nothing.
    uint8_t hold = 0;
    smc_family_hold_rewritten(&hold, 0);
    EXPECT_EQ(hold, 0);
    family_holds_after(hold);
    smc_family_hold_rewritten(&hold, 0);
    uint8_t held = hold;
    EXPECT_TRUE(smc_family_hold(&hold));
    smc_family_hold_rewritten(&hold, 0);
    EXPECT_EQ(hold, (uint8_t) (held - 1));
}
