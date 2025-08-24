#ifndef QT_SETTINGSINPUT_HPP
#define QT_SETTINGSINPUT_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class SettingsInput;
}

class SettingsInput : public QWidget {
    Q_OBJECT

public:
    explicit SettingsInput(QWidget *parent = nullptr);
    ~SettingsInput();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_comboBoxKeyboard_currentIndexChanged(int index);
    void on_pushButtonConfigureKeyboard_clicked();

    void on_comboBoxMouse_currentIndexChanged(int index);
    void on_pushButtonConfigureMouse_clicked();

    void on_comboBoxJoystick0_currentIndexChanged(int index);
    void on_pushButtonJoystick01_clicked();
    void on_pushButtonJoystick02_clicked();
    void on_pushButtonJoystick03_clicked();
    void on_pushButtonJoystick04_clicked();

    void on_tableKeys_cellDoubleClicked(int row, int col);
    void on_tableKeys_currentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);

    void on_pushButtonClearBind_clicked();
    void on_pushButtonBind_clicked();

private:
    Ui::SettingsInput *ui;
    int                machineId = 0;
    void refreshInputList();
};

#endif // QT_SETTINGSINPUT_HPP
