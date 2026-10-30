/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Memory viewer and debugger window.
 *
 *          Everything it shows is read through the debugger core
 *          (debugger.c), which never calls a device's handlers: only the
 *          bytes on screen (and the page table entries that map them) are
 *          read, and memory only devices can answer for is shown, not read.
 */
#include "qt_memoryviewer.hpp"

#include <algorithm>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

extern "C" {
#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/debugger.h>

int startblit_timeout(int timeout_ms);
}

#include "qt_disasm.hpp"

/* Colours for the classes of memory. */
static const QColor romBg(0xff, 0xe0, 0x60);
static const QColor romFg(0x00, 0x00, 0x00);
static const QColor unmappedFg(0x90, 0x90, 0x90);
static const QColor inaccessibleBg(0xc0, 0x00, 0x00);
static const QColor inaccessibleFg(0xff, 0xff, 0xff);

#ifdef USE_DISASM
static constexpr int DISASM_LINES = 32;
#endif

static bool
readable(uint8_t cls)
{
    return (cls == DEBUGGER_MEM_RAM) || (cls == DEBUGGER_MEM_ROM) || (cls == DEBUGGER_MEM_VIDEO);
}

/* What the viewer may write: RAM, and video memory (debugger_mem_write()). */
static bool
writable(uint8_t cls)
{
    return (cls == DEBUGGER_MEM_RAM) || (cls == DEBUGGER_MEM_VIDEO);
}

static QFont
monoFont()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::Monospace);
    return font;
}

/* ---- MemoryHexView ---- */

MemoryHexView::MemoryHexView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFont(monoFont());
    QFontMetrics fm(font());
    charW_ = fm.horizontalAdvance(QLatin1Char('0'));
    lineH_ = fm.height();

    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setMouseTracking(true); /* for drag selection */
    /* address, 16 bytes with a gap after 8, the characters */
    setMinimumWidth(charW_ * (10 + (BYTES_PER_ROW * 3) + 1 + 1 + BYTES_PER_ROW + 2) + verticalScrollBar()->sizeHint().width());
    updateScrollBar();
}

uint32_t
MemoryHexView::topAddr() const
{
    return (uint32_t) verticalScrollBar()->value() * BYTES_PER_ROW;
}

int
MemoryHexView::visibleRows() const
{
    int rows = viewport()->height() / lineH_;
    return (rows < 1) ? 1 : rows;
}

