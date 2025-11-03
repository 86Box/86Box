/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the update check dialog module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_UPDATECHECKDIALOG_HPP
#define QT_UPDATECHECKDIALOG_HPP

#include <QDialog>

#include <qt_updatecheck.hpp>

namespace Ui {
class UpdateCheckDialog;
}

class UpdateCheckDialog final : public QDialog {
    Q_OBJECT
public:
    explicit UpdateCheckDialog(UpdateCheck::UpdateChannel channel, QWidget *parent = nullptr);
    ~UpdateCheckDialog() override;

private:
    Ui::UpdateCheckDialog     *ui;
    UpdateCheck::UpdateChannel updateChannel = UpdateCheck::UpdateChannel::Stable;
    UpdateCheck               *updateCheck;
    QString                    currentVersion;
    void                       upToDate();

private slots:
    void downloadComplete(const UpdateCheck::UpdateResult &result);
    void generalDownloadError(const QString &error) const;
};

#endif // QT_UPDATECHECKDIALOG_HPP
