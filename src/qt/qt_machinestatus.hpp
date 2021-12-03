#ifndef QT_MACHINESTATUS_HPP
#define QT_MACHINESTATUS_HPP

#include <QWidget>

class QStatusBar;

class MachineStatus : public QObject
{
    Q_OBJECT

public:
    explicit MachineStatus(QObject *parent = nullptr);
    ~MachineStatus();

    static bool hasCassette();
    static bool hasCartridge();
    static bool hasIDE();
    static bool hasSCSI();
    static void iterateFDD(const std::function<void(int i)>& cb);
    static void iterateCDROM(const std::function<void(int i)>& cb);
    static void iterateZIP(const std::function<void(int i)>& cb);
    static void iterateMO(const std::function<void(int i)>& cb);
public slots:
    void refresh(QStatusBar* sbar);
    void setActivity(int tag, bool active);
    void setEmpty(int tag, bool active);
    void message(const QString& msg);

private:
    struct States;
    std::unique_ptr<States> d;
};

#endif // QT_MACHINESTATUS_HPP
