/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

class QJsonObject;

namespace Fooyin::Jellyfin {
enum class ItemType : uint8_t
{
    Unknown = 0,
    Audio,
    MusicAlbum,
    MusicArtist,
    MusicGenre,
    Playlist,
    CollectionFolder,
    UserView,
    Folder,
};

//! The subset of a Jellyfin BaseItemDto that the plugin uses.
struct JellyfinItem
{
    QString id;
    QString name;
    QString sortName;
    ItemType type{ItemType::Unknown};
    QString collectionType;
    QString parentId;

    // Audio / album metadata
    QString album;
    QString albumId;
    QString albumArtist;
    QStringList artists;
    QStringList albumArtists;
    QStringList genres;
    QStringList composers;
    QString overview;
    int indexNumber{-1};
    int parentIndexNumber{-1};
    int productionYear{0};
    QString premiereDate;
    QString dateCreated;
    qint64 runTimeTicks{0};
    int childCount{-1};
    std::optional<float> normalizationGain;

    // Media information (first media source / audio stream)
    QString container;
    QString codec;
    int bitrate{0}; //!< bits per second
    int sampleRate{0};
    int channels{0};
    int bitDepth{0};
    qint64 size{-1};

    // Images
    QString primaryImageTag;
    QString albumPrimaryImageTag;

    // User data
    bool isFavorite{false};
    int playCount{0};

    //! Entry id inside a playlist (only set for playlist item listings).
    QString playlistItemId;

    [[nodiscard]] bool isValid() const
    {
        return !id.isEmpty();
    }

    //! Duration in milliseconds.
    [[nodiscard]] uint64_t durationMs() const
    {
        return runTimeTicks > 0 ? static_cast<uint64_t>(runTimeTicks / 10000) : 0;
    }

    static JellyfinItem fromJson(const QJsonObject& object);
};

using JellyfinItems = std::vector<JellyfinItem>;

struct ItemPage
{
    JellyfinItems items;
    int totalRecordCount{0};
    int startIndex{0};

    static ItemPage fromJson(const QJsonObject& object);
};

[[nodiscard]] ItemType itemTypeFromString(const QString& type);
} // namespace Fooyin::Jellyfin
