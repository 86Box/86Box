// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <gtest/gtest.h>
#include "qt_memoryviewer.hpp"

extern "C" {
#include <86box/debugger.h>
#include <86box/mem.h>
int                      dopause;
mem_mapping_t           *read_mapping[MEM_MAPPINGS_NO], *write_mapping[MEM_MAPPINGS_NO];
static mem_mapping_t     mapping;
static uint8_t           memory[4096];
static int               state, lockDepth, ioWidth, ioWrites, ioReads, steps;
static uint32_t          ioValue;
static int memoryReads, memoryWrites;
uint32_t mem_view_read(int view, uint32_t address, int width) {
    EXPECT_GT(lockDepth, 0);
    EXPECT_EQ(view, MEM_VIEW_CPU);
    EXPECT_EQ(width, 1);
    ++memoryReads;
    return memory[address & 4095];
}
void mem_view_write(int view, uint32_t address, uint32_t value, int width) {
    EXPECT_GT(lockDepth, 0);
    EXPECT_EQ(view, MEM_VIEW_CPU);
    EXPECT_EQ(width, 1);
    ++memoryWrites;
    memory[address & 4095] = value;
}
static uint16_t          ioPort;
static bool              busy;
static QVector<uint32_t> breakpoints;
int
startblit_timeout(int)
{
    if (busy)
        return 0;
    ++lockDepth;
    return 1;
}
void
endblit(void)
{
    --lockDepth;
}
int
debugger_resetting(void)
{
    return 0;
}
void
debugger_attach(int)
{
    state = DEBUGGER_RUN;
}
int
debugger_get_state(void)
{
    return state;
}
int
debugger_stopped(void)
{
    return state == DEBUGGER_BREAK;
}
int
debugger_stopped_partial(void)
{
    return 0;
}
void
debugger_pause(void)
{
    state = DEBUGGER_BREAK;
}
void
debugger_continue(void)
{
    state = DEBUGGER_RUN;
}
void
debugger_step(int count)
{
    steps = count;
    state = DEBUGGER_STEP;
}
int
debugger_stop_reason(void)
{
    return DEBUGGER_STOP_PAUSE;
}
uint32_t
debugger_stop_count(void)
{
    return 0;
}
void
debugger_cpu_format(char *buf, int size)
{
    std::snprintf(buf, size, "EAX 12345678");
}
int
debugger_register(const char *, uint32_t *, uint32_t *)
{
    return 0;
}
int
debugger_register_write(const char *, uint32_t)
{
    return 0;
}
int
debugger_selector_base(uint16_t selector, uint32_t *base)
{
    *base = selector << 4;
    return 1;
}
int
debugger_translate(uint32_t linear, uint32_t *physical)
{
    *physical = linear;
    return 1;
}
void
debugger_mem_read(int, uint32_t address, uint32_t length, uint8_t *data, uint8_t *cls)
{
    for (uint32_t i = 0; i < length; ++i) {
        data[i] = address + i < 4096 ? memory[address + i] : 0xff;
        cls[i]  = address + i < 4096 ? DEBUGGER_MEM_RAM : DEBUGGER_MEM_UNMAPPED;
    }
}
int
debugger_mem_write(int, uint32_t address, uint8_t value)
{
    EXPECT_GT(lockDepth, 0);
    if (address >= 4096)
        return 0;
    memory[address] = value;
    return 1;
}
int
debugger_breakpoint_present(uint32_t address)
{
    return breakpoints.contains(address);
}
int
debugger_breakpoint_toggle(uint32_t address)
{
    if (breakpoints.removeOne(address))
        return 0;
    if (breakpoints.size() == DEBUGGER_MAX_BREAKPOINTS)
        return -1;
    breakpoints.append(address);
    return 1;
}
int
debugger_breakpoints(uint32_t *list, int max)
{
    int count = std::min(max, int(breakpoints.size()));
    std::copy_n(breakpoints.begin(), count, list);
    return count;
}
void
debugger_breakpoints_clear(void)
{
    breakpoints.clear();
}
static uint32_t
ioAccess(uint16_t port, int width)
{
    EXPECT_GT(lockDepth, 0);
    ioPort  = port;
    ioWidth = width;
    return 0x12345678;
}
uint8_t
inb(uint16_t port)
{
    ++ioReads;
    return ioAccess(port, 1);
}
uint16_t
inw(uint16_t port)
{
    ++ioReads;
    return ioAccess(port, 2);
}
uint32_t
inl(uint16_t port)
{
    ++ioReads;
    return ioAccess(port, 4);
}
void
outb(uint16_t port, uint8_t value)
{
    ++ioWrites;
    ioAccess(port, 1);
    ioValue = value;
}
void
outw(uint16_t port, uint16_t value)
{
    ++ioWrites;
    ioAccess(port, 2);
    ioValue = value;
}
void
outl(uint16_t port, uint32_t value)
{
    ++ioWrites;
    ioAccess(port, 4);
    ioValue = value;
}
}

