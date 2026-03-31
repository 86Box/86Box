#ifndef QT_PREFERENCESKEYBINDINGS_HPP
#define QT_PREFERENCESKEYBINDINGS_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class PreferencesKeyBindings;
}

class PreferencesKeyBindings : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesKeyBindings(QWidget *parent = nullptr);
    ~PreferencesKeyBindings();

    void save();

private slots:
    void on_tableKeys_cellDoubleClicked(int row, int col);
    void on_tableKeys_currentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn);

    void on_pushButtonClearBind_clicked();
    void on_pushButtonBind_clicked();

private:
    Ui::PreferencesKeyBindings *ui;
    void                        refreshInputList();
};

#endif // QT_PREFERENCESKEYBINDINGS_HPP