void
MemoryHexView::updateScrollBar()
{
    const int total = (int) (0x100000000ULL / BYTES_PER_ROW);
    verticalScrollBar()->setRange(0, total - visibleRows());
    verticalScrollBar()->setPageStep(visibleRows());
    verticalScrollBar()->setSingleStep(1);

    const int width = charW_ * (10 + (BYTES_PER_ROW * 3) + 1 + 1 + BYTES_PER_ROW + 2);
    horizontalScrollBar()->setRange(0, qMax(0, width - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
}

void
MemoryHexView::setSpace(int space)
{
    space_  = space;
    nibble_ = 0;
    refresh();
}

void
MemoryHexView::goTo(uint32_t addr)
{
    sel_    = addr;
    anchor_ = addr;
    nibble_ = 0;
    const uint32_t row = addr / BYTES_PER_ROW;
    /* Put the address a few rows down from the top, for context. */
    int64_t top = (int64_t) row - 2;
    verticalScrollBar()->setValue((int) std::clamp<int64_t>(top, 0, verticalScrollBar()->maximum()));
    refresh();
}

void
MemoryHexView::ensureVisible(uint32_t addr)
{
    const int64_t row = addr / BYTES_PER_ROW;
    const int64_t top = verticalScrollBar()->value();
    if (row < top)
        verticalScrollBar()->setValue((int) row);
    else if (row >= (top + visibleRows()))
        verticalScrollBar()->setValue((int) std::min<int64_t>(row - visibleRows() + 1, verticalScrollBar()->maximum()));
}

void
MemoryHexView::refresh()
{
    const uint32_t start = topAddr();
    const uint32_t len   = (uint32_t) std::min<uint64_t>((uint64_t) visibleRows() * BYTES_PER_ROW, 0x100000000ULL - start);

    QVector<uint8_t> data(len);
    QVector<uint8_t> cls(len);

    if (!MemoryViewer::lock()) {
        emit status(tr("The emulator is busy; the memory view was not updated."));
        return;
    }
    debugger_mem_read(space_, start, len, data.data(), cls.data());
    MemoryViewer::unlock();

    cacheAddr_ = start;
    data_      = data;
    cls_       = cls;
    viewport()->update();
}

/* Length of the multi-cell selection, from the anchor to the cursor. */
uint64_t
MemoryHexView::selectionLen() const
{
    return (uint64_t) selectionEnd() - (uint64_t) selectionStart() + 1ULL;
}

/* The selection, for the status bar. */
QString
MemoryHexView::selectionText() const
{
    if (sel_ == anchor_)
        return QString::asprintf("%08X", sel_);
    return QString::asprintf("%08X-%08X (%llu bytes)", selectionStart(), selectionEnd(), (unsigned long long) selectionLen());
}

void
MemoryHexView::setSelection(uint32_t addr, bool extend)
{
    if (!extend)
        anchor_ = addr;
    sel_    = addr;
    nibble_ = 0;
    emit status(selectionText());
    viewport()->update();
}

void
MemoryHexView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
    refresh();
}

void
MemoryHexView::scrollContentsBy(int, int)
{
    refresh();
}

void
MemoryHexView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const QPalette &pal = palette();
    const int       xoff = -horizontalScrollBar()->value();
    const int       rows = visibleRows();
    const int       hexX = xoff + charW_ * 10;
    const int       ascX = hexX + charW_ * ((BYTES_PER_ROW * 3) + 2);
    const int       ascent = QFontMetrics(font()).ascent();

    p.fillRect(viewport()->rect(), pal.color(QPalette::Base));
    p.setFont(font());

    for (int r = 0; r < rows; r++) {
        const uint64_t rowAddr = (uint64_t) cacheAddr_ + (uint64_t) r * BYTES_PER_ROW;
        const int      y       = r * lineH_;
        if (rowAddr > 0xffffffffULL)
            break;

        p.setPen(pal.color(QPalette::Text));
        p.drawText(xoff, y + ascent, QString::asprintf("%08X", (uint32_t) rowAddr));

        for (int i = 0; i < BYTES_PER_ROW; i++) {
            const uint32_t idx = (uint32_t) (r * BYTES_PER_ROW + i);
            if (idx >= (uint32_t) data_.size())
                break;

            const uint8_t cls  = cls_[idx];
            const uint8_t val  = data_[idx];
            const int     hx   = hexX + charW_ * (i * 3 + ((i >= 8) ? 1 : 0));
            const int     ax   = ascX + charW_ * i;
            const QRect   hexRect(hx, y, charW_ * 2, lineH_);
            const QRect   ascRect(ax, y, charW_, lineH_);
            QString       hexText;
            QChar         ascChar;
            QColor        fg = pal.color(QPalette::Text);

            switch (cls) {
                case DEBUGGER_MEM_RAM:
                case DEBUGGER_MEM_ROM:
                case DEBUGGER_MEM_VIDEO:
                    hexText = QString::asprintf("%02X", val);
                    ascChar = ((val >= 0x20) && (val < 0x7f)) ? QChar(val) : QChar('.');
                    /* Video memory is mapped, not RAM: yellow as well. */
                    if (cls != DEBUGGER_MEM_RAM) {
                        p.fillRect(hexRect.adjusted(0, 0, (i == 7) ? charW_ * 2 : charW_, 0), romBg);
                        p.fillRect(ascRect, romBg);
                        fg = romFg;
                    }
                    break;
                case DEBUGGER_MEM_INACCESSIBLE:
                    hexText = QStringLiteral("??");
                    ascChar = QChar('?');
                    p.fillRect(hexRect.adjusted(0, 0, (i == 7) ? charW_ * 2 : charW_, 0), inaccessibleBg);
                    p.fillRect(ascRect, inaccessibleBg);
                    fg = inaccessibleFg;
                    break;
                default:
                    hexText = QStringLiteral("--");
                    ascChar = QChar(' ');
                    fg      = unmappedFg;
                    break;
            }

            const uint32_t addr = (uint32_t) rowAddr + i;
            if (addr >= selectionStart() && addr <= selectionEnd()) {
                /* A translucent highlight, over whatever class was drawn. */
                QColor selBg = pal.color(QPalette::Highlight);
                selBg.setAlpha(120);
                p.fillRect(hexRect.adjusted(0, 0, (i == 7) ? charW_ * 2 : charW_, 0), selBg);
                p.fillRect(ascRect, selBg);
            }
            if (addr == sel_) {
                QPen pen(pal.color(QPalette::Highlight));
                pen.setWidth(2);
                p.setPen(pen);
                p.drawRect(hexRect.adjusted(-1, 0, 0, -1));
                p.drawRect(ascRect.adjusted(-1, 0, 0, -1));
                if (nibble_)
                    hexText = QString::asprintf("%X_", editVal_ >> 4);
            }

            p.setPen(fg);
            p.drawText(hx, y + ascent, hexText);
            p.drawText(ax, y + ascent, QString(ascChar));
        }
    }
}

int
MemoryHexView::hitTest(const QPoint &pos, uint32_t *addr) const
{
    const int xoff = -horizontalScrollBar()->value();
    const int hexX = xoff + charW_ * 10;
    const int ascX = hexX + charW_ * ((BYTES_PER_ROW * 3) + 2);
    const int row  = pos.y() / lineH_;
    int       col  = -1;

    if ((pos.x() >= hexX) && (pos.x() < (ascX - charW_ * 2))) {
        int c = (pos.x() - hexX) / charW_;
        if (c >= 25) /* the gap after the eighth byte */
            c--;
        col = qMin(c / 3, BYTES_PER_ROW - 1);
    } else if ((pos.x() >= ascX) && (pos.x() < (ascX + charW_ * BYTES_PER_ROW)))
        col = (pos.x() - ascX) / charW_;

    if (col < 0)
        return 0;

    const uint64_t a = (uint64_t) cacheAddr_ + (uint64_t) row * BYTES_PER_ROW + col;
    if (a > 0xffffffffULL)
        return 0;
    *addr = (uint32_t) a;
    return 1;
}

