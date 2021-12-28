#ifndef QT_MACHINESTATUS_HPP
#define QT_MACHINESTATUS_HPP

#include <QWidget>
#include <QLabel>
#include <QMouseEvent>

#include <memory>

class QStatusBar;

class ClickableLabel : public QLabel {
    Q_OBJECT;
    public:
        explicit ClickableLabel(QWidget* parent = nullptr)
        : QLabel(parent) {}
        ~ClickableLabel() {};

    signals:
        void clicked(QPoint);
        void doubleClicked(QPoint);

    protected:
        void mousePressEvent(QMouseEvent* event) override { emit clicked(event->globalPos()); }
        void mouseDoubleClickEvent(QMouseEvent* event) override { emit doubleClicked(event->globalPos()); }
};

class MachineStatus : public QObject
{
    Q_OBJECT

public:
    explicit MachineStatus(QObject *parent = nullptr);
    ~MachineStatus();

    static bool hasCassette();
    static bool hasIDE();
    static bool hasSCSI();
    static void iterateFDD(const std::function<void(int i)>& cb);
    static void iterateCDROM(const std::function<void(int i)>& cb);
    static void iterateZIP(const std::function<void(int i)>& cb);
    static void iterateMO(const std::function<void(int i)>& cb);

    QString getMessage();
public slots:
    void refresh(QStatusBar* sbar);
    void setActivity(int tag, bool active);
    void setEmpty(int tag, bool active);
    void message(const QString& msg);
    void updateTip(int tag);

private:
    struct States;
    std::unique_ptr<States> d;
};

#endif // QT_MACHINESTATUS_HPP
