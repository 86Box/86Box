/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VM manager Discord Rich Presence module
 *
 * Authors: David Hrdlička, <hrdlickadavid@outlook.com>
 *
 *          Copyright 2026 David Hrdlička.
 */
#include <QDateTime>
#include <QUrl>

#include "qt_vmmanager_discord.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/version.h>
}

static const auto kClientId = QStringLiteral("906956844956782613");
// How long a machine's window has to stay focused before it's shown instead
static constexpr int kDwellTime = 10000;
// Discord rejects text fields longer than this
static constexpr int kMaxTextLength = 128;

static const auto kIconPrefix = QStringLiteral(":/systemicons/");

static bool
isRunning(VMManagerSystem::ProcessStatus status)
{
    return (status == VMManagerSystem::ProcessStatus::Running) || (status == VMManagerSystem::ProcessStatus::RunningWaiting);
}

static bool
isPaused(VMManagerSystem::ProcessStatus status)
{
    return (status == VMManagerSystem::ProcessStatus::Paused) || (status == VMManagerSystem::ProcessStatus::PausedWaiting);
}

static QString
fitText(const QString &text, int maxLength = kMaxTextLength)
{
    if (text.size() <= maxLength)
        return text;

    // Don't split a surrogate pair
    auto cut = maxLength - 1;
    if (text.at(cut - 1).isHighSurrogate())
        cut--;
    return text.left(cut) + QChar(0x2026);
}

static void
setText(QJsonObject &object, const QString &key, const QString &text)
{
    if (text.isEmpty())
        return;

    // Discord also rejects text fields shorter than 2 characters
    object[key] = (text.size() < 2) ? QString("\"%1\"").arg(text) : fitText(text);
}

VMManagerDiscord::VMManagerDiscord(QObject *parent)
    : QObject(parent)
    , client(kClientId)
{
    clock.start();

    dwell_timer.setSingleShot(true);
    dwell_timer.setInterval(kDwellTime);
    connect(&dwell_timer, &QTimer::timeout, this, &VMManagerDiscord::dwellElapsed);

    loadSettings();
}

void
VMManagerDiscord::setModel(VMManagerModel *newModel)
{
    if (model)
        disconnect(model, nullptr, this, nullptr);
    for (auto it = tracked.constBegin(); it != tracked.constEnd(); ++it)
        disconnect(it.key(), nullptr, this, nullptr);
    tracked.clear();
    dwell_timer.stop();

    model = newModel;
    if (model) {
        for (int row = 0; row < model->rowCount(QModelIndex()); row++)
            trackSystem(model->getConfigObjectForIndex(model->index(row)));
        connect(model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int first, int last) {
            for (int row = first; row <= last; row++)
                trackSystem(model->getConfigObjectForIndex(model->index(row)));
            update();
        });
    }

    update();
}

void
VMManagerDiscord::loadSettings()
{
    enabled = discord_presence;
    privacy = static_cast<Privacy>(discord_privacy);

    if (enabled)
        client.start();
    else
        client.stop();

    update();
}

void
VMManagerDiscord::trackSystem(VMManagerSystem *system)
{
    if (tracked.contains(system))
        return;

    Tracked state;
    state.status = system->getProcessStatus();
    if (state.status != VMManagerSystem::ProcessStatus::Stopped) {
        state.started_at = QDateTime::currentMSecsSinceEpoch();
        state.used_at    = clock.elapsed();
    }
    tracked.insert(system, state);

    connect(system, &VMManagerSystem::clientProcessStatusChanged, this, [this, system] { statusChanged(system); });
    connect(system, &VMManagerSystem::windowActivated, this, [this, system] { windowActivated(system); });
    // Display name, icon and hardware changes
    connect(system, &VMManagerSystem::itemDataChanged, this, &VMManagerDiscord::update);
    connect(system, &VMManagerSystem::configurationChanged, this, &VMManagerDiscord::update);
    connect(system, &QObject::destroyed, this, [this, system] {
        tracked.remove(system);
        update();
    });
}

void
VMManagerDiscord::statusChanged(VMManagerSystem *system)
{
    auto it = tracked.find(system);
    if (it == tracked.end())
        return;

    const auto previous = it->status;
    it->status          = system->getProcessStatus();

    if (it->status == VMManagerSystem::ProcessStatus::Stopped) {
        it->started_at = 0;
    } else if (previous == VMManagerSystem::ProcessStatus::Stopped) {
        // Starting a machine shows it right away
        it->started_at = QDateTime::currentMSecsSinceEpoch();
        it->used_at    = clock.elapsed();
    } else if (isPaused(previous) && isRunning(it->status)) {
        // So does resuming it
        it->used_at = clock.elapsed();
    }

    update();
}