void
MemoryHexView::mousePressEvent(QMouseEvent *event)
{
    uint32_t addr;
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton && hitTest(event->pos(), &addr)) {
        dragging_ = true;
        setSelection(addr, event->modifiers().testFlag(Qt::ShiftModifier));
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void
MemoryHexView::mouseMoveEvent(QMouseEvent *event)
{
    /* Extend the selection while the left button is held. */
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
        uint32_t addr;
        if (hitTest(event->pos(), &addr) && (addr != sel_))
            setSelection(addr, true);
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void
MemoryHexView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        dragging_ = false;
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void
MemoryHexView::moveSelection(int64_t delta, bool extend)
{
    const int64_t a = std::clamp<int64_t>((int64_t) sel_ + delta, 0, 0xffffffffLL);
    setSelection((uint32_t) a, extend);
    ensureVisible(sel_);
}

void
MemoryHexView::keyPressEvent(QKeyEvent *event)
{
    const bool extend = event->modifiers().testFlag(Qt::ShiftModifier);
    switch (event->key()) {
        case Qt::Key_Left:
            moveSelection(-1, extend);
            return;
        case Qt::Key_Right:
            moveSelection(1, extend);
            return;
        case Qt::Key_Up:
            moveSelection(-BYTES_PER_ROW, extend);
            return;
        case Qt::Key_Down:
            moveSelection(BYTES_PER_ROW, extend);
            return;
        case Qt::Key_PageUp:
            moveSelection(-(int64_t) visibleRows() * BYTES_PER_ROW, extend);
            return;
        case Qt::Key_PageDown:
            moveSelection((int64_t) visibleRows() * BYTES_PER_ROW, extend);
            return;
        case Qt::Key_Home:
            moveSelection(-(int64_t) (sel_ % BYTES_PER_ROW), extend);
            return;
        case Qt::Key_End:
            moveSelection(BYTES_PER_ROW - 1 - (sel_ % BYTES_PER_ROW), extend);
            return;
        case Qt::Key_C:
            if (event->modifiers() & Qt::ControlModifier) {
                copySelection(event->modifiers() & Qt::ShiftModifier);
                return;
            }
            break;
        case Qt::Key_Escape:
            if (nibble_) {
                nibble_ = 0;
                viewport()->update();
                return;
            }
            if (sel_ != anchor_) {
                anchor_ = sel_;
                emit status(selectionText());
                viewport()->update();
                return;
            }
            break;
        default:
            break;
    }

    bool      ok    = false;
    const int digit = event->text().toInt(&ok, 16);
    if (!ok || (event->text().size() != 1) || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    /* Only RAM and video memory can be written; the cached class is what
       is on screen. */
    const uint32_t idx = sel_ - cacheAddr_;
    if ((idx >= (uint32_t) cls_.size()) || !writable(cls_[idx])) {
        QApplication::beep();
        emit status(tr("Only RAM and video memory can be written."));
        return;
    }

    if (!nibble_) {
        editVal_ = (uint8_t) (digit << 4);
        nibble_  = 1;
        viewport()->update();
        return;
    }

    editVal_ |= (uint8_t) digit;
    nibble_ = 0;
    if (writeByte(sel_, editVal_))
        moveSelection(1, false);
}

bool
MemoryHexView::writeByte(uint32_t addr, uint8_t val)
{
    int written = 0;
    if (MemoryViewer::lock()) {
        written = debugger_mem_write(space_, addr, val);
        MemoryViewer::unlock();
    }
    if (!written) {
        QApplication::beep();
        emit status(QString::asprintf("%08X: ", addr) + tr("not written (not writable RAM or video memory, or the emulator is busy)."));
    } else
        emit memoryWritten();
    refresh();
    return written != 0;
}

/* Write a byte over a range; returns how many made it through. A page at a
   time, letting the emulator run in between, so a large fill does not
   hold it up for long. */
uint64_t
MemoryHexView::writeRange(uint32_t start, uint64_t len, uint8_t val)
{
    uint64_t written = 0;
    uint64_t done    = 0;
    while (done < len) {
        const uint64_t addr = (uint64_t) start + done;
        const uint64_t n    = std::min<uint64_t>(len - done, 0x1000 - (addr & 0xfff));
        if (!MemoryViewer::lock()) {
            QApplication::beep();
            emit status(tr("The emulator is busy; try again."));
            break;
        }
        for (uint64_t i = 0; i < n; i++)
            written += debugger_mem_write(space_, (uint32_t) (addr + i), val);
        MemoryViewer::unlock();
        done += n;
    }
    if (written > 0)
        emit memoryWritten();
    refresh();
    return written;
}

/* Copy the selected bytes to the clipboard, as hex or ASCII. */
void
MemoryHexView::copySelection(bool ascii)
{
    /* A selection the whole address space would not fit; cap the copy. */
    const uint64_t selLen = selectionLen();
    const uint64_t count  = std::min<uint64_t>(selLen, 0x1000000ULL);
    const uint64_t start  = selectionStart();
    QString        text;
    uint64_t       unreadable = 0;

    /* A chunk at a time, letting the emulator run in between. */
    constexpr uint32_t chunk = 0x10000;
    QVector<uint8_t>   data(chunk);
    QVector<uint8_t>   cls(chunk);
    uint64_t           done = 0;
    while (done < count) {
        const uint32_t n = (uint32_t) std::min<uint64_t>(chunk, count - done);
        if (!MemoryViewer::lock()) {
            emit status(tr("The emulator is busy; try again."));
            return;
        }
        debugger_mem_read(space_, (uint32_t) (start + done), n, data.data(), cls.data());
        MemoryViewer::unlock();
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t c = cls[i];
            if (ascii) {
                const uint8_t v = data[i];
                text += (readable(c) && (v >= 0x20) && (v < 0x7f)) ? QChar(v) : QChar('.');
                if (!readable(c))
                    unreadable++;
            } else {
                text += readable(c) ? QString::asprintf("%02X ", data[i]) : QStringLiteral("-- ");
                if (!readable(c))
                    unreadable++;
            }
        }
        done += n;
    }

    if (!ascii && text.endsWith(QLatin1Char(' ')))
        text.chop(1);
    QApplication::clipboard()->setText(text);

    QString msg = tr("Copied %1 bytes from %2 to the clipboard").arg(count).arg(QString::asprintf("%08X", (uint32_t) start));
    if (unreadable)
        msg += tr(" (%1 could not be read)").arg(unreadable);
    if (count < selLen)
        msg += tr(" of a %1 byte selection").arg(selLen);
    emit status(msg + QLatin1Char('.'));
}

/* Fill the selected bytes with one value. */
void
MemoryHexView::fillSelection()
{
    const uint64_t selLen = selectionLen();
    const uint64_t count  = std::min<uint64_t>(selLen, 0x1000000ULL);
    const uint32_t start  = selectionStart();

    bool          ok   = false;
    const QString text = QInputDialog::getText(this, tr("Fill selection"),
                                               tr("Fill %1 bytes from %2 with (hex):")
                                                   .arg(count)
                                                   .arg(QString::asprintf("%08X", start)),
                                               QLineEdit::Normal, QStringLiteral("00"), &ok);
    if (!ok)
        return;
    const uint32_t val = text.trimmed().toUInt(&ok, 16);
    if (!ok || (val > 0xff)) {
        emit status(tr("Enter a hex byte from 00 to FF."));
        return;
    }

    const uint64_t written = writeRange(start, count, (uint8_t) val);
    QString        msg = tr("Filled %1 of %2 bytes with %3 (%4 were not writable RAM or video memory).")
                          .arg(written)
                          .arg(count)
                          .arg(QString::asprintf("%02X", val))
                          .arg(count - written);
    if (count < selLen)
        msg += tr(" Selected: %1 bytes (%2 were skipped).").arg(selLen).arg(selLen - count);
    emit status(msg + QLatin1Char('.'));
}

void
MemoryHexView::mouseDoubleClickEvent(QMouseEvent *event)
{
    uint32_t addr;
    if (!hitTest(event->pos(), &addr))
        return;
    setSelection(addr, false);

    const uint32_t idx = addr - cacheAddr_;
    if ((idx >= (uint32_t) cls_.size()) || !writable(cls_[idx])) {
        QApplication::beep();
        emit status(tr("Only RAM and video memory can be written."));
        return;
    }

    bool          ok   = false;
    const QString text = QInputDialog::getText(this, tr("Edit byte"), QString::asprintf("%08X:", addr), QLineEdit::Normal,
                                               QString::asprintf("%02X", data_[idx]), &ok);
    if (!ok)
        return;
    const uint32_t val = text.trimmed().toUInt(&ok, 16);
    if (!ok || (val > 0xff)) {
        emit status(tr("Enter a hex byte from 00 to FF."));
        return;
    }
    writeByte(addr, (uint8_t) val);
}

void
MemoryHexView::contextMenuEvent(QContextMenuEvent *event)
{
    uint32_t addr;
    /* Clicking outside the selection starts a new one; inside keeps it. */
    if (hitTest(event->pos(), &addr) && ((addr < selectionStart()) || (addr > selectionEnd())))
        setSelection(addr, false);

    QMenu    menu(this);
    QAction *copy    = menu.addAction(tr("Copy selection (hex)"));
    QAction *copyAsc = menu.addAction(tr("Copy selection (ASCII)"));
    QAction *fill = menu.addAction(tr("Fill selection..."));
    menu.addSeparator();
    QAction *addrAct = menu.addAction(tr("Copy address"));
    QAction *dis     = menu.addAction(tr("Disassemble here"));
    QAction *dump    = menu.addAction(tr("Dump memory from here..."));
    dis->setEnabled(space_ == DEBUGGER_SPACE_LINEAR);

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == copy)
        copySelection(false);
    else if (chosen == copyAsc)
        copySelection(true);
    else if (chosen == fill)
        fillSelection();
    else if (chosen == addrAct)
        QApplication::clipboard()->setText(QString::asprintf("%08X", sel_));
    else if (chosen == dis)
        emit disassembleRequested(sel_);
    else if (chosen == dump)
        emit dumpRequested(selectionStart(), (uint32_t) std::min<uint64_t>(selectionLen(), 0xffffffffULL));
}

/* ---- MemoryViewer ---- */

/* Nested locks only count: a refresh takes the lock once for all its
   parts, so it waits for the emulator at most once. */
static int lockDepth = 0;

bool
MemoryViewer::lock()
{
    if (lockDepth > 0) {
        lockDepth++;
        return true;
    }
    if (!startblit_timeout(250))
        return false;
    /* A hard reset is freeing the memory this would read. */
    if (debugger_resetting()) {
        endblit();
        return false;
    }
    lockDepth = 1;
    return true;
}

void
MemoryViewer::unlock()
{
    if (--lockDepth == 0)
        endblit();
}

MemoryViewer::MemoryViewer(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Memory viewer"));
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_DeleteOnClose);

    /* The two run buttons are icons that rule each other out: one of
       them is checked at all times, mirroring the debugger's state. */
    pauseBtn_    = new QPushButton(QIcon(":/menuicons/qt/icons/pause.ico"), QString(), this);
    continueBtn_ = new QPushButton(QIcon(":/menuicons/qt/icons/run.ico"), QString(), this);
    auto *stepBtn = new QPushButton(tr("Step"), this);
    pauseBtn_->setShortcut(QKeySequence(Qt::Key_F6));
    continueBtn_->setShortcut(QKeySequence(Qt::Key_F5));
    stepBtn->setShortcut(QKeySequence(Qt::Key_F11));
    pauseBtn_->setToolTip(tr("Stop the processor at the next instruction (F6)"));
    continueBtn_->setToolTip(tr("Let the processor run (F5)"));
    /* Icons only: the names are for screen readers. */
    pauseBtn_->setAccessibleName(tr("Pause"));
    continueBtn_->setAccessibleName(tr("Continue"));
    /* Checkable, in a mutually exclusive group, like the state they show. */
    pauseBtn_->setCheckable(true);
    continueBtn_->setCheckable(true);
    runGroup_ = new QButtonGroup(this);
    runGroup_->addButton(pauseBtn_);
    runGroup_->addButton(continueBtn_);
    runGroup_->setExclusive(true);
    stepBtn->setToolTip(tr("Run the given number of instructions (F11)"));
    stepCount_ = new QSpinBox(this);
    stepCount_->setRange(1, 1000000);
    stepCount_->setValue(1);
    stepCount_->setToolTip(tr("Instructions per step"));

    spaceBox_ = new QComboBox(this);
    spaceBox_->addItem(tr("Physical"), DEBUGGER_SPACE_PHYSICAL);
    spaceBox_->addItem(tr("Linear"), DEBUGGER_SPACE_LINEAR);
    addrEdit_ = new QLineEdit(this);
    addrEdit_->setPlaceholderText(tr("Address, register or seg:off"));
    auto *goBtn = new QPushButton(tr("Go"), this);
    liveBox_    = new QCheckBox(tr("Live update"), this);
    liveBox_->setChecked(true);
    auto *dumpBtn = new QPushButton(tr("Dump..."), this);

    auto *bar = new QHBoxLayout;
    bar->addWidget(pauseBtn_);
    bar->addWidget(continueBtn_);
    bar->addWidget(stepBtn);
    bar->addWidget(stepCount_);
    bar->addSpacing(12);
    bar->addWidget(spaceBox_);
    bar->addWidget(addrEdit_, 1);
    bar->addWidget(goBtn);
    bar->addWidget(liveBox_);
    bar->addWidget(dumpBtn);

    hex_ = new MemoryHexView(this);

    /* The legend. */
    auto *legend = new QHBoxLayout;
    auto  swatch = [this, legend](const QString &text, const QString &style) {
        auto *l = new QLabel(text, this);
        l->setStyleSheet(style + QStringLiteral(" padding: 1px 6px; border: 1px solid palette(mid);"));
        legend->addWidget(l);
    };
    swatch(tr("RAM"), QStringLiteral(""));
    swatch(tr("Mapped, not RAM"), QStringLiteral("background-color: #ffe060; color: black;"));
    swatch(tr("Unmapped") + QStringLiteral(" (--)"), QStringLiteral("color: #909090;"));
    swatch(tr("Inaccessible") + QStringLiteral(" (?\?)"), QStringLiteral("background-color: #c00000; color: white;"));
    legend->addStretch(1);
    auto *legendNote = new QLabel(tr("Inaccessible: only device handlers answer there (reading would have side effects), or the linear page is not present."), this);
    legendNote->setWordWrap(true);
    legendNote->setStyleSheet(QStringLiteral("color: palette(dark);"));

    auto *left       = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(hex_, 1);
    leftLayout->addLayout(legend);
    leftLayout->addWidget(legendNote);

    cpuText_ = new QPlainTextEdit(this);
    cpuText_->setReadOnly(true);
    cpuText_->setFont(monoFont());
    cpuText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    auto *cpuBox    = new QGroupBox(tr("Processor"), this);
    auto *cpuLayout = new QVBoxLayout(cpuBox);
    cpuLayout->addWidget(cpuText_);

    disasmEdit_ = new QLineEdit(this);
    disasmEdit_->setPlaceholderText(tr("Linear address, register or seg:off"));
    followBox_ = new QCheckBox(tr("Follow CS:IP"), this);
    followBox_->setChecked(true);
    auto *disGo = new QPushButton(tr("Go"), this);
    disasm_     = new QTreeWidget(this);
    disasm_->setColumnCount(5);
    disasm_->setHeaderLabels({ QString(), tr("Address"), tr("Bytes"), tr("Instruction"), tr("Note") });
    disasm_->setRootIsDecorated(false);
    disasm_->setUniformRowHeights(true);
    disasm_->setFont(monoFont());
    disasm_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    disasm_->setToolTip(tr("Double-click a line to set or clear a breakpoint there"));
    auto *disBox    = new QGroupBox(tr("Disassembly"), this);
    auto *disLayout = new QVBoxLayout(disBox);
    auto *disBar    = new QHBoxLayout;
    disBar->addWidget(disasmEdit_, 1);
    disBar->addWidget(disGo);
    disBar->addWidget(followBox_);
    disLayout->addLayout(disBar);
    disLayout->addWidget(disasm_, 1);
#ifndef USE_DISASM
    disBox->setEnabled(false);
    disBox->setTitle(tr("Disassembly (not available in this build)"));
#endif

    auto *right = new QSplitter(Qt::Vertical, this);
    right->addWidget(cpuBox);
    right->addWidget(disBox);
    right->setStretchFactor(1, 1);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({ hex_->minimumWidth(), hex_->minimumWidth() });
    right->setSizes({ 360, 440 });

    statusLabel_ = new QLabel(this);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(bar);
    layout->addWidget(split, 1);
    layout->addWidget(statusLabel_);

    resize(1400, 820);

    connect(pauseBtn_, &QPushButton::clicked, this, &MemoryViewer::onPause);
    connect(continueBtn_, &QPushButton::clicked, this, &MemoryViewer::onContinue);
    connect(stepBtn, &QPushButton::clicked, this, &MemoryViewer::onStep);
    connect(goBtn, &QPushButton::clicked, this, &MemoryViewer::onGo);
    connect(addrEdit_, &QLineEdit::returnPressed, this, &MemoryViewer::onGo);
    connect(spaceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MemoryViewer::onSpaceChanged);
    connect(dumpBtn, &QPushButton::clicked, this, [this]() { onDump(hex_->selection(), (uint32_t) hex_->selectionLen()); });
    connect(disGo, &QPushButton::clicked, this, &MemoryViewer::onDisasmGo);
    connect(disasmEdit_, &QLineEdit::returnPressed, this, &MemoryViewer::onDisasmGo);
    connect(followBox_, &QCheckBox::toggled, this, [this](bool) { refreshDisasm(); });
    connect(disasm_, &QTreeWidget::itemDoubleClicked, this, &MemoryViewer::onDisasmItemActivated);
    connect(hex_, &MemoryHexView::status, statusLabel_, &QLabel::setText);
    connect(hex_, &MemoryHexView::dumpRequested, this, &MemoryViewer::onDump);
    connect(hex_, &MemoryHexView::memoryWritten, this, &MemoryViewer::refreshDisasm);
    connect(hex_, &MemoryHexView::disassembleRequested, this, [this](uint32_t addr) {
        followBox_->setChecked(false);
        disasmAddr_ = addr;
        disasmEdit_->setText(QString::asprintf("%08X", addr));
        refreshDisasm();
    });

    /* Attaching turns off the recompiler for as long as the window is open. */
    debugger_attach(1);
    lastStops_ = debugger_stop_count();

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &MemoryViewer::onPoll);
    timer_->start(100);

    hex_->goTo(0);
    refreshAll();
}

