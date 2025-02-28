#ifndef QT_SETTINGSPORTS_HPP
#define QT_SETTINGSPORTS_HPP

#include <QWidget>

namespace Ui {
class SettingsPorts;
}

class SettingsPorts : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPorts(QWidget *parent = nullptr);
    ~SettingsPorts();

    void save();

#if 0
private slots:
    void on_checkBoxSerialPassThru7_stateChanged(int state);
    void on_checkBoxSerialPassThru6_stateChanged(int state);
    void on_checkBoxSerialPassThru5_stateChanged(int state);
#endif
    void on_checkBoxSerialPassThru4_stateChanged(int state);
    void on_checkBoxSerialPassThru3_stateChanged(int state);
    void on_checkBoxSerialPassThru2_stateChanged(int state);
    void on_checkBoxSerialPassThru1_stateChanged(int state);

private slots:
#if 0
    void on_pushButtonSerialPassThru7_clicked();
    void on_pushButtonSerialPassThru6_clicked();
    void on_pushButtonSerialPassThru5_clicked();
#endif
    void on_pushButtonSerialPassThru4_clicked();
    void on_pushButtonSerialPassThru3_clicked();
    void on_pushButtonSerialPassThru2_clicked();
    void on_pushButtonSerialPassThru1_clicked();

private slots:
    void on_checkBoxParallel4_stateChanged(int arg1);
    void on_checkBoxParallel3_stateChanged(int arg1);
    void on_checkBoxParallel2_stateChanged(int arg1);
    void on_checkBoxParallel1_stateChanged(int arg1);

private:
    Ui::SettingsPorts *ui;
};

#endif // QT_SETTINGSPORTS_HPP
