/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinplaybackreporter.h"

#include "jellyfinclient.h"
#include "jellyfinsettings.h"

#include <core/player/playercontroller.h>
#include <utils/settings/settingsmanager.h>

#include <QJsonObject>
#include <QTimer>
#include <QUuid>

using namespace Qt::StringLiterals;

namespace {
constexpr int ProgressIntervalMs = 10000;

qint64 ticks(uint64_t ms)
{
    return static_cast<qint64>(ms) * 10000;
}
} // namespace

namespace Fooyin::Jellyfin {
JellyfinPlaybackReporter::JellyfinPlaybackReporter(JellyfinClient* client, PlayerController* player,
                                                   SettingsManager* settings, QObject* parent)
    : QObject{parent}
    , m_client{client}
    , m_player{player}
    , m_settings{settings}
    , m_progressTimer{new QTimer(this)}
{
    m_progressTimer->setInterval(ProgressIntervalMs);
    QObject::connect(m_progressTimer, &QTimer::timeout, this, [this]() { reportProgress(u"TimeUpdate"_s); });

    QObject::connect(m_player, &PlayerController::currentTrackChanged, this, &JellyfinPlaybackReporter::trackChanged);
    QObject::connect(m_player, &PlayerController::playStateChanged, this,
                     [this](Player::PlayState state, Player::PlayState /*previous*/) { playStateChanged(state); });
    QObject::connect(m_player, &PlayerController::positionChanged, this, [this](uint64_t ms) {
        m_positionMs = ms;
        // fooyin doesn't emit trackPlayed() for remote tracks, so watch the threshold directly.
        if(!m_itemId.isEmpty() && !m_played && m_player->playedThresholdReached()) {
            m_played = true;
        }
    });
    QObject::connect(m_player, &PlayerController::positionMoved, this, [this](uint64_t ms) {
        m_positionMs = ms;
        reportProgress(u"TimeUpdate"_s);
    });
}

JellyfinPlaybackReporter::~JellyfinPlaybackReporter() = default;

void JellyfinPlaybackReporter::finish()
{
    reportStopped();
}

bool JellyfinPlaybackReporter::enabled() const
{
    return m_settings->value(QLatin1StringView{Settings::ReportPlayback}).toBool()
        && m_client->session()->isSignedIn();
}

QJsonObject JellyfinPlaybackReporter::baseReport() const
{
    QJsonObject report;
    report.insert("ItemId"_L1, m_itemId);
    report.insert("PlaySessionId"_L1, m_playSessionId);
    report.insert("PositionTicks"_L1, ticks(m_positionMs));
    report.insert("IsPaused"_L1, m_paused);
    report.insert("CanSeek"_L1, m_player->currentTrackSeekable());
    report.insert("PlayMethod"_L1, m_client->session()->streamOptions().format == StreamFormat::Original
                                       ? u"DirectPlay"_s
                                       : u"Transcode"_s);
    return report;
}

void JellyfinPlaybackReporter::trackChanged(const Track& track)
{
    reportStopped();

    m_itemId = m_client->session()->itemIdForPath(track.filepath());
    if(m_itemId.isEmpty() || !enabled()) {
        m_itemId.clear();
        return;
    }

    m_playSessionId = QUuid::createUuid().toString(QUuid::Id128);
    m_positionMs    = 0;
    m_durationMs    = track.duration();
    m_played        = false;
    m_paused        = m_player->playState() == Player::PlayState::Paused;

    qCDebug(FOOFIN) << "Reporting playback start of" << m_itemId;
    m_client->reportPlayback(u"/Sessions/Playing"_s, baseReport());
    m_progressTimer->start();
}

void JellyfinPlaybackReporter::playStateChanged(Player::PlayState state)
{
    if(m_itemId.isEmpty()) {
        return;
    }

    switch(state) {
        case Player::PlayState::Paused:
            m_paused = true;
            m_progressTimer->stop();
            reportProgress(u"Pause"_s);
            break;
        case Player::PlayState::Playing:
            if(m_paused) {
                m_paused = false;
                reportProgress(u"Unpause"_s);
            }
            m_progressTimer->start();
            break;
        case Player::PlayState::Stopped:
            reportStopped();
            break;
    }
}

void JellyfinPlaybackReporter::reportProgress(const QString& event)
{
    if(m_itemId.isEmpty() || !enabled()) {
        return;
    }

    QJsonObject report = baseReport();
    report.insert("EventName"_L1, event);
    m_client->reportPlayback(u"/Sessions/Playing/Progress"_s, report);
}

void JellyfinPlaybackReporter::reportStopped()
{
    m_progressTimer->stop();
    if(m_itemId.isEmpty()) {
        return;
    }

    if(enabled()) {
        // The server decides from the final position whether this counts as a play (it needs ~90%).
        // Once fooyin's own played threshold is reached, report the end so both agree; this also
        // covers the position having already been reset when the next track takes over.
        QJsonObject report = baseReport();
        if(m_played && m_durationMs > m_positionMs) {
            report.insert("PositionTicks"_L1, ticks(m_durationMs));
        }
        qCDebug(FOOFIN) << "Reporting playback stop of" << m_itemId << "at" << m_positionMs << "ms, played"
                        << m_played;
        m_client->reportPlayback(u"/Sessions/Playing/Stopped"_s, report);
    }

    m_itemId.clear();
    m_playSessionId.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    m_paused     = false;
    m_played     = false;
}
} // namespace Fooyin::Jellyfin