MemoryViewer::~MemoryViewer()
{
    debugger_attach(0);
}

void
MemoryViewer::closeEvent(QCloseEvent *event)
{
    timer_->stop();
    debugger_attach(0);
    QDialog::closeEvent(event);
}

void
MemoryViewer::onPause()
{
    debugger_pause();
    updateStatus();
}

void
MemoryViewer::onContinue()
{
    debugger_continue();
    updateStatus();
}

void
MemoryViewer::onStep()
{
    debugger_step(stepCount_->value());
    updateStatus();
}

/* One of the two run buttons is always checked: the state the debugger
   is in. The exclusive group unchecks the other on a click; this sets
   both where the state goes another way (a step, a breakpoint). */
void
MemoryViewer::updateRunButtons()
{
    const bool stopped = (debugger_get_state() == DEBUGGER_BREAK);
    pauseBtn_->setChecked(stopped);
    continueBtn_->setChecked(!stopped);
}

void
MemoryViewer::onSpaceChanged(int index)
{
    hex_->setSpace(spaceBox_->itemData(index).toInt());
}

void
MemoryViewer::onGo()
{
    uint32_t addr;
    QString  err;
    if (!parseAddress(addrEdit_->text(), hex_->space(), &addr, &err)) {
        statusLabel_->setText(err);
        return;
    }
    hex_->goTo(addr);
}

