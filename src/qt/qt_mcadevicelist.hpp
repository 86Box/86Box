#ifndef QT_MCADEVICELIST_HPP
#define QT_MCADEVICELIST_HPP

#include <QDialog>

namespace Ui {
class MCADeviceList;
}

class MCADeviceList : public QDialog
{
    Q_OBJECT

public:
    explicit MCADeviceList(QWidget *parent = nullptr);
    ~MCADeviceList();

private:
    Ui::MCADeviceList *ui;
};

#endif // QT_MCADEVICELIST_HPP
