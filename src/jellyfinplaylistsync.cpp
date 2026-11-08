/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinplaylistsync.h"

#include "jellyfinclient.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <core/playlist/playlist.h>
#include <core/playlist/playlisthandler.h>
#include <utils/settings/settingsmanager.h>

#include <QTimer>

#include <ranges>

using namespace Qt::StringLiterals;

namespace {
constexpr auto SyncIntervalMs     = 30 * 60 * 1000;
constexpr auto StartupFallbackMs  = 15 * 1000;

bool sameTracks(const Fooyin::TrackList& a, const Fooyin::TrackList& b)
{
    return std::ranges::equal(a, b, [](const Fooyin::Track& x, const Fooyin::Track& y) {
        return x.filepath() == y.filepath();
    });
}
} // namespace

namespace Fooyin::Jellyfin {
JellyfinPlaylistSync::JellyfinPlaylistSync(JellyfinClient* client, PlaylistHandler* handler, SettingsManager* settings,
                                           QObject* parent)
    : QObject{parent}
    , m_client{client}
    , m_handler{handler}
    , m_settings{settings}
    , m_timer{new QTimer(this)}
{
    m_timer->setInterval(SyncIntervalMs);
    QObject::connect(m_timer, &QTimer::timeout, this, &JellyfinPlaylistSync::sync);
    m_timer->start();

    // Don't touch playlists before fooyin has loaded its own, or names would be duplicated.
    const auto becomeReady = [this]() {
        if(!m_ready) {
            m_ready = true;
            sync();
        }
    };
    QObject::connect(m_handler, &PlaylistHandler::playlistsPopulated, this, becomeReady);
    QTimer::singleShot(StartupFallbackMs, this, [this, becomeReady]() {
        if(m_handler->playlistCount() > 0) {
            becomeReady();
        }
    });

    QObject::connect(m_client->session().get(), &JellyfinSession::signedInChanged, this, [this](bool signedIn) {
        if(signedIn) {
            sync();
        }
    });
    m_settings->subscribe(QLatin1StringView{Settings::MirrorPlaylists}, this, [this]() {
        if(enabled()) {
            sync();
        }
    });
}

bool JellyfinPlaylistSync::isRunning() const
{
    return m_running;
}

bool JellyfinPlaylistSync::enabled() const
{
    return m_settings->value(QLatin1StringView{Settings::MirrorPlaylists}).toBool()
        && m_client->session()->isSignedIn();
}

void JellyfinPlaylistSync::sync()
{
    if(!m_ready || !enabled()) {
        return;
    }
    if(m_running) {
        m_rerun = true;
        return;
    }
    m_running = true;

    QUrlQuery query;
    query.addQueryItem(u"includeItemTypes"_s, u"Playlist"_s);
    query.addQueryItem(u"recursive"_s, u"true"_s);
    query.addQueryItem(u"mediaTypes"_s, u"Audio"_s);
    query.addQueryItem(u"sortBy"_s, u"SortName"_s);

    m_client->allItems(u"/Items"_s, query, -1, this, [this](const ItemPage& page, const QString& error) {
        if(!error.isEmpty()) {
            finish(0, error);
            return;
        }

        auto mirrors   = std::make_shared<std::vector<Mirror>>(page.items.size());
        auto remaining = std::make_shared<size_t>(page.items.size());
        if(page.items.empty()) {
            apply(*mirrors);
            return;
        }

        for(size_t i{0}; i < page.items.size(); ++i) {
            const JellyfinItem& playlist = page.items.at(i);
            mirrors->at(i).name          = playlist.name;

            m_client->allItems(
                u"/Playlists/%1/Items"_s.arg(playlist.id), {}, -1, this,
                [this, mirrors, remaining, i](const ItemPage& items, const QString& itemsError) {
                    Mirror& mirror = mirrors->at(i);
                    mirror.fetched = itemsError.isEmpty();

                    const auto session    = m_client->session();
                    const QUrl server     = session->credentials().server;
                    const QString archive = session->archiveFile();
                    const int artworkSize = session->artworkSize();
                    for(const JellyfinItem& item : items.items) {
                        if(item.type == ItemType::Audio) {
                            mirror.tracks.push_back(toTrack(item, server, archive, artworkSize));
                        }
                    }

                    if(--*remaining == 0) {
                        apply(*mirrors);
                    }
                });
        }
    });
}

void JellyfinPlaylistSync::apply(const std::vector<Mirror>& mirrors)
{
    const QStringList owned = m_settings->value(QLatin1StringView{Settings::MirroredPlaylists}).toStringList();
    QStringList current;

    for(const Mirror& mirror : mirrors) {
        // Jellyfin allows duplicate names; fooyin identifies playlists by name.
        QString name = QString::fromLatin1(NamePrefix) + mirror.name;
        for(int n{2}; current.contains(name); ++n) {
            name = u"%1%2 (%3)"_s.arg(QString::fromLatin1(NamePrefix), mirror.name).arg(n);
        }
        current.push_back(name);

        if(!mirror.fetched) {
            continue; // keep whatever we had rather than emptying it
        }

        Playlist* playlist = m_handler->playlistByName(name);
        if(!playlist) {
            playlist = m_handler->createPlaylist(name, mirror.tracks);
            if(playlist) {
                m_handler->setPlaylistLocked(playlist->id(), true);
            }
            continue;
        }

        // Ours if recorded as such, or locked with our prefix (the record lives in fooyin's settings,
        // which aren't written if fooyin doesn't exit cleanly).
        if(!owned.contains(name) && !playlist->isLocked()) {
            qCInfo(FOOFIN) << "Not mirroring into" << name << "- a playlist with that name already exists";
            current.removeLast();
            continue;
        }

        if(!sameTracks(playlist->tracks(), mirror.tracks)) {
            m_handler->setPlaylistLocked(playlist->id(), false);
            m_handler->replacePlaylistTracks(playlist->id(), mirror.tracks);
        }
        m_handler->setPlaylistLocked(playlist->id(), true);
    }

    // Playlists deleted on the server
    for(const QString& name : owned) {
        if(!current.contains(name)) {
            if(Playlist* playlist = m_handler->playlistByName(name)) {
                m_handler->removePlaylist(playlist->id());
            }
        }
    }

    m_settings->set(QLatin1StringView{Settings::MirroredPlaylists}, current);
    finish(static_cast<int>(mirrors.size()), {});
}

void JellyfinPlaylistSync::finish(int count, const QString& error)
{
    m_running = false;
    if(error.isEmpty()) {
        qCDebug(FOOFIN) << "Mirrored" << count << "Jellyfin playlists";
    }
    else {
        qCWarning(FOOFIN) << "Playlist sync failed:" << error;
    }
    Q_EMIT finished(count, error);

    if(std::exchange(m_rerun, false)) {
        sync();
    }
}
} // namespace Fooyin::Jellyfin