void
VMManagerDiscord::windowActivated(VMManagerSystem *system)
{
    auto it = tracked.find(system);
    if (it == tracked.end())
        return;

    if (system == shown) {
        // Switching back to the shown machine abandons any pending switch
        it->used_at = clock.elapsed();
        focused     = nullptr;
        dwell_timer.stop();
        return;
    }

    // Briefly passing through a machine's window shouldn't change the status
    focused = system;
    dwell_timer.start();
}

void
VMManagerDiscord::dwellElapsed()
{
    if (!focused)
        return;

    auto it = tracked.find(focused);
    focused = nullptr;
    if (it == tracked.end())
        return;

    it->used_at = clock.elapsed();
    update();
}

bool
VMManagerDiscord::isCandidate(const VMManagerSystem *system) const
{
    return (system->getProcessStatus() != VMManagerSystem::ProcessStatus::Stopped) && !system->isSettingsOnly();
}

void
VMManagerDiscord::update()
{
    if (!enabled) {
        shown = nullptr;
        client.setActivity({});
        return;
    }

    // Running machines win over paused ones, then the most recently used one
    auto best       = tracked.constEnd();
    int  candidates = 0;
    for (auto it = tracked.constBegin(); it != tracked.constEnd(); ++it) {
        if (!isCandidate(it.key()))
            continue;

        candidates++;
        if (best != tracked.constEnd()) {
            const bool running = isRunning(it->status);
            if (running != isRunning(best->status)) {
                if (!running)
                    continue;
            } else if (it->used_at <= best->used_at)
                continue;
        }
        best = it;
    }

    if (best == tracked.constEnd()) {
        shown = nullptr;
        client.setActivity({});
        return;
    }

    shown = best.key();
    client.setActivity(buildActivity(best.key(), candidates - 1));
}

QString
VMManagerDiscord::iconUrl(const QString &icon)
{
    if (!icon.startsWith(kIconPrefix))
        return {};

#ifdef EMU_GIT_HASH
    // Builds from a modified tree have a + appended to the hash
    auto ref = QString(EMU_GIT_HASH);
    ref.remove('+');
#else
    const auto ref = QStringLiteral("master");
#endif
    const auto file = QString::fromLatin1(QUrl::toPercentEncoding(icon.mid(kIconPrefix.size())));
    return QString("https://raw.githubusercontent.com/86Box/86Box/%1/src/qt/assets/systemicons/%2").arg(ref, file);
}

QJsonObject
VMManagerDiscord::buildActivity(VMManagerSystem *system, int others) const
{
    const auto &state = tracked[system];

    // Machine names are prefixed with their chipset in brackets
    auto machine = system->getDisplayValue(VMManager::Display::Name::Machine);
    if (machine.startsWith('[')) {
        const auto end = machine.indexOf("] ");
        if (end >= 0)
            machine = machine.mid(end + 2);
    }
    const auto cpu = system->getDisplayValue(VMManager::Display::Name::CPU);

    QString details;
    QString status;
    switch (privacy) {
        case Privacy::NameAndHardware:
            details = system->displayName;
            status  = machine.isEmpty() ? cpu : QString("%1 · %2").arg(machine, cpu);
            break;
        case Privacy::HardwareOnly:
            details = machine;
            status  = cpu;
            break;
        case Privacy::Minimal:
            details = tr("Using %1").arg(EMU_NAME);
            break;
    }

    if (others > 0) {
        if (privacy == Privacy::Minimal) {
            status = tr("%n machine(s) active", "", others + 1);
        } else {
            // Keep the count visible even when the rest has to be cut short
            const auto suffix = tr(" (+%n more)", "", others);
            details           = (fitText(details, kMaxTextLength - suffix.size()) + suffix).trimmed();
        }
    }

    QJsonObject activity;
    setText(activity, "details", details);
    setText(activity, "state", status);
    if (state.started_at)
        activity["timestamps"] = QJsonObject { { "start", state.started_at } };

    QJsonObject assets;
    auto        largeImage = (privacy != Privacy::Minimal) ? iconUrl(system->icon) : QString();
    if (largeImage.isEmpty()) {
#if defined(RELEASE_BUILD)
        largeImage = QStringLiteral("86box-green");
#elif defined(BETA_BUILD)
        largeImage = QStringLiteral("86box-yellow");
#elif defined(ALPHA_BUILD)
        largeImage = QStringLiteral("86box-red");
#else
        largeImage = QStringLiteral("86box");
#endif
    }
    assets["large_image"] = largeImage;
    assets["large_text"]  = QStringLiteral(EMU_NAME " " EMU_VERSION_FULL);
    if (isRunning(state.status)) {
        assets["small_image"] = QStringLiteral("status-running");
        assets["small_text"]  = tr("Running");
    } else if (isPaused(state.status)) {
        assets["small_image"] = QStringLiteral("status-paused");
        assets["small_text"]  = tr("Paused");
    }
    activity["assets"] = assets;

    return activity;
}
