/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for 86Box VM manager main module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_VMMANAGER_MAIN_H
#define QT_VMMANAGER_MAIN_H

#include <QWidget>
#include "qt_vmmanager_model.hpp"
#include "qt_vmmanager_details.hpp"
#include "qt_vmmanager_listviewdelegate.hpp"

#include <QPushButton>

extern "C" {
#include <86box/86box.h> // for vmm_path
#include <86box/version.h>
}

#if EMU_BUILD_NUM != 0
#    include "qt_updatecheck.hpp"
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class VMManagerMain; }
QT_END_NAMESPACE

class VMManagerMain final : public QWidget {
    Q_OBJECT

public:
    explicit VMManagerMain(QWidget *parent = nullptr);
    ~VMManagerMain() override;
    // Used to save the current selection
    [[nodiscard]] QString getCurrentSelection() const;

    enum class ToolbarButton {
        Start,
        Pause,
        StartPause,
        Shutdown,
        Reset,
        CtrlAltDel,
        Settings,
    };
signals:
    void selectionChanged(const QModelIndex &currentSelection, QProcess::ProcessState processState);
    void updateStatusLeft(const QString &text);
    void updateStatusRight(const QString &text);

public slots:
    void startButtonPressed() const;
    void settingsButtonPressed();
    void restartButtonPressed() const;
    void pauseButtonPressed() const;
    void shutdownRequestButtonPressed() const;
    void shutdownForceButtonPressed() const;
    void searchSystems(const QString &text) const;
    void newMachineWizard();
    void updateGlobalSettings();
    void deleteSystem(VMManagerSystem *sysconfig);
    void addNewSystem(const QString &name, const QString &dir, const QString &displayName = QString(), const QString &configFile = {});
#if __GNUC__ >= 11
    [[nodiscard]] QStringList getSearchCompletionList() const;
#else
    QStringList getSearchCompletionList() const;
#endif
    void modelDataChange();
    void onPreferencesUpdated();
    void onLanguageUpdated();
#ifdef Q_OS_WINDOWS
    void onDarkModeUpdated();
#endif
    void onConfigUpdated(const QString &uuid);
    int  getActiveMachineCount();

    QList<int> getPaneSizes() const;
    void       setPaneSizes(const QList<int> &sizes);

private:
    Ui::VMManagerMain *ui;

    VMManagerModel        *vm_model;
    VMManagerDetails      *vm_details;
    VMManagerSystem       *selected_sysconfig;
    // VMManagerConfig       *config;
    QSortFilterProxyModel *proxy_model;
#if EMU_BUILD_NUM != 0
    bool                   updateCheck = false;
#endif
    bool                   regexSearch = false;

    // void updateSelection(const QItemSelection &selected,
    //                      const QItemSelection &deselected);
    void currentSelectionChanged(const QModelIndex &current,
                       const QModelIndex &previous);
    void refresh();
    void updateDisplayName(const QModelIndex &index);
    void loadSettings();
    [[nodiscard]] bool currentSelectionIsValid() const;
    [[nodiscard]] QString machineCountString(QString states = "") const;
#if EMU_BUILD_NUM != 0
    void backgroundUpdateCheckStart() const;
#endif
    void showTextFileContents(const QString &title, const QString &path);
private slots:
#if EMU_BUILD_NUM != 0
    void backgroundUpdateCheckComplete(const UpdateCheck::UpdateResult &result);
    void backgroundUpdateCheckError(const QString &errorMsg);
#endif
};

#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDir>

class IconSelectionDialog final : public QDialog {
    Q_OBJECT

public:
    explicit IconSelectionDialog(QString assetPath, QWidget *parent = nullptr) : QDialog(parent), listWidget(new QListWidget) {
        // Set the list widget to icon mode
        listWidget->setViewMode(QListWidget::IconMode);
        setFixedSize(QSize(540, 360));
        listWidget->setGridSize(QSize(96, 96));
        listWidget->setIconSize(QSize(64, 64));
        // Read in all the assets from the given path
        const QDir iconsDir(assetPath);
        if (!assetPath.endsWith("/")) {
            assetPath.append("/");
        }
        setWindowTitle(tr("Select an icon"));

        // Loop on all files and add them as items (icons) in QListWidget
        for(const QString& iconName : iconsDir.entryList()) {
            const auto item = new QListWidgetItem(QIcon(assetPath + iconName), iconName);
            // Set the UserRole to the resource bundle path
            item->setData(Qt::UserRole, assetPath + iconName);
            listWidget->addItem(item);
        }

        // Dialog buttons
        const auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset);
        // Use the reset button for resetting the icon to the default
        const QPushButton* resetButton = buttonBox->button(QDialogButtonBox::Reset);

        // Connect the buttons signals
        connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(listWidget, &QListWidget::doubleClicked, this, &QDialog::accept);
        connect(resetButton, &QPushButton::clicked, [this] {
            // For reset, set the index to invalid so the caller will receive a blank string
            listWidget->setCurrentIndex(QModelIndex());
            // Then accept
            QDialog::accept();
        });

        const auto layout = new QVBoxLayout(this);
        layout->addWidget(listWidget);
        layout->addWidget(buttonBox);
    }

    public slots:
    [[nodiscard]] QString getSelectedIconName() const {
        if (listWidget->currentIndex().isValid()) {
            return listWidget->currentItem()->data(Qt::UserRole).toString();
        }
        // Index is invalid because the reset button was pressed
        return {};
    }

private:
    QListWidget* listWidget;
};

#endif //QT_VMMANAGER_MAIN_H
