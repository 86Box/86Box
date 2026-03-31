#ifndef QT_SETTINGS_COMPLETER
#define QT_SETTINGS_COMPLETER

#include <QWidget>
#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QStandardItemModel>

namespace Ui {
class SettingsCompleter;
}

class SettingsCompleter : public QObject {
public:
    explicit SettingsCompleter(QComboBox *cb, QComboBox *cbSort);
    SettingsCompleter()  = default;
    ~SettingsCompleter() = default;

    void addMachine(int i, int j);
    void addDevice(const void *device, QString name);

    void removeRows();

private:
    QComboBox *         comboBoxMain = nullptr;
    QComboBox *         comboBoxSort = nullptr;

    QCompleter *        completer    = nullptr;
    QStandardItemModel *model        = nullptr;

    int                 rows         = 0;

    bool                eventFilter(QObject *watched, QEvent *event);
    void                addRow(QString name, QString alias, int special, int id);
};

#endif // QT_SETTINGS_COMPLETER