void
MemoryViewer::onDisasmGo()
{
    uint32_t addr;
    QString  err;
    if (!parseAddress(disasmEdit_->text(), DEBUGGER_SPACE_LINEAR, &addr, &err)) {
        statusLabel_->setText(err);
        return;
    }
    followBox_->setChecked(false);
    disasmAddr_ = addr;
    refreshDisasm();
}

void
MemoryViewer::onDisasmItemActivated(QTreeWidgetItem *item, int)
{
    if (!item)
        return;
    const uint32_t addr = item->data(1, Qt::UserRole).toUInt();
    if (debugger_breakpoint_toggle(addr) < 0)
        statusLabel_->setText(tr("Too many breakpoints."));
    refreshDisasm();
}

void
MemoryViewer::onPoll()
{
    const int      state = debugger_get_state();
    const uint32_t stops = debugger_stop_count();

    if ((state != lastState_) || (stops != lastStops_)) {
        const bool stopped = (stops != lastStops_);
        lastState_         = state;
        lastStops_         = stops;
        updateStatus();
        if (stopped) {
            refreshAll();
            if (debugger_stop_reason() == DEBUGGER_STOP_BREAKPOINT) {
                raise();
                activateWindow();
            }
        }
        return;
    }

    if ((state != DEBUGGER_BREAK) && liveBox_->isChecked() && (++liveTicks_ >= 5)) {
        liveTicks_ = 0;
        refreshAll();
    }
}

