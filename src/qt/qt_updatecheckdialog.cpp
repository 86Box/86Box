/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Update check dialog module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDir>
#include <QTimer>

#include "qt_updatecheckdialog.hpp"
#include "ui_qt_updatecheckdialog.h"
#include "qt_updatedetails.hpp"

extern "C" {
#include <86box/version.h>
}

UpdateCheckDialog::
    UpdateCheckDialog(const UpdateCheck::UpdateChannel channel, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UpdateCheckDialog)
    , updateCheck(new UpdateCheck(channel))
{
    ui->setupUi(this);
    ui->statusLabel->setHidden(true);
    updateChannel  = channel;
    currentVersion = UpdateCheck::getCurrentVersion(updateChannel);
    connect(updateCheck, &UpdateCheck::updateCheckError, [=](const QString &errorMsg) {
        generalDownloadError(errorMsg);
    });
    connect(updateCheck, &UpdateCheck::updateCheckComplete, this, &UpdateCheckDialog::downloadComplete);

    QTimer::singleShot(0, [this] {
        updateCheck->checkForUpdates();
    });
}

UpdateCheckDialog::~UpdateCheckDialog()
    = default;

void
UpdateCheckDialog::generalDownloadError(const QString &error) const
{
    ui->progressBar->setMaximum(100);
    ui->progressBar->setValue(100);
    ui->statusLabel->setVisible(true);
    const auto statusText = tr("There was an error checking for updates:\n\n%1\n\nPlease try again later.").arg(error);
    ui->statusLabel->setText(statusText);
    ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok);
}

void
UpdateCheckDialog::downloadComplete(const UpdateCheck::UpdateResult &result)
{
    if (result.upToDate) {
        upToDate();
        return;
    }

    const auto updateDetails = new UpdateDetails(result, this);
    connect(updateDetails, &QDialog::accepted, [this] {
        accept();
    });
    connect(updateDetails, &QDialog::rejected, [this] {
        reject();
    });
    updateDetails->exec();
}

void
UpdateCheckDialog::upToDate()
{
    ui->titleLabel->setText(tr("Update check complete"));
    ui->progressBar->setMaximum(100);
    ui->progressBar->setValue(100);
    ui->statusLabel->setVisible(true);
    QString currentVersionString;
    if (updateChannel == UpdateCheck::UpdateChannel::Stable)
        currentVersionString = QString("v%1").arg(currentVersion);
    else
        currentVersionString = QString("%1 %2").arg(tr("build"), currentVersion);
    const auto statusText = tr("You are running the latest %1 version of 86Box: %2").arg(updateChannel == UpdateCheck::UpdateChannel::Stable ? tr("stable") : tr("beta"), currentVersionString);
    ui->statusLabel->setText(statusText);
    ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok);
}
