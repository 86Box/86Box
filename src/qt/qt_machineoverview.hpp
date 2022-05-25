#ifndef QT_MACHINEOVERVIEW_HPP
#define QT_MACHINEOVERVIEW_HPP

#include <QDialog>

namespace Ui {
class MachineOverview;
}

class MachineOverview : public QDialog
{
    Q_OBJECT

public:
    explicit MachineOverview(QWidget *parent = nullptr);
    ~MachineOverview();

public slots:
    void refresh();

private:
    Ui::MachineOverview *ui;
};

#endif // QT_MACHINEOVERVIEW_HPP
