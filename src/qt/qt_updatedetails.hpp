/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the update details module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_UPDATEDETAILS_HPP
#define QT_UPDATEDETAILS_HPP

#include <QDialog>
#include <QWidget>
#include "qt_updatecheck.hpp"

namespace Ui {
class UpdateDetails;
}

class UpdateDetails final : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDetails(const UpdateCheck::UpdateResult &updateResult, QWidget *parent = nullptr);
    ~UpdateDetails() override;

private:
    Ui::UpdateDetails *ui;
    static QString     jenkinsUpdateToMarkdown(const QList<UpdateCheck::JenkinsReleaseInfo> &releaseInfoList);
    static QString     githubUpdateToMarkdown(const QList<UpdateCheck::GithubReleaseInfo> &releaseInfoList);
private slots:
    static void visitDownloadPage(const UpdateCheck::UpdateChannel &channel);
};

#endif // QT_UPDATEDETAILS_HPP
