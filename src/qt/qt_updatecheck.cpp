/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Update check module
 *
 * Authors: cold-brewed
 *
 *          Copyright 2024 cold-brewed
 */
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

#include "qt_updatecheck.hpp"
#include "qt_downloader.hpp"
#include "qt_updatedetails.hpp"

extern "C" {
#include <86box/version.h>
}

UpdateCheck::
UpdateCheck(const UpdateChannel channel, QObject *parent) : QObject(parent)
{
    updateChannel = channel;
    currentVersion = getCurrentVersion(channel);
}

UpdateCheck::~
UpdateCheck()
    = default;

void
UpdateCheck::checkForUpdates()
{
    if (updateChannel == UpdateChannel::Stable) {
        const auto githubDownloader = new Downloader(Downloader::DownloadLocation::Temp);
        connect(githubDownloader, &Downloader::downloadCompleted, this, &UpdateCheck::githubDownloadComplete);
        connect(githubDownloader, &Downloader::errorOccurred, this, &UpdateCheck::generalDownloadError);
        githubDownloader->download(QUrl(githubReleaseApi), "github_releases.json");
    } else {
        const auto jenkinsDownloader = new Downloader(Downloader::DownloadLocation::Temp);
        connect(jenkinsDownloader, &Downloader::downloadCompleted, this, &UpdateCheck::jenkinsDownloadComplete);
        connect(jenkinsDownloader, &Downloader::errorOccurred, this, &UpdateCheck::generalDownloadError);
        jenkinsDownloader->download(jenkinsLatestNReleasesUrl(10), "jenkins_list.json");
    }
}

void
UpdateCheck::jenkinsDownloadComplete(const QString &filename, const QVariant &varData)
{
    auto generalError = tr("Unable to determine release information");
    auto jenkinsReleaseListResult = parseJenkinsJson(filename);
    auto latestVersion = 0; // NOLINT (Default value as a fallback)

    if(!jenkinsReleaseListResult.has_value() || jenkinsReleaseListResult.value().isEmpty()) {
        generalDownloadError(generalError);
        return;
    }
    const auto jenkinsReleaseList = jenkinsReleaseListResult.value();
    latestVersion = jenkinsReleaseListResult->first().buildNumber;

    // If we can't determine the local build (blank current version), always show an update as available.
    // Callers can adjust accordingly.
    // Otherwise, do a comparison with EMU_BUILD_NUM
    bool updateAvailable = false;
    bool upToDate = true;
    if(currentVersion.isEmpty() || EMU_BUILD_NUM < latestVersion) {
        updateAvailable = true;
        upToDate = false;
    }

    const auto updateResult = UpdateResult {
        .channel = updateChannel,
        .updateAvailable = updateAvailable,
        .upToDate = upToDate,
        .currentVersion = currentVersion,
        .latestVersion = QString::number(latestVersion),
        .githubInfo = {},
        .jenkinsInfo = jenkinsReleaseList,
    };

    emit updateCheckComplete(updateResult);
}

void
UpdateCheck::generalDownloadError(const QString &error)
{
    emit updateCheckError(error);
}

void
UpdateCheck::githubDownloadComplete(const QString &filename, const QVariant &varData)
{
    const auto generalError            = tr("Unable to determine release information");
    const auto githubReleaseListResult = parseGithubJson(filename);
    QString latestVersion = "0.0";
    if(!githubReleaseListResult.has_value() || githubReleaseListResult.value().isEmpty()) {
        generalDownloadError(generalError);
    }
    auto githubReleaseList = githubReleaseListResult.value();
    // Warning: this check (using the tag name) relies on a consistent naming scheme: "v<number>"
    // where <number> is the release number. For example, 4.2 from v4.2 as the tag name.
    // Another option would be parsing the name field which is generally "86Box <number>" but
    // either option requires a consistent naming scheme.
    latestVersion = githubReleaseList.first().tag_name.replace("v", "");
    for (const auto &release: githubReleaseList) {
        qDebug().noquote().nospace() << release.name << ": " << release.html_url << " (" << release.created_at << ")";
    }

    // const auto updateDetails = new UpdateDetails(githubReleaseList, currentVersion);
    bool updateAvailable = false;
    bool upToDate = true;
    if(currentVersion.isEmpty() || (versionCompare(currentVersion, latestVersion) < 0)) {
        updateAvailable = true;
        upToDate = false;
    }

    const auto updateResult = UpdateResult {
        .channel = updateChannel,
        .updateAvailable = updateAvailable,
        .upToDate = upToDate,
        .currentVersion = currentVersion,
        .latestVersion = latestVersion,
        .githubInfo = githubReleaseList,
        .jenkinsInfo = {},
    };

    emit updateCheckComplete(updateResult);

}

