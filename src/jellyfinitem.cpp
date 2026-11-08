/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinitem.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace {
QStringList stringList(const QJsonValue& value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for(const QJsonValue& entry : array) {
        const QString text = entry.toString().trimmed();
        if(!text.isEmpty()) {
            list.push_back(text);
        }
    }
    return list;
}

//! Names from an array of NameIdPair / NameGuidPair objects.
QStringList namesList(const QJsonValue& value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for(const QJsonValue& entry : array) {
        const QString text = entry.toObject().value("Name"_L1).toString().trimmed();
        if(!text.isEmpty()) {
            list.push_back(text);
        }
    }
    return list;
}

int intValue(const QJsonObject& object, QLatin1StringView key, int fallback)
{
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toInt(fallback) : fallback;
}
} // namespace

namespace Fooyin::Jellyfin {
ItemType itemTypeFromString(const QString& type)
{
    if(type == "Audio"_L1) {
        return ItemType::Audio;
    }
    if(type == "MusicAlbum"_L1) {
        return ItemType::MusicAlbum;
    }
    if(type == "MusicArtist"_L1) {
        return ItemType::MusicArtist;
    }
    if(type == "MusicGenre"_L1 || type == "Genre"_L1) {
        return ItemType::MusicGenre;
    }
    if(type == "Playlist"_L1) {
        return ItemType::Playlist;
    }
    if(type == "CollectionFolder"_L1) {
        return ItemType::CollectionFolder;
    }
    if(type == "UserView"_L1) {
        return ItemType::UserView;
    }
    if(type == "Folder"_L1) {
        return ItemType::Folder;
    }
    return ItemType::Unknown;
}

JellyfinItem JellyfinItem::fromJson(const QJsonObject& object)
{
    JellyfinItem item;

    item.id             = object.value("Id"_L1).toString();
    item.name           = object.value("Name"_L1).toString();
    item.sortName       = object.value("SortName"_L1).toString();
    item.type           = itemTypeFromString(object.value("Type"_L1).toString());
    item.collectionType = object.value("CollectionType"_L1).toString();
    item.parentId       = object.value("ParentId"_L1).toString();

    item.album       = object.value("Album"_L1).toString();
    item.albumId     = object.value("AlbumId"_L1).toString();
    item.albumArtist = object.value("AlbumArtist"_L1).toString();
    item.artists     = stringList(object.value("Artists"_L1));
    if(item.artists.empty()) {
        item.artists = namesList(object.value("ArtistItems"_L1));
    }
    item.albumArtists = namesList(object.value("AlbumArtists"_L1));
    if(item.albumArtists.empty() && !item.albumArtist.isEmpty()) {
        item.albumArtists = {item.albumArtist};
    }
    item.genres = stringList(object.value("Genres"_L1));

    const QJsonArray people = object.value("People"_L1).toArray();
    for(const QJsonValue& person : people) {
        const QJsonObject personObject = person.toObject();
        if(personObject.value("Type"_L1).toString() == "Composer"_L1) {
            item.composers.push_back(personObject.value("Name"_L1).toString());
        }
    }

    item.overview          = object.value("Overview"_L1).toString();
    item.indexNumber       = intValue(object, "IndexNumber"_L1, -1);
    item.parentIndexNumber = intValue(object, "ParentIndexNumber"_L1, -1);
    item.productionYear    = intValue(object, "ProductionYear"_L1, 0);
    item.premiereDate      = object.value("PremiereDate"_L1).toString();
    item.dateCreated       = object.value("DateCreated"_L1).toString();
    item.runTimeTicks      = static_cast<qint64>(object.value("RunTimeTicks"_L1).toDouble());
    item.childCount        = intValue(object, "ChildCount"_L1, -1);
    if(const QJsonValue gain = object.value("NormalizationGain"_L1); gain.isDouble()) {
        item.normalizationGain = static_cast<float>(gain.toDouble());
    }

    item.container = object.value("Container"_L1).toString();

    const QJsonArray sources = object.value("MediaSources"_L1).toArray();
    if(!sources.isEmpty()) {
        const QJsonObject source = sources.first().toObject();
        if(item.container.isEmpty()) {
            item.container = source.value("Container"_L1).toString();
        }
        item.size    = static_cast<qint64>(source.value("Size"_L1).toDouble(-1));
        item.bitrate = intValue(source, "Bitrate"_L1, 0);

        const QJsonArray streams = source.value("MediaStreams"_L1).toArray();
        for(const QJsonValue& streamValue : streams) {
            const QJsonObject stream = streamValue.toObject();
            if(stream.value("Type"_L1).toString() != "Audio"_L1) {
                continue;
            }
            item.codec      = stream.value("Codec"_L1).toString();
            item.sampleRate = intValue(stream, "SampleRate"_L1, 0);
            item.channels   = intValue(stream, "Channels"_L1, 0);
            item.bitDepth   = intValue(stream, "BitDepth"_L1, 0);
            if(item.bitrate <= 0) {
                item.bitrate = intValue(stream, "BitRate"_L1, 0);
            }
            break;
        }
    }

    item.primaryImageTag      = object.value("ImageTags"_L1).toObject().value("Primary"_L1).toString();
    item.albumPrimaryImageTag = object.value("AlbumPrimaryImageTag"_L1).toString();

    const QJsonObject userData = object.value("UserData"_L1).toObject();
    item.isFavorite            = userData.value("IsFavorite"_L1).toBool();
    item.playCount             = intValue(userData, "PlayCount"_L1, 0);

    item.playlistItemId = object.value("PlaylistItemId"_L1).toString();

    return item;
}

ItemPage ItemPage::fromJson(const QJsonObject& object)
{
    ItemPage page;

    const QJsonArray items = object.value("Items"_L1).toArray();
    page.items.reserve(items.size());
    for(const QJsonValue& value : items) {
        JellyfinItem item = JellyfinItem::fromJson(value.toObject());
        if(item.isValid()) {
            page.items.push_back(std::move(item));
        }
    }

    page.totalRecordCount = intValue(object, "TotalRecordCount"_L1, static_cast<int>(page.items.size()));
    page.startIndex       = intValue(object, "StartIndex"_L1, 0);

    return page;
}
} // namespace Fooyin::Jellyfin
