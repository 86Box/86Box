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

public slots:
    void refresh(QStatusBar* sbar);
    void setActivity(int tag, bool active);
    void setEmpty(int tag, bool active);

private:
    struct States;
    std::unique_ptr<States> d;
};

#endif // QT_MACHINESTATUS_HPP