QUrl
UpdateCheck::jenkinsLatestNReleasesUrl(const int &count)
{
    const auto urlPath = QString("https://ci.86box.net/job/86box/api/json?tree=builds[number,result,timestamp,changeSets[items[commitId,affectedPaths,author[fullName],msg,id]]]{0,%1}").arg(count);
    return { urlPath };
}

QString
UpdateCheck::getCurrentVersion(const UpdateChannel &updateChannel)
{
    if (updateChannel == UpdateChannel::Stable) {
        return {EMU_VERSION};
    }
    // If EMU_BUILD_NUM is anything other than the default of zero it was set by the build process
    if constexpr (EMU_BUILD_NUM != 0) {
        return QString::number(EMU_BUILD_NUM); // NOLINT because EMU_BUILD_NUM is defined as 0 by default and is set at build time
    }
    // EMU_BUILD_NUM is not set, most likely a local build
    return {}; // NOLINT (Having EMU_BUILD_NUM assigned to a default number throws off the linter)
}

std::optional<QList<UpdateCheck::JenkinsReleaseInfo>>
UpdateCheck::parseJenkinsJson(const QString &filename)
{
    QList<JenkinsReleaseInfo> releaseInfoList;
    QFile json_file(filename);
    if (!json_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Couldn't open the json file: error" << json_file.error();
        return std::nullopt;
    }

    const QString read_file = json_file.readAll();
    json_file.close();

    const auto json_doc = QJsonDocument::fromJson(read_file.toUtf8());

    if (json_doc.isNull()) {
        qWarning("Failed to create QJsonDocument, possibly invalid JSON");
        return std::nullopt;
    }

    if (!json_doc.isObject()) {
        qWarning("JSON does not have the expected format (object in root), cannot continue");
        return std::nullopt;
    }

    auto json_object = json_doc.object();

    // The json contains multiple release
    if(json_object.contains("builds") && json_object["builds"].isArray()) {

        QJsonArray builds = json_object["builds"].toArray();
        for (const auto &each_build: builds) {
            if (auto build = parseJenkinsRelease(each_build.toObject()); build.has_value() && build.value().result == "SUCCESS") {
                releaseInfoList.append(build.value());
            }
        }
    } else if(json_object.contains("changeSets") && json_object["changeSets"].isArray()) {
        // The json contains only one release, as obtained by the lastSuccessfulBuild api
        if (const auto build = parseJenkinsRelease(json_object); build.has_value()) {
            releaseInfoList.append(build.value());
        }
    } else {
        qWarning("JSON is missing data or has invalid data, cannot continue");
        qDebug() << json_object;
        return std::nullopt;
    }

    return releaseInfoList;
}

