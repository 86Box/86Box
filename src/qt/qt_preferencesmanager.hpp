#ifndef QT_PREFERENCESMANAGER_HPP
#define QT_PREFERENCESMANAGER_HPP

#include <QWidget>

namespace Ui {
class PreferencesManager;
}

/* The preferences of the VM manager. */
class PreferencesManager : public QWidget {
    Q_OBJECT

public:
    /* The system directory can only be changed while no machines are running. */
    explicit PreferencesManager(bool machinesRunning, QWidget *parent = nullptr);
    ~PreferencesManager();

    void save();

private slots:
    void chooseDirectoryLocation();
    void on_pushButtonDefaultSystemDir_released();

private:
    Ui::PreferencesManager *ui;
};

#endif // QT_PREFERENCESMANAGER_HPP
