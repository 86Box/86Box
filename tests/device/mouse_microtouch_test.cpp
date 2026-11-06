#include <gtest/gtest.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mouse.h>
#include <86box/serial.h>
#include <86box/fifo8.h>
#include <86box/video.h>
#include <86box/nvr.h>

void *mtouch_init(const device_t *info);
void  mtouch_close(void *priv);
}

// The 3M MicroTouch serial controller against the MicroTouch Touch Controllers
// Reference Guide. The serial port, timers and host mouse are mocked: commands
// go in byte by byte, the controller's bytes are captured, and each byte slot
// of its transmit timer (about 1 ms at 9600 baud) is stepped by hand.

namespace {

serial_t port;
void (*dev_write)(serial_t *, void *, uint8_t);
void *dev_priv;
int (*poll_fn)(void *);
void *poll_priv;
std::vector<uint8_t> out;
int g_but, g_pressed, g_identity = 3;
double g_x = 0.5, g_y = 0.5;
std::filesystem::path nvr_dir;
std::vector<pc_timer_t *> timers;
std::vector<int> timer_on;

} // namespace

extern "C" {

monitor_t monitors[MONITORS_NUM];
int       enable_overscan;
int       mouse_tablet_in_proximity;

void pclog(const char *, ...) { }

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    abort();
}

int
device_get_config_int(const char *name)
{
    return strcmp(name, "identity") ? 0 : g_identity;
}

FILE *
nvr_fopen(char *str, char *mode)
{
    return fopen((nvr_dir / str).string().c_str(), mode);
}

serial_t *
serial_attach_ex(int, void (*)(serial_t *, void *), void (*w)(serial_t *, void *, uint8_t),
                 void (*)(serial_t *, void *, double), void (*)(serial_t *, void *, uint8_t), void *priv)
{
    memset(&port, 0, sizeof(port));
    port.out_new = 0xffff;
    dev_write    = w;
    dev_priv     = priv;
    return &port;
}

void serial_set_cts(serial_t *, uint8_t) { }
void serial_set_dsr(serial_t *, uint8_t) { }
void serial_set_dcd(serial_t *, uint8_t) { }
void serial_write_fifo(serial_t *, uint8_t dat) { out.push_back(dat); }
int  fifo_get_full(void *) { return 0; }

int tablet_get_buttons_ex(void) { return g_but; }

int
tablet_take_pressed(void)
{
    int b     = g_pressed;
    g_pressed = 0;
    return b;
}

void
mouse_get_abs_coords(double *x, double *y)
{
    *x = g_x;
    *y = g_y;
}

void mouse_set_buttons(int) { }

void
mouse_set_poll_ex(int (*f)(void *), void *arg)
{
    poll_fn   = f;
    poll_priv = arg;
}

void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    timer->callback = callback;
    timer->priv     = priv;
    timers.push_back(timer);
    timer_on.push_back(start_timer);
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    for (size_t i = 0; i < timers.size(); i++)
        if (timers[i] == timer) {
            timer_on[i]   = 1;
            timer->period = period;
        }
}

void
timer_stop(pc_timer_t *timer)
{
    for (size_t i = 0; i < timers.size(); i++)
        if (timers[i] == timer)
            timer_on[i] = 0;
}

} // extern "C"

namespace {

class MicroTouch : public ::testing::Test {
protected:
    void *dev   = nullptr;
    int  slot   = 0;
    int  reset_left = -1;

    void SetUp() override
    {
        nvr_dir = std::filesystem::temp_directory_path() / "86box_mtouch_test";
        std::filesystem::remove_all(nvr_dir);
        std::filesystem::create_directories(nvr_dir);
        open(3);
    }

    void TearDown() override
    {
        close();
        std::filesystem::remove_all(nvr_dir);
    }

    void open(int identity)
    {
        g_identity = identity;
        g_but = g_pressed = 0;
        timers.clear();
        timer_on.clear();
        dev = mtouch_init(nullptr);
    }

    void close()
    {
        if (dev)
            mtouch_close(dev);
        dev = nullptr;
    }

    // One byte slot: the transmit timer fires; every 10 slots the 100 Hz mouse
    // poll; the reset timer fires after its 500 ms.
    void step(int n)
    {
        for (int k = 0; k < n; k++, slot++) {
            if ((slot % 10) == 0)
                poll_fn(poll_priv);
            if (timer_on[1]) {
                if (reset_left < 0)
                    reset_left = 500;
                if (--reset_left == 0) {
                    timer_on[1] = 0;
                    reset_left  = -1;
                    timers[1]->callback(timers[1]->priv);
                }
            }
            if (timer_on[0]) {
                timer_on[0] = 0;
                timers[0]->callback(timers[0]->priv);
            }
        }
    }

