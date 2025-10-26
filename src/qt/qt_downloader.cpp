/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Downloader module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QStandardPaths>

#include "qt_downloader.hpp"

extern "C" {
#include <86box/plat.h>
}

Downloader::
Downloader(const DownloadLocation downloadLocation, QObject *parent)
    : QObject(parent)
    , file(nullptr)
    , reply(nullptr)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    , variantData(QMetaType(QMetaType::UnknownType))
#else
    , variantData(QVariant::Invalid)
#endif
{
    char PATHBUF[256];
    switch (downloadLocation) {
        case DownloadLocation::Data:
            plat_get_global_data_dir(PATHBUF, 255);
            break;
        case DownloadLocation::Config:
            plat_get_global_config_dir(PATHBUF, 255);
            break;
        case DownloadLocation::Temp:
            plat_get_temp_dir(PATHBUF, 255);
            break;
    }
    downloadDirectory = QDir(PATHBUF);
}

Downloader::~Downloader() { delete file; }

void Downloader::download(const QUrl &url, const QString &filepath, const QVariant &varData) {

    variantData = varData;
    // temporary until I get the plat stuff fixed
    // const auto global_dir = temporaryGetGlobalDataDir();
    // qDebug() << "I was passed filepath " << filepath;
    // Join with filename to create final file
    // const auto final_path = QDir(global_dir).filePath(filepath);
    const auto final_path = downloadDirectory.filePath(filepath);

    file = new QFile(final_path);
    if(!file->open(QIODevice::WriteOnly)) {
        qWarning() << "Unable to open file " << final_path;
        return;
    }

    const auto nam = new QNetworkAccessManager(this);
    // Create the network request and execute
    const auto request = QNetworkRequest(url);
    reply = nam->get(request);
    // Connect to the finished signal
    connect(reply, &QNetworkReply::finished, this, &Downloader::onResult);
}

void
Downloader::onResult()
{
    if (reply->error()) {
        qWarning() << "Error returned from QNetworkRequest: " << reply->errorString();
        emit errorOccurred(reply->errorString());
        reply->deleteLater();
        return;
    }

    file->write(reply->readAll());
    file->flush();
    file->close();

    reply->deleteLater();
    qDebug() << Q_FUNC_INFO << "Downloaded complete: file written to " << file->fileName();
    emit downloadCompleted(file->fileName(), variantData);
}

