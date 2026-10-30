#ifndef QT_MEMORYVIEWER_HPP
#define QT_MEMORYVIEWER_HPP

#include <QAbstractScrollArea>
#include <QDialog>
#include <QVector>

class QButtonGroup;
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

/* A hex view of one address space that reads only the rows it shows. */
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

signals:
    void status(const QString &msg);
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

    int      space_ = 0;
    uint32_t sel_    = 0;
    uint32_t anchor_ = 0; /* the other end of the selection */
    int      nibble_ = 0; /* 1 after the high nibble of an edit was typed */
    uint8_t  editVal_ = 0;

    uint32_t         cacheAddr_ = 0;
    QVector<uint8_t> data_;
    QVector<uint8_t> cls_;

    bool dragging_ = false;

    int charW_ = 8;
    int lineH_ = 16;
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

private slots:
    void onPause();
    void onContinue();
    void onStep();
    void onGo();
    void onSpaceChanged(int index);
    void onPoll();
    void onDisasmGo();
    void onDisasmItemActivated(QTreeWidgetItem *item, int column);
    void onDump(uint32_t start, uint32_t len);
    void refreshAll();

private:
    void refreshCpu();
    void refreshDisasm();
    void updateStatus();
    void updateRunButtons();

    MemoryHexView  *hex_;
    QButtonGroup   *runGroup_;
    QPushButton    *pauseBtn_;
    QPushButton    *continueBtn_;
    QComboBox      *spaceBox_;
    QLineEdit      *addrEdit_;
    QCheckBox      *liveBox_;
    QSpinBox       *stepCount_;
    QPlainTextEdit *cpuText_;
    QTreeWidget    *disasm_;
    QLineEdit      *disasmEdit_;
    QCheckBox      *followBox_;
    QLabel         *statusLabel_;
    QTimer         *timer_;

    int      lastState_  = -1;
    uint32_t lastStops_  = 0;
    int      liveTicks_  = 0;
    uint32_t disasmAddr_ = 0;
};

#endif // QT_MEMORYVIEWER_HPP