    std::string cmd(const char *c, int slots = 20)
    {
        out.clear();
        dev_write(&port, dev_priv, 0x01);
        for (const char *p = c; *p; p++)
            dev_write(&port, dev_priv, static_cast<uint8_t>(*p));
        dev_write(&port, dev_priv, 0x0d);
        step(slots);
        return got();
    }

    void xon() { dev_write(&port, dev_priv, 0x11); }

    void touch(double x, double y, int ms)
    {
        g_x   = x;
        g_y   = y;
        g_but = 1;
        step(ms);
        g_but = 0;
    }

    // Touch, then let the liftoff out; returns everything sent.
    std::string tap(double x, double y, int ms, int after = 30)
    {
        out.clear();
        touch(x, y, ms);
        step(after);
        return got();
    }

    static std::string got() { return std::string(out.begin(), out.end()); }
    static int x14(const std::string &p, size_t at) { return (uint8_t) p[at + 1] | ((uint8_t) p[at + 2] << 7); }
    static int y14(const std::string &p, size_t at) { return (uint8_t) p[at + 3] | ((uint8_t) p[at + 4] << 7); }
};

const std::string OK = "\x01" "0\r";

TEST_F(MicroTouch, ResetIdentityStatus)
{
    EXPECT_EQ(cmd("R", 600), OK);
    EXPECT_EQ(cmd("OI"), "\x01Q10100\r");
    EXPECT_EQ(cmd("OS"), "\x01\x40\x60\r"); // software reset flag set
    EXPECT_EQ(cmd("Z"), OK);
    EXPECT_EQ(cmd("NM"), OK); // sent by 3M TouchWare, undocumented
    EXPECT_EQ(cmd("UT"), "\x01QM****00\r");
}

TEST_F(MicroTouch, TabletStream)
{
    cmd("FT");
    cmd("MS");
    auto p = tap(0.25, 0.5, 60);
    ASSERT_GE(p.size(), 15u);
    EXPECT_EQ(p.size() % 5, 0u);
    EXPECT_EQ((uint8_t) p[0], 0xC8);
    EXPECT_EQ((uint8_t) p[p.size() - 5], 0x88);
    EXPECT_EQ(std::count(p.begin(), p.end(), '\x88'), 1);
    EXPECT_EQ(x14(p, 0), 4095);
    EXPECT_EQ(y14(p, 0), 8191); // y counts from the bottom
}

TEST_F(MicroTouch, ModesApplyToTablet)
{
    cmd("FT");
    cmd("MP");
    auto p = tap(0.5, 0.5, 60);
    ASSERT_EQ(p.size(), 5u);
    EXPECT_EQ((uint8_t) p[0], 0xC8);

    cmd("MDU");
    p = tap(0.5, 0.5, 60);
    ASSERT_EQ(p.size(), 10u);
    EXPECT_EQ((uint8_t) p[0], 0xC8);
    EXPECT_EQ((uint8_t) p[5], 0x88);

    cmd("MI");
    EXPECT_TRUE(tap(0.5, 0.5, 60).empty());
}

TEST_F(MicroTouch, QuickTapBetweenPolls)
{
    out.clear();
    g_pressed = 1; // pressed and released since the last poll
    step(40);
    auto p = got();
    EXPECT_GE(std::count(p.begin(), p.end(), '\xC8'), 1);
    EXPECT_EQ(std::count(p.begin(), p.end(), '\x88'), 1);
}

TEST_F(MicroTouch, DecimalHexadecimalAndModeStatus)
{
    cmd("FD");
    auto p = tap(0.25, 0.5, 25, 40);
    EXPECT_EQ(p.substr(0, 9), "\x01" "249,499\r");

    cmd("MT");
    p = tap(0.25, 0.5, 25, 40);
    ASSERT_GE(p.size(), 27u);
    EXPECT_EQ(p[0], '\x19');              // touchdown
    EXPECT_EQ(p[9], '\x1C');              // continued
    EXPECT_EQ(p[p.size() - 9], '\x18');   // liftoff

    cmd("FH"); // resets Mode Status
    p = tap(1.0, 0.0, 15, 40);
    EXPECT_EQ(p.substr(0, 9), "\x01" "3FF,3FF\r");
}

TEST_F(MicroTouch, BinaryIsPolled)
{
    cmd("FB");
    out.clear();
    g_x = g_y = 0.5;
    g_but = 1;
    step(40);
    EXPECT_TRUE(got().empty()); // nothing without XON

    xon();
    step(20);
    auto p = got();
    ASSERT_EQ(p.size(), 10u);
    EXPECT_EQ(p.substr(0, 6), std::string("\x17\x20\x20\x20\x20\x19", 6));
    EXPECT_EQ(((p[6] & 0x1f) << 5) | (p[7] & 0x1f), 511);

    out.clear();
    xon();
    step(20);
    p = got();
    ASSERT_EQ(p.size(), 5u);
    EXPECT_EQ(p[0], '\x1C');

    g_but = 0;
    step(20);
    out.clear();
    step(20);
    EXPECT_TRUE(got().empty()); // liftoff held until XON
    xon();
    step(20);
    p = got();
    ASSERT_EQ(p.size(), 5u);
    EXPECT_EQ(p[0], '\x18');
}

TEST_F(MicroTouch, BinaryStreamAndZone)
{
    cmd("FBS");
    auto p = tap(0.5, 0.5, 30);
    ASSERT_GE(p.size(), 15u);
    EXPECT_EQ(p[0], '\x17');
    EXPECT_EQ(p[5], '\x19');
    EXPECT_EQ(p[p.size() - 5], '\x18');

    cmd("FZ");
    p = tap(0.5, 0.5, 30);
    ASSERT_GE(p.size(), 15u);
    EXPECT_EQ(p[0], 'D');
    EXPECT_EQ(p[5], 'B');
    EXPECT_EQ(p[p.size() - 5], 'A');
}

TEST_F(MicroTouch, RawUntilReset)
{
    cmd("FR");
    out.clear();
    step(30);
    auto p = got();
    ASSERT_GE(p.size(), 14u);
    EXPECT_TRUE(p[0] & 0x80);
    EXPECT_FALSE(p[1] & 0x88);
    EXPECT_TRUE(p[7] & 0x80);

    cmd("R", 600);
    out.clear();
    step(30);
    EXPECT_TRUE(got().empty());
}

TEST_F(MicroTouch, CalibrateExtended)
{
    cmd("FT");
    EXPECT_EQ(cmd("CX"), OK);

    out.clear();
    g_pressed = 1;
    step(40);
    EXPECT_EQ(got(), "\x01" "2\r"); // too short (SMT3)
    EXPECT_EQ(tap(0.5, 0.5, 100, 20), "\x01" "0\r"); // off the target
    EXPECT_EQ(tap(0.2, 0.8, 100, 20), "\x01" "1\r"); // lower left
    EXPECT_EQ(tap(0.8, 0.2, 100, 20), "\x01" "1\r"); // upper right

    // The targets were touched at 0.2/0.8 on the raw screen; they now report 1/8 and 7/8.
    auto p = tap(0.2, 0.8, 30, 20);
    EXPECT_NEAR(x14(p, 0), 2048, 20);
    EXPECT_NEAR(y14(p, 0), 2048, 20);

    cmd("FZ");
    p = tap(0.05, 0.5, 30);
    ASSERT_GE(p.size(), 10u);
    EXPECT_EQ(p[0], 'L'); // outside the calibrated area
    EXPECT_EQ(p[p.size() - 5], 'I');
}

TEST_F(MicroTouch, CalibrateNewDecimalAcknowledgements)
{
    cmd("FD");
    cmd("CN");
    EXPECT_EQ(tap(0.02, 0.98, 100, 20), "\x01" "1\r");
    EXPECT_EQ(tap(0.98, 0.02, 100, 20), "\x01" "0\r"); // upper right positive is 0 (Table 7)
}

TEST_F(MicroTouch, ParameterLockSurvivesPowerCycle)
{
    cmd("FH");
    cmd("MDU");
    cmd("PL");
    close();
    open(3);
    auto p = tap(0.5, 0.5, 40);
    ASSERT_EQ(p.size(), 18u); // Hexadecimal, Down/Up
    EXPECT_EQ(p[0], '\x01');
    EXPECT_EQ(p[4], ',');
}

TEST_F(MicroTouch, RestoreDefaults)
{
    cmd("FD");
    cmd("MP");
    EXPECT_EQ(cmd("RD", 600), OK);
    auto p = tap(0.25, 0.5, 30);
    ASSERT_GE(p.size(), 10u);
    EXPECT_EQ((uint8_t) p[0], 0xC8); // Tablet, Stream
    EXPECT_EQ(x14(p, 0), 4095);      // factory calibration
    EXPECT_EQ(cmd("PN812"), OK);
}

TEST_F(MicroTouch, Smt2DefaultsAndPowerOnFlag)
{
    close();
    open(0); // A3
    EXPECT_EQ(cmd("OS"), "\x01\x60\x40\r");
    EXPECT_EQ(cmd("OS"), "\x01\x40\x40\r");
    auto p = tap(0.5, 0.5, 20, 40);
    ASSERT_GE(p.size(), 9u);
    EXPECT_EQ(p[0], '\x01'); // Format Decimal
    EXPECT_EQ(p[4], ',');
}

TEST_F(MicroTouch, TouchPenUnitTypeResetFlag)
{
    close();
    open(2); // P5
    EXPECT_EQ(cmd("UT"), "\x01TP****20\r");
    EXPECT_EQ(cmd("UT"), "\x01TP****00\r");
}

} // namespace
