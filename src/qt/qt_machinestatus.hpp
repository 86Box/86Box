#ifndef QT_MACHINESTATUS_HPP
#define QT_MACHINESTATUS_HPP

#include <QAction>
#include <QMenu>
#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QMimeData>

#include <memory>

class QStatusBar;

class ClickableLabel : public QLabel {
    Q_OBJECT;

public:
    explicit ClickableLabel(QWidget *parent = nullptr)
        : QLabel(parent)
    {
    }
    ~ClickableLabel() {};

signals:
    void clicked(QPoint);
    void doubleClicked(QPoint);
    void dropped(QString);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void mousePressEvent(QMouseEvent *event) override { emit clicked(event->globalPosition().toPoint()); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { emit doubleClicked(event->globalPosition().toPoint()); }
#else
    void mousePressEvent(QMouseEvent *event) override { emit clicked(event->globalPos()); }
    void mouseDoubleClickEvent(QMouseEvent *event) override { emit doubleClicked(event->globalPos()); }
#endif
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
        } else
            event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1) {
            event->setDropAction(Qt::CopyAction);
            event->acceptProposedAction();
        } else
            event->ignore();
    }
    void dropEvent(QDropEvent *event) override
    {
        if (event->dropAction() == Qt::CopyAction) {
            emit dropped(event->mimeData()->urls()[0].toLocalFile());
        } else
            event->ignore();
    }
};

class MachineStatus : public QObject {
    Q_OBJECT

public:
    explicit MachineStatus(QObject *parent = nullptr);
    ~MachineStatus();

    static bool hasCassette();
    static bool hasIDE();
    static bool hasSCSI();
    static void iterateFDD(const std::function<void(int i)> &cb);
    static void iterateCDROM(const std::function<void(int i)> &cb);
    static void iterateRDisk(const std::function<void(int i)> &cb);
    static void iterateMO(const std::function<void(int i)> &cb);
    static void iterateNIC(const std::function<void(int i)> &cb);

    QString getMessage();
    void    clearActivity();
    void    setSoundMenu(QMenu* menu);
public slots:
    void refresh(QStatusBar *sbar);
    void message(const QString &msg);
    void updateTip(int tag);
    void refreshEmptyIcons();
    void refreshIcons();
    void updateSoundIcon();

private:
    struct States;
    std::unique_ptr<States> d;
    QTimer                 *refreshTimer;
    QMenu                  *soundMenu;
};

#endif // QT_MACHINESTATUS_HPP
