/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "jellyfinitem.h"

#include <core/track.h>

#include <QUrl>
#include <QUrlQuery>

namespace Fooyin::Jellyfin {
enum class StreamFormat : uint8_t
{
    Original = 0, //!< The untouched file; seekable via HTTP range requests.
    Mp3,
    Opus,
    Aac,
};

[[nodiscard]] QString streamFormatKey(StreamFormat format);
[[nodiscard]] StreamFormat streamFormatFromKey(const QString& key);

struct StreamOptions
{
    StreamFormat format{StreamFormat::Original};
    int maxBitrateKbps{320};
};

/*!
 * Normalises a user-entered server address: adds a missing scheme, drops query/fragment, trailing
 * slashes and a pasted web-client suffix ("/web", "/web/index.html").
 */
[[nodiscard]] QUrl normaliseServerUrl(const QString& input);

//! Builds an API URL below the server base (which may contain a sub-path such as "/jellyfin").
[[nodiscard]] QUrl apiUrl(const QUrl& server, const QString& path, const QUrlQuery& query = {});

/*!
 * The persistent path stored in fooyin playlists for a Jellyfin track.
 *
 * It deliberately carries no credentials or transcoding options: those are added when the stream
 * is opened (see resolveStreamUrl), so signing in again or changing the stream format never
 * invalidates saved playlists, and no access token ends up in fooyin's playlist database.
 */
[[nodiscard]] QString trackPath(const QUrl& server, const QString& itemId);

//! Returns the item id when @p path is a (legacy) http track path on @p server, otherwise an empty string.
[[nodiscard]] QString itemIdFromTrackPath(const QUrl& server, const QString& path);

/*!
 * Jellyfin tracks are stored as entries of a small per-server "archive" file
 * ("unpack://jellyfin|<n>|file://<dir>/<serverId>.jellyfin!<itemId>.<ext>"), which the plugin's
 * archive reader turns into an HTTP stream. fooyin treats archive tracks like local files, so
 * gapless playback, next-track preloading and the waveform seekbar all work, which they don't for
 * plain http(s) URLs.
 */
constexpr auto ArchiveExtension = "jellyfin";

[[nodiscard]] QString archiveTrackPath(const QString& archiveFile, const QString& itemId, const QString& container);
//! Item id from an archive entry name ("<itemId>.<ext>"), or empty.
[[nodiscard]] QString itemIdFromEntry(const QString& entry);
//! Item id and server id of a Jellyfin archive track path; both empty if @p path isn't one.
struct ArchiveTrackRef
{
    QString serverId;
    QString itemId;
};
[[nodiscard]] ArchiveTrackRef archiveTrackRef(const QString& path);
//! File extension for an entry, from a Jellyfin container string ("flac", "mov,mp4,m4a,...").
[[nodiscard]] QString entryExtension(const QString& container);

//! The URL actually requested for playback, including authentication and stream options.
[[nodiscard]] QUrl resolveStreamUrl(const QUrl& server, const QString& itemId, const StreamOptions& options,
                                    const QString& token, const QString& deviceId);

//! True for a Jellyfin audio stream URL that carries an access token (see resolveStreamUrl()).
[[nodiscard]] bool isAuthenticatedStreamUrl(const QUrl& url);

//! Primary image of an item. Jellyfin serves images without authentication.
[[nodiscard]] QUrl imageUrl(const QUrl& server, const QString& itemId, const QString& tag, int maxSize);

//! Image URL used for a track's artwork (the album cover, falling back to the track's own image).
[[nodiscard]] QUrl trackArtworkUrl(const QUrl& server, const JellyfinItem& item, int maxSize);

/*!
 * Converts an Audio item into a fooyin track whose metadata needs no further reading.
 * With an @p archiveFile the track gets an archive path, otherwise a plain http stream path.
 */
[[nodiscard]] Track toTrack(const JellyfinItem& item, const QUrl& server, const QString& archiveFile,
                            int artworkSize);

//! Fooyin-style codec name ("FLAC", "MP3", "Opus", ...) for a Jellyfin/FFmpeg codec id.
[[nodiscard]] QString displayCodec(const QString& codec);

//! Milliseconds since epoch for a Jellyfin ISO-8601 timestamp, or 0 when it cannot be parsed.
[[nodiscard]] uint64_t parseJellyfinDate(const QString& date);
} // namespace Fooyin::Jellyfin
