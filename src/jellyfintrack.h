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

//! Returns the item id when @p path is a track path on @p server, otherwise an empty string.
[[nodiscard]] QString itemIdFromTrackPath(const QUrl& server, const QString& path);

//! The URL actually requested for playback, including authentication and stream options.
[[nodiscard]] QUrl resolveStreamUrl(const QUrl& server, const QString& itemId, const StreamOptions& options,
                                    const QString& token, const QString& deviceId);

//! Primary image of an item. Jellyfin serves images without authentication.
[[nodiscard]] QUrl imageUrl(const QUrl& server, const QString& itemId, const QString& tag, int maxSize);

//! Image URL used for a track's artwork (the album cover, falling back to the track's own image).
[[nodiscard]] QUrl trackArtworkUrl(const QUrl& server, const JellyfinItem& item, int maxSize);

//! Converts an Audio item into a fooyin track whose metadata needs no further reading.
[[nodiscard]] Track toTrack(const JellyfinItem& item, const QUrl& server, int artworkSize);

//! Fooyin-style codec name ("FLAC", "MP3", "Opus", ...) for a Jellyfin/FFmpeg codec id.
[[nodiscard]] QString displayCodec(const QString& codec);

//! Milliseconds since epoch for a Jellyfin ISO-8601 timestamp, or 0 when it cannot be parsed.
[[nodiscard]] uint64_t parseJellyfinDate(const QString& date);
} // namespace Fooyin::Jellyfin
