/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager main window
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
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

class VMManagerMainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit VMManagerMainWindow(QWidget *parent = nullptr);
    ~VMManagerMainWindow() override;
    void updateSettings();
signals:
    void preferencesUpdated();
    void languageUpdated();
#ifdef Q_OS_WINDOWS
    void darkModeUpdated();
#endif

private:
    Ui::VMManagerMainWindow *ui;

    VMManagerMain *vmm;
    void           saveSettings() const;
    QLabel        *statusLeft;
    QLabel        *statusRight;
    QIcon          runIcon;
    QIcon          pauseIcon;

public slots:
    void setStatusLeft(const QString &text) const;
    void setStatusRight(const QString &text) const;
    void updateLanguage();
#ifdef Q_OS_WINDOWS
    void updateDarkMode();
#endif

private slots:
    void vmmStateChanged(const VMManagerSystem *sysconfig) const;
    void on_actionHide_tool_bar_triggered();
    void preferencesTriggered();
#if EMU_BUILD_NUM != 0
    void checkForUpdatesTriggered();
#endif

    void on_actionExit_triggered();
    void on_actionDocumentation_triggered();
    void on_actionAbout_86Box_triggered();
    void on_actionAbout_Qt_triggered();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
};

#endif // VMM_MAINWINDOW_H
