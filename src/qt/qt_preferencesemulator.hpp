#ifndef QT_PREFERENCESEMULATOR_HPP
#define QT_PREFERENCESEMULATOR_HPP

#include <QWidget>
#include <QtGui/QStandardItemModel>
#include <QtGui/QStandardItem>
#include <QItemDelegate>
#include <QPainter>
#include <QVariant>
#include <QTableWidget>

namespace Ui {
class PreferencesEmulator;
}

class PreferencesEmulator : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesEmulator(QWidget *parent = nullptr);
    ~PreferencesEmulator();

    void save();

private slots:
    void on_pushButtonLanguage_released();

private:
    Ui::PreferencesEmulator *ui;

    SettingsCompleter       *scLanguage;

    friend class MainWindow;
};

#endif // QT_PREFERENCESEMULATOR_HPP
