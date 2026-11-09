/*
 * 86Box    A hypervisor and IBM PC system emulator.
 *
 *          Serial modem flow-control tests.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
#include <gtest/gtest.h>

extern "C" {
#include <86box/modem/modem_flow.h>
}

TEST(ModemFlow, HardwareWatermarksAndRts)
{
    modem_flow_t flow = {};
    ASSERT_TRUE(modem_flow_set_mode(&flow, MODEM_FLOW_HARDWARE));
    EXPECT_FALSE(modem_flow_can_send(&flow, false));
    EXPECT_TRUE(modem_flow_can_send(&flow, true));
    modem_flow_update(&flow, true, MODEM_FLOW_HIGH_WATER - 1);
    EXPECT_TRUE(modem_flow_cts(&flow));
    modem_flow_update(&flow, true, MODEM_FLOW_HIGH_WATER);
    EXPECT_FALSE(modem_flow_cts(&flow));
    modem_flow_update(&flow, true, MODEM_FLOW_LOW_WATER + 1);
    EXPECT_FALSE(modem_flow_cts(&flow));
    modem_flow_update(&flow, true, MODEM_FLOW_LOW_WATER);
    EXPECT_TRUE(modem_flow_cts(&flow));
    EXPECT_EQ(modem_flow_control_pending(&flow), -1);
}

TEST(ModemFlow, SoftwarePauseAndPriorityTransitions)
{
    modem_flow_t flow = {};
    ASSERT_TRUE(modem_flow_set_mode(&flow, MODEM_FLOW_SOFTWARE));
    EXPECT_TRUE(modem_flow_receive(&flow, MODEM_FLOW_XOFF));
    EXPECT_FALSE(modem_flow_can_send(&flow, true));
    modem_flow_update(&flow, true, MODEM_FLOW_HIGH_WATER);
    EXPECT_EQ(modem_flow_control_pending(&flow), MODEM_FLOW_XOFF);
    modem_flow_control_sent(&flow, MODEM_FLOW_XOFF);
    EXPECT_EQ(modem_flow_control_pending(&flow), -1);
    modem_flow_update(&flow, true, MODEM_FLOW_LOW_WATER);
    EXPECT_EQ(modem_flow_control_pending(&flow), MODEM_FLOW_XON);
    modem_flow_control_sent(&flow, MODEM_FLOW_XON);
    EXPECT_TRUE(modem_flow_receive(&flow, MODEM_FLOW_XON));
    EXPECT_TRUE(modem_flow_can_send(&flow, false));
    EXPECT_FALSE(modem_flow_receive(&flow, 'A'));
}

TEST(ModemFlow, PendingStopCancelsAndSentStopIsReleased)
{
    modem_flow_t flow = {};
    ASSERT_TRUE(modem_flow_set_mode(&flow, MODEM_FLOW_SOFTWARE));
    modem_flow_update(&flow, true, MODEM_FLOW_HIGH_WATER);
    modem_flow_update(&flow, true, MODEM_FLOW_LOW_WATER);
    EXPECT_EQ(modem_flow_control_pending(&flow), -1);
    modem_flow_update(&flow, true, MODEM_FLOW_HIGH_WATER);
    modem_flow_control_sent(&flow, MODEM_FLOW_XOFF);
    ASSERT_TRUE(modem_flow_set_mode(&flow, MODEM_FLOW_HARDWARE));
    EXPECT_EQ(modem_flow_control_pending(&flow), MODEM_FLOW_XON);
    modem_flow_control_sent(&flow, MODEM_FLOW_XON);
    EXPECT_FALSE(modem_flow_receive(&flow, MODEM_FLOW_XOFF));
    EXPECT_FALSE(modem_flow_set_mode(&flow, 2));
    EXPECT_EQ(flow.mode, MODEM_FLOW_HARDWARE);
}