class DebuggerCommands : public testing::Test {
protected:
    MemoryViewer   *viewer;
    QLineEdit      *input;
    QPlainTextEdit *output;
    void            SetUp() override
    {
        busy      = false;
        lockDepth = ioReads = ioWrites = ioWidth = steps = 0;
        breakpoints.clear();
        std::memset(memory, 0, sizeof(memory));
        read_mapping[0] = &mapping;
        viewer          = new MemoryViewer;
        input           = viewer->findChild<QLineEdit *>(QStringLiteral("debuggerCommandInput"));
        output          = viewer->findChild<QPlainTextEdit *>(QStringLiteral("debuggerCommandOutput"));
        ASSERT_NE(input, nullptr);
        ASSERT_NE(output, nullptr);
    }
    void TearDown() override
    {
        delete viewer;
        EXPECT_EQ(lockDepth, 0);
    }
    void key(int code)
    {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
        QApplication::sendEvent(input, &event);
    }
    void command(const char *text)
    {
        input->setText(QString::fromLatin1(text));
        key(Qt::Key_Return);
    }
};

TEST_F(DebuggerCommands, ReturnDoesNotToggleExecutionAndHistoryRestoresDraft)
{
    command("pause");
    EXPECT_EQ(state, DEBUGGER_BREAK);
    command("db 10 10");
    EXPECT_EQ(state, DEBUGGER_BREAK);
    input->setText(QStringLiteral("unfinished"));
    key(Qt::Key_Up);
    EXPECT_EQ(input->text(), QStringLiteral("db 10 10"));
    key(Qt::Key_Down);
    EXPECT_EQ(input->text(), QStringLiteral("unfinished"));
    command("step 10");
    EXPECT_EQ(steps, 10);
    EXPECT_EQ(state, DEBUGGER_STEP);
}

TEST_F(DebuggerCommands, MemoryCommandsShareSelectionAndValidateBeforeWriting)
{
    command("pause");
    command("goto 20");
    command("write . aa bb");
    EXPECT_EQ(memory[0x20], 0xaa);
    EXPECT_EQ(memory[0x21], 0xbb);
    command("write . cc 100");
    EXPECT_EQ(memory[0x20], 0xaa);
    command("db . 2");
    EXPECT_TRUE(output->toPlainText().contains(QStringLiteral("00000020  AA BB")));
    command("db ffffffff 2");
    EXPECT_TRUE(output->toPlainText().contains(QStringLiteral("no address wrap")));
    command("run");
    command("write . 00");
    EXPECT_EQ(memory[0x20], 0xaa);
}

TEST_F(DebuggerCommands, ExplicitMemorySideEffects)
{
    memoryReads = memoryWrites = 0;
    command("dbio 20 2");
    command("writeio 20 aa");
    EXPECT_EQ(memoryReads, 0);
    EXPECT_EQ(memoryWrites, 0);
    command("pause");
    command("writeio 20 aa bb");
    EXPECT_EQ(memoryWrites, 2);
    command("dbio 20 2");
    EXPECT_EQ(memoryReads, 2);
    command("db 20 2");
    EXPECT_EQ(memoryReads, 2);
    command("writeio 20 cc 100");
    EXPECT_EQ(memoryWrites, 2);
}

TEST_F(DebuggerCommands, IoWidthsRangeChecksAndPauseAreEnforced)
{
    command("peek 80");
    EXPECT_EQ(ioReads, 0);
    command("pause");
    command("peek 80");
    EXPECT_EQ(ioReads, 1);
    EXPECT_EQ(ioPort, 0x80);
    EXPECT_EQ(ioWidth, 1);
    command("peek 80 w");
    EXPECT_EQ(ioWidth, 2);
    command("peek 80 d");
    EXPECT_EQ(ioWidth, 4);
    command("poke 80 1234 w");
    EXPECT_EQ(ioWrites, 1);
    EXPECT_EQ(ioValue, 0x1234u);
    command("poke 80 100 b");
    command("poke 10000 01");
    command("poke 80 1 q");
    EXPECT_EQ(ioWrites, 1);
    busy = true;
    command("poke 80 01");
    EXPECT_EQ(ioWrites, 1);
}

TEST_F(DebuggerCommands, BreakpointsAreIdempotentAndPhysicalSelectionIsNotLinear)
{
    command("bp .");
    EXPECT_TRUE(breakpoints.isEmpty());
    command("bp 1234");
    command("bp 1234");
    EXPECT_EQ(breakpoints.size(), 1);
    command("bc 1234");
    command("bc 1234");
    EXPECT_TRUE(breakpoints.isEmpty());
}

int
main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
