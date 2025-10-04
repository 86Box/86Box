/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Header for the update check module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#ifndef QT_UPDATECHECK_HPP
#define QT_UPDATECHECK_HPP

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QWidget>

#include <optional>

class UpdateCheck final : public QObject {
    Q_OBJECT
public:
    enum class UpdateChannel {
        Stable,
        CI,
    };

    struct JenkinsChangeSetItem {
        QString     buildId;       // sha hash
        QString     author;        // github username
        QString     message;       // commit message
        QStringList affectedPaths; // list of files in the change
    };

    struct JenkinsReleaseInfo {
        int                         buildNumber = 0;
        QString                     result;
        qint64                      timestamp = 0;
        QList<JenkinsChangeSetItem> changeSetItems;
    };

    struct GithubReleaseInfo {
        QString name;
        QString tag_name;
        QString html_url;
        QString target_commitish;
        QString created_at;
        QString published_at;
        QString body;
    };

    struct UpdateResult {
        UpdateChannel             channel;
        bool                      updateAvailable = false;
        bool                      upToDate        = false;
        QString                   currentVersion;
        QString                   latestVersion;
        QList<GithubReleaseInfo>  githubInfo;
        QList<JenkinsReleaseInfo> jenkinsInfo;
    };

    explicit UpdateCheck(UpdateChannel channel, QObject *parent = nullptr);
    ~UpdateCheck() override;
    void                         checkForUpdates();
    static int                   versionCompare(const QString &version1, const QString &version2);
    [[nodiscard]] static QString getCurrentVersion(const UpdateChannel &updateChannel = UpdateChannel::Stable);

signals:
    // void updateCheckComplete(const UpdateCheck::UpdateChannel &channel, const QVariant &updateData);
    void updateCheckComplete(const UpdateCheck::UpdateResult &result);
    void updateCheckError(const QString &errorMsg);

private:
    UpdateChannel updateChannel = UpdateChannel::Stable;

    const QUrl githubReleaseApi = QUrl("https://api.github.com/repos/86box/86Box/releases");
    const QUrl jenkinsLatestApi = QUrl("https://ci.86box.net/job/86box/lastSuccessfulBuild/api/json");
    QString    jenkinsLatestVersion;
    QString    currentVersion;

    static QUrl jenkinsLatestNReleasesUrl(const int &count);

    static std::optional<QList<JenkinsReleaseInfo>> parseJenkinsJson(const QString &filename);
    static std::optional<JenkinsReleaseInfo>        parseJenkinsRelease(const QJsonObject &json);

    static std::optional<QList<GithubReleaseInfo>> parseGithubJson(const QString &filename);
    static std::optional<GithubReleaseInfo>        parseGithubRelease(const QJsonObject &json);

private slots:
    void jenkinsDownloadComplete(const QString &filename, const QVariant &varData);
    void githubDownloadComplete(const QString &filename, const QVariant &varData);
    void generalDownloadError(const QString &error);
};

#endif // QT_UPDATECHECK_HPP
