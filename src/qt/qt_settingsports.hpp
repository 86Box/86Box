#ifndef QT_SETTINGSPORTS_HPP
#define QT_SETTINGSPORTS_HPP

#include <QWidget>

namespace Ui {
class SettingsPorts;
}

class SettingsPorts : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPorts(QWidget *parent = nullptr);
    ~SettingsPorts();

    void save();
private slots:
    void on_checkBoxParallel3_stateChanged(int arg1);
    void on_checkBoxParallel2_stateChanged(int arg1);
    void on_checkBoxParallel1_stateChanged(int arg1);

    void on_checkBoxParallel4_stateChanged(int arg1);

private:
    Ui::SettingsPorts *ui;
};

#endif // QT_SETTINGSPORTS_HPP