std::optional<UpdateCheck::JenkinsReleaseInfo>
UpdateCheck::parseJenkinsRelease(const QJsonObject &json)
{
    // The root should contain number, result, and timestamp.
    if (!json.contains("number") || !json.contains("result") || !json.contains("timestamp")) {
        return std::nullopt;
    }

    auto releaseInfo = JenkinsReleaseInfo {
        .buildNumber = json["number"].toInt(),
        .result      = json["result"].toString(),
        .timestamp   = static_cast<qint64>(json["timestamp"].toDouble())
    };

    // Overview
    // Each build should contain a changeSets object with an array. Only the first element is needed.
    // The first element should be an object containing an items object with an array.
    // Each array element in the items object has information releated to the build. More or less: commit data
    // In jq parlance it would be similar to `builds[].changeSets[0].items[]`

    // To break down the somewhat complicated if-init statement below:
    // * Get the object for `changeSets`
    // * Convert the value to array
    // * Grab the first element in the array
    // Proceed if
    // * the element (first in changeSets) is an object that contains the key `items`
    if (const auto changeSet = json["changeSets"].toArray().first(); changeSet.isObject() && changeSet.toObject().contains("items")) {
        // Then proceed to process each `items` array element
        for (const auto &item : changeSet.toObject()["items"].toArray()) {
            auto itemObject = item.toObject();
            // Basic validation
            if (!itemObject.contains("commitId") || !itemObject.contains("msg") || !itemObject.contains("affectedPaths")) {
                return std::nullopt;
            }
            // Convert the paths for each commit to a string list
            QStringList paths;
            for (const auto &each_path : itemObject["affectedPaths"].toArray().toVariantList()) {
                if (each_path.type() == QVariant::String) {
                    paths.append(each_path.toString());
                }
            }
            // Build the structure
            const auto releaseItem = JenkinsChangeSetItem {
                .buildId       = itemObject["commitId"].toString(),
                .author        = itemObject["author"].toObject()["fullName"].toString(),
                .message       = itemObject["msg"].toString(),
                .affectedPaths = paths,
            };
            releaseInfo.changeSetItems.append(releaseItem);
        }
    } else {
        qWarning("Could not parse release information, possibly invalid JSON");
    }
    return releaseInfo;
}

std::optional<QList<UpdateCheck::GithubReleaseInfo>>
UpdateCheck::parseGithubJson(const QString &filename)
{
    QList<GithubReleaseInfo> releaseInfoList;
    QFile json_file(filename);
    if (!json_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("Couldn't open the json file: error %d", json_file.error());
        return std::nullopt;
    }

    const QString read_file = json_file.readAll();
    json_file.close();

    const auto json_doc = QJsonDocument::fromJson(read_file.toUtf8());

    if (json_doc.isNull()) {
        qWarning("Failed to create QJsonDocument, possibly invalid JSON");
        return std::nullopt;
    }

    if (!json_doc.isArray()) {
        qWarning("JSON does not have the expected format (array in root), cannot continue");
        return std::nullopt;
    }

    auto release_array = json_doc.array();

    for (const auto &each_release: release_array) {
        if (auto release = parseGithubRelease(each_release.toObject()); release.has_value()) {
            releaseInfoList.append(release.value());
        }
    }
    return releaseInfoList;
}
std::optional<UpdateCheck::GithubReleaseInfo>
UpdateCheck::parseGithubRelease(const QJsonObject &json)
{
    // Perform some basic validation
    if (!json.contains("name") || !json.contains("tag_name") || !json.contains("html_url")) {
        return std::nullopt;
    }

    auto githubRelease = GithubReleaseInfo {
        .name             = json["name"].toString(),
        .tag_name         = json["tag_name"].toString(),
        .html_url         = json["html_url"].toString(),
        .target_commitish = json["target_commitish"].toString(),
        .created_at       = json["created_at"].toString(),
        .published_at     = json["published_at"].toString(),
        .body             = json["body"].toString(),
    };

    return githubRelease;
}

// A simple method to compare version numbers
// Should work for comparing x.y.z and x.y. Missing
// values (parts) will be treated as zeroes
int
UpdateCheck::versionCompare(const QString &version1, const QString &version2)
{
    // Split both
    QStringList v1List = version1.split('.');
    QStringList v2List = version2.split('.');

    // Out of the two versions get the maximum amount of "parts"
    const int maxParts = std::max(v1List.size(), v2List.size());

    // Initialize both with zeros
    QVector<int> v1Parts(maxParts, 0);
    QVector<int> v2Parts(maxParts, 0);

    for (int i = 0; i < v1List.size(); ++i) {
        v1Parts[i] = v1List[i].toInt();
    }

    for (int i = 0; i < v2List.size(); ++i) {
        v2Parts[i] = v2List[i].toInt();
    }

    for (int i = 0; i < maxParts; ++i) {
        // First version is greater
        if (v1Parts[i] > v2Parts[i])
            return 1;
        // First version is less
        if (v1Parts[i] < v2Parts[i])
            return -1;
    }
    // They are equal
    return 0;
}
