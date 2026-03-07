#ifndef QT_SETTINGSKEYBINDINGS_HPP
#define QT_SETTINGSKEYBINDINGS_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class SettingsKeyBindings;
}

class SettingsKeyBindings : public QWidget {
    Q_OBJECT

public:
    explicit SettingsKeyBindings(QWidget *parent = nullptr);
    ~SettingsKeyBindings();

    void save();

public slots:
    void onCurrentMachineChanged(int machineId);

private slots:
    void on_tableKeys_cellDoubleClicked(int row, int col);
    void on_tableKeys_currentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);

    void on_pushButtonClearBind_clicked();
    void on_pushButtonBind_clicked();

private:
    Ui::SettingsKeyBindings *ui;
    int                      machineId = 0;
    void                     refreshInputList();
};

#endif // QT_SETTINGSKEYBINDINGS_HPP
