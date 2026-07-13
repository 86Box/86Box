#ifndef QT_PREFERENCESKEYBINDINGS_HPP
#define QT_PREFERENCESKEYBINDINGS_HPP

#include <QWidget>

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
    void onKeyBindingsRowChanged(const QModelIndex &current);
    void on_treeViewKeys_doubleClicked(const QModelIndex &idx);

    void on_pushButtonClearBind_clicked();
    void on_pushButtonBind_clicked();

private:
    Ui::PreferencesKeyBindings *ui;
    void                        refreshInputList();
};

#endif // QT_PREFERENCESKEYBINDINGS_HPP
