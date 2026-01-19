/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager preferences module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef VMMANAGER_PREFERENCES_H
#define VMMANAGER_PREFERENCES_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class VMManagerPreferences;
}
QT_END_NAMESPACE

class VMManagerPreferences final : public QDialog {
    Q_OBJECT
public:
    explicit VMManagerPreferences(QWidget *parent = nullptr, bool machinesRunning = false);
    ~VMManagerPreferences() override;

private:
    Ui::VMManagerPreferences *ui;
    QString                   settingsFile;
private slots:
    void chooseDirectoryLocation();
    void on_pushButtonDefaultSystemDir_released();
    void on_pushButtonLanguage_released();

protected:
    void accept() override;
    void reject() override;
};

#endif // VMMANAGER_PREFERENCES_H
