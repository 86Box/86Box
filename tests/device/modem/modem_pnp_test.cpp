/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Serial Plug and Play modem tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <cstdint>
#include <cstdio>
#include <string>

#include <gtest/gtest.h>

extern "C" {
#include <86box/modem/modem_pnp.h>
}

static void
wait_low(modem_pnp_t &pnp, int milliseconds)
{
    modem_pnp_control(&pnp, false, false, true);
    for (int i = 0; i < milliseconds; i++)
        modem_pnp_tick(&pnp, true);
}

static std::string
read_response(modem_pnp_t &pnp)
{
    std::string bytes;
    while (modem_pnp_pending(&pnp))
        bytes.push_back(static_cast<char>(modem_pnp_next(&pnp)));
    return bytes;
}

TEST(ModemPnp, ProfilesProduceChecksummedIdentification)
{
    const struct { int profile; const char *id; } profiles[] = {
        { MODEM_PNP_PNPC100, "PNPC100" },
        { MODEM_PNP_PNPC101, "PNPC101" },
        { MODEM_PNP_PNPC102, "PNPC102" },
        { MODEM_PNP_GENERIC, "PNPC103" },
        { MODEM_PNP_PNPC104, "PNPC104" },
        { MODEM_PNP_PNPC105, "PNPC105" },
        { MODEM_PNP_PNPC106, "PNPC106" },
        { MODEM_PNP_ENHANCED, "PNPC107" }
    };
    for (const auto &profile : profiles) {
        modem_pnp_t pnp;
        modem_pnp_init(&pnp, profile.profile);
        modem_pnp_format(&pnp, 1200, 7, 0, 1);
        modem_pnp_control(&pnp, true, false, true);
        wait_low(pnp, 200);
        ASSERT_TRUE(modem_pnp_control(&pnp, true, false, true));
        const auto response = read_response(pnp);
        const std::string prefix = std::string("(\x01$") + profile.id + "\\\\MODEM\\" + profile.id
                                   + "\\86Box Emulated Modem";
        ASSERT_EQ(response.size(), prefix.size() + 3);
        EXPECT_EQ(response.substr(0, prefix.size()), prefix);
        EXPECT_EQ(response.back(), ')');
        unsigned sum = ')';
        for (unsigned char c : prefix)
            sum += c;
        char expected[3];
        snprintf(expected, sizeof(expected), "%02X", sum & 0xff);
        EXPECT_EQ(response.substr(prefix.size(), 2), expected);
        EXPECT_EQ(modem_pnp_next(&pnp), -1);
    }
}

TEST(ModemPnp, UnknownProfilesDoNotRespond)
{
    for (int profile : { -1, 9, 100 }) {
        modem_pnp_t pnp;
        modem_pnp_init(&pnp, profile);
        modem_pnp_format(&pnp, 1200, 7, 0, 1);
        wait_low(pnp, 200);
        EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true)) << profile;
        EXPECT_FALSE(modem_pnp_pending(&pnp)) << profile;
    }
}

TEST(ModemPnp, RejectsPartialTimedOutAndNonProbeSequences)
{
    modem_pnp_t pnp;
    modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
    modem_pnp_format(&pnp, 1200, 7, 0, 1);
    wait_low(pnp, 99);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true));
    wait_low(pnp, 1001);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true));
    wait_low(pnp, 200);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, true, true));
    EXPECT_FALSE(modem_pnp_pending(&pnp));
}

TEST(ModemPnp, AcceptsBothTimingBoundaries)
{
    for (int milliseconds : { 100, 1000 }) {
        modem_pnp_t pnp;
        modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
        modem_pnp_format(&pnp, 1200, 7, 0, 1);
        wait_low(pnp, milliseconds);
        EXPECT_TRUE(modem_pnp_control(&pnp, true, false, true));
    }
}

TEST(ModemPnp, AcceptsNominalBaudWithinClockTolerance)
{
    for (int baud : { 1176, 1200, 1201, 1224 }) {
        modem_pnp_t pnp;
        modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
        modem_pnp_format(&pnp, baud, 7, 0, 1);
        wait_low(pnp, 200);
        EXPECT_TRUE(modem_pnp_control(&pnp, true, false, true)) << baud;
    }

    for (int baud : { 0, 1175, 1225, 9600 }) {
        modem_pnp_t pnp;
        modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
        modem_pnp_format(&pnp, baud, 7, 0, 1);
        wait_low(pnp, 200);
        EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true)) << baud;
        EXPECT_FALSE(modem_pnp_pending(&pnp)) << baud;
    }
}

TEST(ModemPnp, DisabledWrongFormatAndBusyDoNotRespond)
{
    modem_pnp_t pnp;
    modem_pnp_init(&pnp, MODEM_PNP_DISABLED);
    modem_pnp_format(&pnp, 1200, 7, 0, 1);
    wait_low(pnp, 200);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true));

    modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
    modem_pnp_format(&pnp, 1200, 8, 0, 1);
    wait_low(pnp, 200);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, false, true));
    modem_pnp_format(&pnp, 1200, 7, 0, 1);
    wait_low(pnp, 200);
    EXPECT_FALSE(modem_pnp_control(&pnp, true, false, false));
    EXPECT_FALSE(modem_pnp_pending(&pnp));
}

TEST(ModemPnp, CancelsOnBusyOrFormatChangeAndAllowsReenumeration)
{
    modem_pnp_t pnp;
    modem_pnp_init(&pnp, MODEM_PNP_GENERIC);
    modem_pnp_format(&pnp, 1200, 7, 0, 1);
    wait_low(pnp, 200);
    ASSERT_TRUE(modem_pnp_control(&pnp, true, false, true));
    modem_pnp_tick(&pnp, false);
    EXPECT_FALSE(modem_pnp_pending(&pnp));
    wait_low(pnp, 200);
    ASSERT_TRUE(modem_pnp_control(&pnp, true, false, true));
    modem_pnp_format(&pnp, 9600, 8, 0, 1);
    EXPECT_FALSE(modem_pnp_pending(&pnp));
    modem_pnp_format(&pnp, 1200, 7, 0, 1);
    wait_low(pnp, 200);
    EXPECT_TRUE(modem_pnp_control(&pnp, true, false, true));
}
