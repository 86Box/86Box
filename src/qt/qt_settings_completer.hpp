#ifndef QT_SETTINGS_COMPLETER
#define QT_SETTINGS_COMPLETER

#include <QWidget>

namespace Ui {
class SettingsCompleter;
}

class SettingsCompleter : public QObject {
public:
    explicit SettingsCompleter(QComboBox *cb, QComboBox *cbSort);
    SettingsCompleter()  = default;
    ~SettingsCompleter() = default;
    void addMachine(int i, int j);

private:
    QComboBox *         comboBoxMain;
    QComboBox *         comboBoxSort;

    QCompleter *        completer;
    QStandardItemModel *model;

    bool                eventFilter(QObject *watched, QEvent *event);
};

#endif // QT_SETTINGS_COMPLETER