void
MemoryViewer::refreshAll()
{
    if (!lock()) {
        statusLabel_->setText(tr("The emulator is busy; the view was not updated."));
        return;
    }
    hex_->refresh();
    refreshCpu();
    refreshDisasm();
    unlock();
    updateStatus();
}

void
MemoryViewer::updateStatus()
{
    QString text;

    switch (debugger_get_state()) {
        case DEBUGGER_BREAK:
            switch (debugger_stop_reason()) {
                case DEBUGGER_STOP_BREAKPOINT:
                    text = tr("Stopped at a breakpoint.");
                    break;
                case DEBUGGER_STOP_STEP:
                    text = tr("Stopped after stepping.");
                    break;
                default:
                    text = tr("Stopped.");
                    break;
            }
            break;
        case DEBUGGER_STEP:
            text = tr("Stepping...");
            break;
        default:
            text = tr("Running.");
            break;
    }

    if (dopause)
        text += QStringLiteral(" ") + tr("The emulator is paused; resume it to run or step.");
    text += QStringLiteral(" ") + tr("The dynamic recompiler is off while this window is open.");
    statusLabel_->setText(text);
    updateRunButtons();
}

void
MemoryViewer::refreshCpu()
{
    QByteArray buf(4096, '\0');

    if (!lock())
        return;
    debugger_cpu_format(buf.data(), buf.size());
    unlock();

    const int pos = cpuText_->verticalScrollBar()->value();
    cpuText_->setPlainText(QString::fromLatin1(buf.constData()));
    cpuText_->verticalScrollBar()->setValue(pos);
}

