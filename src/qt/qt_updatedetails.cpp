/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Update details module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include "qt_updatedetails.hpp"
#include "ui_qt_updatedetails.h"
#include "qt_defs.hpp"

#include <QDesktopServices>
#include <QPushButton>

UpdateDetails::
    UpdateDetails(const UpdateCheck::UpdateResult &updateResult, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UpdateDetails)
{
    ui->setupUi(this);
    ui->updateTitle->setText(tr("<b>An update to 86Box is available!</b>"));
    QString currentVersionText;
    QString latestVersionText;
    if (updateResult.channel == UpdateCheck::UpdateChannel::Stable) {
        currentVersionText = tr("You are currently running version <b>%1</b>.").arg(updateResult.currentVersion);
        latestVersionText  = tr("<b>Version %1</b> is now available.").arg(updateResult.latestVersion);
    } else {
        currentVersionText = tr("You are currently running build <b>%1</b>.").arg(updateResult.currentVersion);
        latestVersionText  = tr("<b>Build %1</b> is now available.").arg(updateResult.latestVersion);
    }
    if (updateResult.currentVersion.isEmpty())
        currentVersionText = "";

    const auto updateDetailsText = QString("%1 %2%3").arg(latestVersionText, currentVersionText.append(' '), tr("Would you like to visit the download page?"));
    ui->updateDetails->setText(updateDetailsText);

    if (updateResult.channel == UpdateCheck::UpdateChannel::Stable) {
        ui->updateText->setMarkdown(githubUpdateToMarkdown(updateResult.githubInfo));
    } else {
        ui->updateText->setMarkdown(jenkinsUpdateToMarkdown(updateResult.jenkinsInfo));
    }

    const auto downloadButton = new QPushButton(tr("Visit download page"));
    ui->buttonBox->addButton(downloadButton, QDialogButtonBox::AcceptRole);
    // Override accepted to mean "I want to visit the download page"
    connect(ui->buttonBox, &QDialogButtonBox::accepted, [updateResult] {
        visitDownloadPage(updateResult.channel);
    });
    const auto logo = QIcon(EMU_ICON_PATH).pixmap(QSize(64, 64));

    ui->icon->setPixmap(logo);
}

UpdateDetails::~UpdateDetails()
    = default;

QString
UpdateDetails::jenkinsUpdateToMarkdown(const QList<UpdateCheck::JenkinsReleaseInfo> &releaseInfoList)
{
    QStringList fullText;
    for (const auto &update : releaseInfoList) {
        fullText.append(QString("### Build %1").arg(update.buildNumber));
        fullText.append("Changes:");
        for (const auto &item : update.changeSetItems) {
            fullText.append(QString("* %1").arg(item.message));
        }
        fullText.append("\n\n\n---\n\n\n");
    }
    // pop off the last hr
    fullText.removeLast();
    // return fullText.join("\n\n---\n\n");
    return fullText.join("\n");
}

QString
UpdateDetails::githubUpdateToMarkdown(const QList<UpdateCheck::GithubReleaseInfo> &releaseInfoList)
{
    // The github release info can be rather large so we'll only
    // display the most recent one
    QList<UpdateCheck::GithubReleaseInfo> singleRelease;
    if (!releaseInfoList.isEmpty()) {
        singleRelease.append(releaseInfoList.first());
    }
    QStringList fullText;
    for (const auto &release : singleRelease) {
        fullText.append(QString("#### %1").arg(release.name));
        // Github body text should already be in markdown and can just
        // be placed here as-is
        fullText.append(release.body);
        fullText.append("\n\n\n---\n\n\n");
    }
    // pop off the last hr
    fullText.removeLast();
    return fullText.join("\n");
}
void
UpdateDetails::visitDownloadPage(const UpdateCheck::UpdateChannel &channel)
{
    switch (channel) {
        case UpdateCheck::UpdateChannel::Stable:
            QDesktopServices::openUrl(QUrl("https://github.com/86Box/86Box/releases/latest"));
            break;
        case UpdateCheck::UpdateChannel::CI:
            QDesktopServices::openUrl(QUrl("https://86box.net/builds#"
#ifdef Q_OS_WINDOWS
                                           "win"
#elif defined(Q_OS_MACOS)
                                           "mac"
#elif defined(Q_OS_LINUX)
                                           "lin"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
                                           "arm64"
#elif defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
                                           "64"
#endif

#ifdef USE_NEW_DYNAREC
                                           "ndr"
#else
                                           "odr"
#endif
                                           ));
            break;
    }
}
