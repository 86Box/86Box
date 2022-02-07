#ifndef QT_SETTINGSSTORAGECONTROLLERS_HPP
#define QT_SETTINGSSTORAGECONTROLLERS_HPP

#include <QWidget>

namespace Ui {
class SettingsStorageControllers;
}

class SettingsStorageControllers : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsStorageControllers(QWidget *parent = nullptr);
    ~SettingsStorageControllers();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_pushButtonSCSI4_clicked();
    void on_pushButtonSCSI3_clicked();
    void on_pushButtonSCSI2_clicked();
    void on_pushButtonSCSI1_clicked();
    void on_comboBoxSCSI4_currentIndexChanged(int index);
    void on_comboBoxSCSI3_currentIndexChanged(int index);
    void on_comboBoxSCSI2_currentIndexChanged(int index);
    void on_comboBoxSCSI1_currentIndexChanged(int index);
    void on_pushButtonQuaternaryIDE_clicked();
    void on_pushButtonTertiaryIDE_clicked();
    void on_pushButtonFD_clicked();
    void on_pushButtonHD_clicked();
    void on_checkBoxQuaternaryIDE_stateChanged(int arg1);
    void on_checkBoxTertiaryIDE_stateChanged(int arg1);
    void on_comboBoxFD_currentIndexChanged(int index);
    void on_comboBoxHD_currentIndexChanged(int index);

private:
    Ui::SettingsStorageControllers *ui;
    int machineId = 0;
};

#endif // QT_SETTINGSSTORAGECONTROLLERS_HPP
