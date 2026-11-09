#include <cstdarg>
#include <cstdint>

#include <gtest/gtest.h>

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/serial.h>
}

namespace {
struct CallbackState {
    uint32_t flags = 0;
    int baud = 0;
    int data_bits = 0;
    int parity = -1;
    int stop_bits = 0;
    int dtr = -1;
};

void control(void *priv, uint32_t flags)
{
    static_cast<CallbackState *>(priv)->flags = flags;
}

void format(void *priv, int baud, int data_bits, int parity, int stop_bits)
{
    auto *state = static_cast<CallbackState *>(priv);
    state->baud = baud;
    state->data_bits = data_bits;
    state->parity = parity;
    state->stop_bits = stop_bits;
}

void dtr(serial_s *, int on, void *priv)
{
    static_cast<CallbackState *>(priv)->dtr = on;
}
}

TEST(SerialBackend, ForwardsOptionalDeviceCallbacks)
{
    CallbackState state;
    serial_device_t device = {};
    serial_backend_store_t store = {};
    device.priv = &state;
    device.dtr_callback = dtr;
    device.control_callback = control;
    device.format_callback = format;

    const serial_backend_t *backend = serial_backend_from_device(nullptr, &device, &store);
    ASSERT_NE(backend, nullptr);
    ASSERT_NE(backend->control, nullptr);
    ASSERT_NE(backend->set_format, nullptr);
    ASSERT_NE(backend->dtr, nullptr);
    backend->control(backend->priv, CHAR_COM_DTR | CHAR_COM_RTS);
    backend->set_format(backend->priv, 1200, 7, 0, 1);
    backend->dtr(backend->priv, 1);
    EXPECT_EQ(state.flags, CHAR_COM_DTR | CHAR_COM_RTS);
    EXPECT_EQ(state.baud, 1200);
    EXPECT_EQ(state.data_bits, 7);
    EXPECT_EQ(state.parity, 0);
    EXPECT_EQ(state.stop_bits, 1);
    EXPECT_EQ(state.dtr, 1);
}

TEST(SerialBackend, LegacyDeviceLeavesNewCallbacksAbsent)
{
    serial_device_t device = {};
    serial_backend_store_t store = {};
    device.dtr_callback = dtr;
    const serial_backend_t *backend = serial_backend_from_device(nullptr, &device, &store);
    ASSERT_NE(backend, nullptr);
    EXPECT_EQ(backend->control, nullptr);
    EXPECT_EQ(backend->set_format, nullptr);
    EXPECT_NE(backend->dtr, nullptr);
}
