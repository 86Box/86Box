/*
* 86Box	A hypervisor and IBM PC system emulator that specializes in
*		running old operating systems and software designed for IBM
*		PC systems and compatibles from 1981 through fairly recent
*		system designs based on the PCI bus.
*
*		This file is part of the 86Box distribution.
*
*		Header for 86Box VM manager main window
*
*
*
* Authors:	cold-brewed
*
*		Copyright 2024 cold-brewed
*/

#ifndef VMM_MAINWINDOW_H
#define VMM_MAINWINDOW_H

#include "qt_vmmanager_main.hpp"

#include <QObject>
#include <QMainWindow>
#include <QProcess>

namespace Ui {
class VMManagerMainWindow;
}

class VMManagerMainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit VMManagerMainWindow(QWidget *parent = nullptr);
    ~VMManagerMainWindow() override;
signals:
    void preferencesUpdated();

private:
    Ui::VMManagerMainWindow *ui;
    VMManagerMain *vmm;
    void saveSettings() const;
    QLabel *statusLeft;
    QLabel *statusRight;
public slots:
    void setStatusLeft(const QString &text) const;
    void setStatusRight(const QString &text) const;

private slots:
    void vmmSelectionChanged(const QModelIndex &currentSelection, QProcess::ProcessState processState) const;
    void preferencesTriggered();
    static void checkForUpdatesTriggered();

protected:
    void closeEvent(QCloseEvent *event) override;
};

#endif // VMM_MAINWINDOW_H
