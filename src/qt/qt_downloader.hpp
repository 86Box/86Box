/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the downloader module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_DOWNLOADER_HPP
#define QT_DOWNLOADER_HPP

#include <QDir>
#include <QNetworkAccessManager>
#include <QString>
#include <QFile>


class Downloader final : public QObject {
    Q_OBJECT
public:
    enum class DownloadLocation {
        Data,   // AppDataLocation   via plat_get_global_data_dir()
        Config, // AppConfigLocation via plat_get_global_config_dir()
        Temp    // TempLocation      via plat_get_temp_dir()
    };
    explicit Downloader(DownloadLocation downloadLocation = DownloadLocation::Data, QObject *parent = nullptr);
    ~Downloader() final;

    void download(const QUrl &url, const QString &filepath, const QVariant &varData = QVariant::Invalid);

signals:
    // Signal emitted when the download is successful
    void downloadCompleted(QString filename, QVariant varData);
    // Signal emitted when an error occurs
    void errorOccurred(const QString&);

private slots:
    void onResult();

private:
    QFile *file;
    QNetworkAccessManager nam;
    QNetworkReply *reply;
    QVariant variantData;
    QDir downloadDirectory;
};

#endif