void
MemoryViewer::refreshDisasm()
{
#ifdef USE_DISASM
    debugger_cpu_info_t info;
    QVector<DisasmInsn> lines;
    QVector<bool>       bps;
    uint32_t            segBase = 0;

    if (!lock())
        return;
    debugger_cpu_info(&info);
    {
        Disassembler dis;
        uint32_t     ip = followBox_->isChecked() ? info.ip : (disasmAddr_ - dis.csBase());
        if (!followBox_->isChecked() && dis.wraps() && (ip > 0xffff)) {
            /* Out of CS's reach where IP wraps at 64 KB: decode in the
               segment the address starts. */
            dis.setBase(disasmAddr_ & ~0x0f);
            ip = disasmAddr_ & 0x0f;
        }
        segBase = dis.csBase();
        for (int i = 0; i < DISASM_LINES; i++) {
            /* The instruction at CS:IP may already be in the prefetch queue. */
            DisasmInsn d = dis.decode(ip, (dis.csBase() == info.cs_base) && (ip == info.ip));
            bps.append(debugger_breakpoint_present(d.lin));
            lines.append(d);
            /* Past something unreadable, go on at the next byte. */
            ip = dis.next(ip, d.len);
        }
    }
    unlock();

    if (followBox_->isChecked())
        disasmEdit_->setText(QString::asprintf("%04X:%0*X", info.cs_sel, info.code32 ? 8 : 4, info.ip));

    /* The rows are kept and rewritten, not made anew: a live update between
       the two clicks of a double-click (a breakpoint) would otherwise take
       the row it started on away, and the double-click with it. */
    while (disasm_->topLevelItemCount() > lines.size())
        delete disasm_->takeTopLevelItem(disasm_->topLevelItemCount() - 1);
    while (disasm_->topLevelItemCount() < lines.size())
        new QTreeWidgetItem(disasm_);

    QTreeWidgetItem *current = nullptr;
    for (int n = 0; n < lines.size(); n++) {
        const DisasmInsn &l    = lines[n];
        QTreeWidgetItem  *item = disasm_->topLevelItem(n);
        QString           bytes;
        for (int c = 0; c < 5; c++) {
            item->setBackground(c, QBrush());
            item->setForeground(c, QBrush());
        }
        for (int i = 0; (i < l.len) && (i < 16); i++)
            bytes += QString::asprintf("%02X", l.bytes[i]);

        item->setText(0, bps[n] ? QStringLiteral("\u25cf") : QString());
        item->setForeground(0, QBrush(Qt::red));
        if (segBase == info.cs_base)
            item->setText(1, QString::asprintf("%08X  %04X:%0*X", l.lin, info.cs_sel, info.code32 ? 8 : 4, info.code32 ? l.ip : (l.ip & 0xffff)));
        else if (!info.protected_mode)
            item->setText(1, QString::asprintf("%08X  %04X:%04X", l.lin, segBase >> 4, l.ip & 0xffff));
        else
            item->setText(1, QString::asprintf("%08X", l.lin));
        item->setData(1, Qt::UserRole, l.lin);
        item->setText(2, bytes);
        item->setText(3, l.text);
        item->setText(4, l.note);
        item->setToolTip(3, l.note);
        item->setToolTip(4, l.note);
        if (l.len <= 0)
            item->setForeground(3, QBrush(unmappedFg));
        else if (l.invalid)
            item->setForeground(3, QBrush(inaccessibleBg));
        if (l.lin == info.pc_linear) {
            for (int c = 1; c < 5; c++) {
                item->setBackground(c, palette().brush(QPalette::Highlight));
                item->setForeground(c, palette().brush(QPalette::HighlightedText));
            }
            current = item;
        }
    }
    if (current)
        disasm_->scrollToItem(current);
#endif
}

