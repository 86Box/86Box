#ifndef QT_SETTINGSSTORAGECONTROLLERS_HPP
#define QT_SETTINGSSTORAGECONTROLLERS_HPP

#include <QWidget>

namespace Ui {
class SettingsStorageControllers;
}

class SettingsStorageControllers : public QWidget {
    Q_OBJECT

public:
    explicit SettingsStorageControllers(QWidget *parent = nullptr);
    ~SettingsStorageControllers();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxFD_currentIndexChanged(int index);
    void on_pushButtonFD_clicked();

    void on_comboBoxHD1_currentIndexChanged(int index);
    void on_pushButtonHD1_clicked();
    void on_comboBoxHD2_currentIndexChanged(int index);
    void on_pushButtonHD2_clicked();
    void on_comboBoxHD3_currentIndexChanged(int index);
    void on_pushButtonHD3_clicked();
    void on_comboBoxHD4_currentIndexChanged(int index);
    void on_pushButtonHD4_clicked();

    void on_comboBoxCDInterface_currentIndexChanged(int index);
    void on_pushButtonCDInterface_clicked();

    void on_comboBoxSCSI1_currentIndexChanged(int index);
    void on_pushButtonSCSI1_clicked();
    void on_comboBoxSCSI2_currentIndexChanged(int index);
    void on_pushButtonSCSI2_clicked();
    void on_comboBoxSCSI3_currentIndexChanged(int index);
    void on_pushButtonSCSI3_clicked();
    void on_comboBoxSCSI4_currentIndexChanged(int index);
    void on_pushButtonSCSI4_clicked();

private:
    Ui::SettingsStorageControllers *ui;
    int                             machineId = 0;
};

#endif // QT_SETTINGSSTORAGECONTROLLERS_HPP
