#ifndef QT_MEMORYVIEWER_HPP
#define QT_MEMORYVIEWER_HPP

#include <QAbstractScrollArea>
#include <QByteArray>
#include <QDialog>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

/* A contiguous range of mapped addresses. */
struct MappedRange {
    uint32_t start;
    uint32_t length; /* in bytes, multiple of BYTES_PER_ROW for simplicity, or just page aligned */
};

/* A hex view of one address space that reads only the rows it shows,
   omitting addresses that are not mapped. */
class MemoryHexView : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit MemoryHexView(QWidget *parent = nullptr);

    void     setSpace(int space);
    int      space() const { return space_; }
    void     goTo(uint32_t addr);
    uint32_t selection() const { return sel_; }
    uint32_t selectionStart() const { return qMin(sel_, anchor_); }
    uint32_t selectionEnd() const { return qMax(sel_, anchor_); }
    uint64_t selectionLen() const;
    void     refresh();
    /* Search mapped memory for a run of bytes, selecting it when found.
       From and to bound the search to an address range, both ends
       inclusive; the defaults search the whole space. */
    bool     find(const QByteArray &pattern, bool forward, uint32_t from = 0, uint32_t to = 0xffffffffU);

signals:
    void status(const QString &msg);
    /* The selection, as one line for the viewer's status row. */
    void rangeReport(const QString &text);
    void dumpRequested(uint32_t start, uint32_t len);
    void disassembleRequested(uint32_t addr);
    void memoryWritten();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    static constexpr int BYTES_PER_ROW = 16;

    uint32_t topAddr() const;
    int      visibleRows() const;
    void     updateScrollBar();
    void     ensureVisible(uint32_t addr);
    void     setSelection(uint32_t addr, bool extend);
    void     moveSelection(int64_t delta, bool extend);
    int      hitTest(const QPoint &pos, uint32_t *addr) const;
    bool     writeByte(uint32_t addr, uint8_t val);
    uint64_t writeRange(uint32_t start, uint64_t len, uint8_t val);
    void     copySelection(bool ascii);
    void     fillSelection();
    QString  selectionText() const;
    void     selectRange(uint32_t start, uint32_t len, bool cursorAtEnd);

    /* Build the list of mapped ranges for the current space. */
    void     rebuildMappedRanges();
    /* Map a virtual row (in the dense view) to a real address. */
    uint32_t virtualRowToAddr(int64_t virtualRow) const;
    /* Map a real address to its virtual row, or -1 if unmapped. */
    int64_t  addrToVirtualRow(uint32_t addr) const;

    int      space_ = 0;
    uint32_t sel_    = 0;
    uint32_t anchor_ = 0; /* the other end of the selection */
    int      nibble_ = 0; /* 1 after the high nibble of an edit was typed */
    uint8_t  editVal_ = 0;

    uint32_t         cacheAddr_ = 0;
    QVector<uint8_t> data_;
    QVector<uint8_t> cls_;

    bool dragging_ = false;
    bool searching_ = false; /* a find is under way; not re-entered */

    int charW_ = 8;
    int lineH_ = 16;

    /* Dense view of only mapped memory. */
    QVector<MappedRange> mappedRanges_;
    uint64_t             totalMappedBytes_ = 0;
};

class MemoryViewer : public QDialog {
    Q_OBJECT

public:
    explicit MemoryViewer(QWidget *parent = nullptr);
    ~MemoryViewer() override;

    /* Parse an address: hex, a register, or segment:offset with either
       part a register or hex (segment:offset is a linear address). */
    static bool parseAddress(const QString &text, int space, uint32_t *addr, QString *err);

    static bool lock();
    static void unlock();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

private slots:
    void onRunToggle();
    void onStep();
    void onGo();
    void onSpaceChanged(int index);
    void onPoll();
    void onDisasmGo();
    void onDisasmItemActivated(QTreeWidgetItem *item, int column);
    void onDump(uint32_t start, uint32_t len);
    void onSearch(bool forward);
    void refreshAll();
    void executeCommand();

private:
    struct DisasmView;

    /* Build the bytes to search for from the search box. */
    QByteArray searchPattern(QString *err) const;

    /* The read parts take the lock; the show parts only touch widgets, so
       a refresh can update them after letting the emulator go again. */
    bool    readCpu(QString *text);
    void    showCpu(const QString &text);
    bool    readDisasm(DisasmView *view);
    void    showDisasm(const DisasmView &view);
    void    refreshCpu();
    void    refreshDisasm();
    void    updateStatus();
    /* A transient message (an error, warning or result), shown at the
       lower right of the window next to the selected range. */
    void    notice(const QString &text);
    void    updateRunButton();
    void    refreshMappings();

    MemoryHexView  *hex_;
    QPushButton    *runBtn_;
    QComboBox      *spaceBox_;
    QLineEdit      *addrEdit_;
    QComboBox      *searchTypeBox_;
    QLineEdit      *searchEdit_;
    QLineEdit      *rangeFromEdit_;
    QLineEdit      *rangeToEdit_;
    QCheckBox      *liveBox_;
    QSpinBox       *stepCount_;
    QPlainTextEdit *cpuText_;
    QTreeWidget    *disasm_;
    QLineEdit      *disasmEdit_;
    QCheckBox      *followBox_;
    QLabel         *statusLabel_;
    /* The lower right: the latest message and the selection's range. */
    QLabel         *messageLabel_;
    QLabel         *rangeLabel_;
    QTimer         *timer_;
    QPlainTextEdit *commandOutput_;
    QLineEdit      *commandInput_;
    QStringList    commandHistory_;
    QString        commandDraft_;
    int            historyIndex_ = 0;

    int      lastState_  = -1;
    uint32_t lastStops_  = 0;
    int      liveTicks_  = 0;
    uint32_t disasmAddr_ = 0;
};

#endif // QT_MEMORYVIEWER_HPP