/* Hex number, with an optional 0x prefix or h suffix. */
static bool
parseHex(const QString &text, uint32_t *val)
{
    QString t = text.trimmed();
    if (t.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        t = t.mid(2);
    else if (t.endsWith(QLatin1Char('h'), Qt::CaseInsensitive))
        t.chop(1);
    if (t.isEmpty() || (t.size() > 8))
        return false;
    bool ok = false;
    *val    = t.toUInt(&ok, 16);
    return ok;
}

bool
MemoryViewer::parseAddress(const QString &text, int space, uint32_t *addr, QString *err)
{
    const QString t   = text.trimmed().toLower();
    const int     sep = t.indexOf(QLatin1Char(':'));
    bool          ret = false;
    uint32_t      val;

    if (t.isEmpty()) {
        *err = tr("Enter an address.");
        return false;
    }

    if (!lock()) {
        *err = tr("The emulator is busy; try again.");
        return false;
    }

    if (sep >= 0) {
        const QByteArray segName = t.left(sep).trimmed().toLatin1();
        const QByteArray offName = t.mid(sep + 1).trimmed().toLatin1();
        uint32_t         base    = 0;
        uint32_t         off     = 0;
        uint32_t         sel;

        if (debugger_register(segName.constData(), &sel, &base) && (segName.size() == 2) && (segName[1] == 's'))
            ret = true;
        else if (parseHex(QString::fromLatin1(segName), &sel) && (sel <= 0xffff)) {
            ret = debugger_selector_base((uint16_t) sel, &base);
            if (!ret)
                *err = tr("The selector cannot be looked up.");
        } else
            *err = tr("Unknown segment \"%1\".").arg(QString::fromLatin1(segName));

        if (ret) {
            if (!debugger_register(offName.constData(), &off, nullptr) && !parseHex(QString::fromLatin1(offName), &off)) {
                *err = tr("Unknown offset \"%1\".").arg(QString::fromLatin1(offName));
                ret  = false;
            }
        }

        /* seg:off is linear; the physical view wants where that is now. */
        val = base + off;
        if (ret && (space == DEBUGGER_SPACE_PHYSICAL) && !debugger_translate(val, &val)) {
            *err = tr("The linear address is not mapped to physical memory.");
            ret  = false;
        }
    } else {
        const QByteArray name = t.toLatin1();
        ret                   = debugger_register(name.constData(), &val, nullptr) || parseHex(t, &val);
        if (!ret)
            *err = tr("\"%1\" is not an address or a register.").arg(text.trimmed());
    }
    unlock();

    if (ret)
        *addr = val;
    return ret;
}

void
MemoryViewer::onDump(uint32_t start, uint32_t len)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Dump memory"));

    auto *startEdit = new QLineEdit(QString::asprintf("%08X", start), &dlg);
    auto *lenEdit   = new QLineEdit(len ? QString::asprintf("%X", len) : QStringLiteral("10000"), &dlg);
    auto *space     = new QComboBox(&dlg);
    space->addItem(tr("Physical"), DEBUGGER_SPACE_PHYSICAL);
    space->addItem(tr("Linear"), DEBUGGER_SPACE_LINEAR);
    space->setCurrentIndex(space->findData(hex_->space()));
    auto *fillEdit = new QLineEdit(QStringLiteral("FF"), &dlg);
    fillEdit->setMaxLength(2);
    auto *fileEdit = new QLineEdit(&dlg);
    auto *browse   = new QPushButton(tr("Browse..."), &dlg);
    auto *fileRow  = new QHBoxLayout;
    fileRow->addWidget(fileEdit, 1);
    fileRow->addWidget(browse);

    auto *form = new QFormLayout;
    form->addRow(tr("Start (hex, register or seg:off):"), startEdit);
    form->addRow(tr("Length (hex bytes):"), lenEdit);
    form->addRow(tr("Address space:"), space);
    form->addRow(tr("Fill byte for memory that cannot be read:"), fillEdit);
    form->addRow(tr("File:"), fileRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto *layout  = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    auto *note = new QLabel(tr("Only RAM and other directly mapped memory is read. Unmapped and inaccessible bytes are not read; the fill byte is written in their place."), &dlg);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, &dlg, [&dlg, fileEdit]() {
        const QString name = QFileDialog::getSaveFileName(&dlg, tr("Dump memory"), fileEdit->text(), tr("Binary files (*.bin);;All files (*)"));
        if (!name.isEmpty())
            fileEdit->setText(name);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    uint32_t addr;
    uint32_t dumpLen;
    uint32_t fill;
    int      sp;
    for (;;) {
        if (dlg.exec() != QDialog::Accepted)
            return;

        QString err;
        sp = space->currentData().toInt();
        if (!parseAddress(startEdit->text(), sp, &addr, &err)) {
            QMessageBox::warning(&dlg, tr("Dump memory"), err);
            continue;
        }
        if (!parseHex(lenEdit->text(), &dumpLen) || !dumpLen) {
            QMessageBox::warning(&dlg, tr("Dump memory"), tr("The length must be a hex number above 0."));
            continue;
        }
        if (!parseHex(fillEdit->text(), &fill) || (fill > 0xff)) {
            QMessageBox::warning(&dlg, tr("Dump memory"), tr("The fill byte must be a hex number from 00 to FF."));
            continue;
        }
        if (((uint64_t) addr + dumpLen) > 0x100000000ULL)
            dumpLen = (uint32_t) (0x100000000ULL - addr);
        if (fileEdit->text().isEmpty()) {
            const QString name = QFileDialog::getSaveFileName(&dlg, tr("Dump memory"), QString::asprintf("mem_%08X.bin", addr), tr("Binary files (*.bin);;All files (*)"));
            if (name.isEmpty())
                continue;
            fileEdit->setText(name);
        }
        break;
    }

    QFile file(fileEdit->text());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Dump memory"), tr("Could not open %1 for writing.").arg(file.fileName()));
        return;
    }

    constexpr uint32_t chunk = 0x10000;
    QVector<uint8_t>   data(chunk);
    QVector<uint8_t>   cls(chunk);
    uint64_t           unreadable = 0;
    uint64_t           done       = 0;
    QProgressDialog    progress(tr("Dumping memory..."), tr("Cancel"), 0, (int) (((uint64_t) dumpLen + chunk - 1) / chunk), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    while (done < dumpLen) {
        const uint32_t n = (uint32_t) std::min<uint64_t>(chunk, dumpLen - done);
        if (!lock()) {
            /* A reset is under way; it ends, or the dump can be cancelled. */
            QApplication::processEvents();
            if (progress.wasCanceled())
                break;
            continue;
        }
        debugger_mem_read(sp, addr + (uint32_t) done, n, data.data(), cls.data());
        unlock();

        for (uint32_t i = 0; i < n; i++) {
            if (!readable(cls[i])) {
                data[i] = (uint8_t) fill;
                unreadable++;
            }
        }
        if (file.write((const char *) data.constData(), n) != n) {
            QMessageBox::critical(this, tr("Dump memory"), tr("Could not write to %1.").arg(file.fileName()));
            return;
        }
        done += n;
        progress.setValue((int) (done / chunk));
        if (progress.wasCanceled())
            break;
    }
    file.close();

    statusLabel_->setText(tr("Dumped %1 bytes from %2 to %3 (%4 bytes could not be read and were filled).")
                              .arg(done)
                              .arg(QString::asprintf("%08X", addr))
                              .arg(file.fileName())
                              .arg(unreadable));
}
