/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/track.h>

#include <QObject>

class QTimer;

namespace Fooyin {
class PlaylistHandler;
class SettingsManager;

namespace Jellyfin {
class JellyfinClient;

/*!
 * Mirrors the server's audio playlists as locked (read-only) fooyin playlists named
 * "Jellyfin: <name>". One-way: changes are only ever pulled from the server. Only playlists this
 * class created are updated or removed, so a user playlist with the same name is left alone.
 */
class JellyfinPlaylistSync : public QObject
{
    Q_OBJECT

public:
    static constexpr auto NamePrefix = "Jellyfin: ";

    JellyfinPlaylistSync(JellyfinClient* client, PlaylistHandler* handler, SettingsManager* settings,
                         QObject* parent = nullptr);

    //! Starts a sync now (or right after the one in progress).
    void sync();
    [[nodiscard]] bool isRunning() const;

Q_SIGNALS:
    void finished(int playlistCount, const QString& error);

private:
    struct Mirror
    {
        QString name;
        TrackList tracks;
        bool fetched{false};
    };

    void apply(const std::vector<Mirror>& mirrors);
    void finish(int count, const QString& error);
    [[nodiscard]] bool enabled() const;

    JellyfinClient* m_client;
    PlaylistHandler* m_handler;
    SettingsManager* m_settings;
    QTimer* m_timer;

    bool m_ready{false};
    bool m_running{false};
    bool m_rerun{false};
};
} // namespace Jellyfin
} // namespace Fooyin
