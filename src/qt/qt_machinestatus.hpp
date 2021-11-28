#ifndef QT_MACHINESTATUS_HPP
#define QT_MACHINESTATUS_HPP

#include <QWidget>

class QLabel;

namespace Ui {
class MachineStatus;
}

class MachineStatus : public QWidget
{
    Q_OBJECT

public:
    explicit MachineStatus(QWidget *parent = nullptr);
    ~MachineStatus();

public slots:
    void refresh();
    void setActivity(int tag, bool active);
    void setEmpty(int tag, bool active);

private:
    Ui::MachineStatus *ui;

    struct States;
    std::unique_ptr<States> d;
};

#endif // QT_MACHINESTATUS_HPP
