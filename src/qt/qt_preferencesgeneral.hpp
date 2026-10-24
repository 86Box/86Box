#ifndef QT_PREFERENCESGENERAL_HPP
#define QT_PREFERENCESGENERAL_HPP

#include <QWidget>

namespace Ui {
class PreferencesGeneral;
}

class SettingsCompleter;

/* The preferences shared by the emulator and the VM manager. */
class PreferencesGeneral : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesGeneral(QWidget *parent = nullptr);
    ~PreferencesGeneral();

    void save();

private slots:
    void on_pushButtonLanguage_released();

private:
    Ui::PreferencesGeneral *ui;

    SettingsCompleter *scLanguage;
};

#endif // QT_PREFERENCESGENERAL_HPP
