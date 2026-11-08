/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/player/playerdefs.h>
#include <core/track.h>

#include <QJsonObject>
#include <QObject>

class QTimer;

namespace Fooyin {
class PlayerController;
class SettingsManager;

namespace Jellyfin {
class JellyfinClient;

/*!
 * Mirrors fooyin playback of Jellyfin tracks to the server's session API, so the server shows what
 * is playing and records play counts / "last played" the same way its own clients do.
 */
class JellyfinPlaybackReporter : public QObject
{
    Q_OBJECT

public:
    JellyfinPlaybackReporter(JellyfinClient* client, PlayerController* player, SettingsManager* settings,
                             QObject* parent = nullptr);
    ~JellyfinPlaybackReporter() override;

    //! Reports the current item as stopped (e.g. on shutdown).
    void finish();

private:
    void trackChanged(const Track& track);
    void playStateChanged(Player::PlayState state);
    void reportProgress(const QString& event);
    void reportStopped();
    [[nodiscard]] QJsonObject baseReport() const;
    [[nodiscard]] bool enabled() const;

    JellyfinClient* m_client;
    PlayerController* m_player;
    SettingsManager* m_settings;
    QTimer* m_progressTimer;

    QString m_itemId;
    QString m_playSessionId;
    uint64_t m_positionMs{0};
    uint64_t m_durationMs{0};
    bool m_paused{false};
    bool m_played{false};
};
} // namespace Jellyfin
} // namespace Fooyin
