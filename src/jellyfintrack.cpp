/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfintrack.h"

#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {
constexpr auto ArtworkTag = "ARTWORKURL";

QString basePath(const QUrl& server)
{
    QString path = server.path();
    while(path.endsWith(u'/')) {
        path.chop(1);
    }
    return path;
}

int effectivePort(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return url.port(scheme == "https"_L1 ? 443 : 80);
}

bool sameServer(const QUrl& server, const QUrl& url)
{
    return server.scheme().compare(url.scheme(), Qt::CaseInsensitive) == 0
        && server.host().compare(url.host(), Qt::CaseInsensitive) == 0 && effectivePort(server) == effectivePort(url);
}
} // namespace

namespace Fooyin::Jellyfin {
QString streamFormatKey(StreamFormat format)
{
    switch(format) {
        case StreamFormat::Mp3:
            return u"mp3"_s;
        case StreamFormat::Opus:
            return u"opus"_s;
        case StreamFormat::Aac:
            return u"aac"_s;
        case StreamFormat::Original:
            break;
    }
    return u"original"_s;
}

StreamFormat streamFormatFromKey(const QString& key)
{
    if(key == "mp3"_L1) {
        return StreamFormat::Mp3;
    }
    if(key == "opus"_L1) {
        return StreamFormat::Opus;
    }
    if(key == "aac"_L1) {
        return StreamFormat::Aac;
    }
    return StreamFormat::Original;
}

QUrl normaliseServerUrl(const QString& input)
{
    QString text = input.trimmed();
    if(text.isEmpty()) {
        return {};
    }
    if(!text.contains("://"_L1)) {
        text.prepend("http://"_L1);
    }

    QUrl url{text, QUrl::TolerantMode};
    if(!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    url.setQuery(QString{});
    url.setFragment(QString{});
    url.setUserInfo(QString{});

    QString path = url.path();
    static const QRegularExpression webSuffix{uR"((/web(/[^/]*\.html)?)?/*$)"_s, QRegularExpression::CaseInsensitiveOption};
    path.remove(webSuffix);
    url.setPath(path);

    return url;
}

QUrl apiUrl(const QUrl& server, const QString& path, const QUrlQuery& query)
{
    QUrl url{server};
    url.setPath(basePath(server) + (path.startsWith(u'/') ? path : u'/' + path));
    url.setQuery(query.isEmpty() ? QString{} : query.query(QUrl::FullyEncoded), QUrl::StrictMode);
    return url;
}

QString trackPath(const QUrl& server, const QString& itemId)
{
    return apiUrl(server, u"/Audio/%1/stream"_s.arg(itemId)).toString(QUrl::FullyEncoded);
}

QString itemIdFromTrackPath(const QUrl& server, const QString& path)
{
    if(server.isEmpty() || path.isEmpty()) {
        return {};
    }

    const QUrl url{path};
    if(!url.isValid() || !sameServer(server, url)) {
        return {};
    }

    static const QRegularExpression streamPath{uR"(^/Audio/([0-9a-fA-F-]{32,36})/stream$)"_s};

    QString relative = url.path();
    const QString base = basePath(server);
    if(!base.isEmpty()) {
        if(!relative.startsWith(base, Qt::CaseInsensitive)) {
            return {};
        }
        relative = relative.mid(base.size());
    }

    const auto match = streamPath.match(relative);
    return match.hasMatch() ? match.captured(1) : QString{};
}

QUrl resolveStreamUrl(const QUrl& server, const QString& itemId, const StreamOptions& options, const QString& token,
                      const QString& deviceId)
{
    QUrlQuery query;

    switch(options.format) {
        case StreamFormat::Original:
            query.addQueryItem(u"static"_s, u"true"_s);
            break;
        case StreamFormat::Mp3:
            query.addQueryItem(u"container"_s, u"mp3"_s);
            query.addQueryItem(u"audioCodec"_s, u"mp3"_s);
            break;
        case StreamFormat::Opus:
            query.addQueryItem(u"container"_s, u"ogg"_s);
            query.addQueryItem(u"audioCodec"_s, u"opus"_s);
            break;
        case StreamFormat::Aac:
            query.addQueryItem(u"container"_s, u"aac"_s);
            query.addQueryItem(u"audioCodec"_s, u"aac"_s);
            break;
    }

    if(options.format != StreamFormat::Original) {
        const int kbps = std::clamp(options.maxBitrateKbps, 32, 1536);
        query.addQueryItem(u"audioBitRate"_s, QString::number(kbps * 1000));
        query.addQueryItem(u"maxAudioChannels"_s, u"2"_s);
    }

    if(!deviceId.isEmpty()) {
        query.addQueryItem(u"deviceId"_s, deviceId);
    }
    if(!token.isEmpty()) {
        // Streams are opened by fooyin's own HTTP device, so the token can't go in a header.
        // Newer servers only accept "ApiKey" (legacy auth disabled), older ones only "api_key".
        query.addQueryItem(u"ApiKey"_s, token);
        query.addQueryItem(u"api_key"_s, token);
    }

    return apiUrl(server, u"/Audio/%1/stream"_s.arg(itemId), query);
}

QUrl imageUrl(const QUrl& server, const QString& itemId, const QString& tag, int maxSize)
{
    QUrlQuery query;
    if(maxSize > 0) {
        query.addQueryItem(u"maxWidth"_s, QString::number(maxSize));
        query.addQueryItem(u"maxHeight"_s, QString::number(maxSize));
    }
    query.addQueryItem(u"quality"_s, u"90"_s);
    if(!tag.isEmpty()) {
        // The tag changes whenever the image does, so it doubles as a cache key.
        query.addQueryItem(u"tag"_s, tag);
    }
    return apiUrl(server, u"/Items/%1/Images/Primary"_s.arg(itemId), query);
}

QUrl trackArtworkUrl(const QUrl& server, const JellyfinItem& item, int maxSize)
{
    if(!item.albumId.isEmpty() && !item.albumPrimaryImageTag.isEmpty()) {
        return imageUrl(server, item.albumId, item.albumPrimaryImageTag, maxSize);
    }
    if(!item.primaryImageTag.isEmpty()) {
        return imageUrl(server, item.id, item.primaryImageTag, maxSize);
    }
    return {};
}

QString displayCodec(const QString& codec)
{
    const QString lower = codec.toLower();
    if(lower.isEmpty()) {
        return {};
    }
    if(lower == "opus"_L1) {
        return u"Opus"_s;
    }
    if(lower == "vorbis"_L1) {
        return u"Vorbis"_s;
    }
    if(lower == "wavpack"_L1) {
        return u"WavPack"_s;
    }
    if(lower.startsWith("wma"_L1)) {
        return u"WMA"_s;
    }
    if(lower.startsWith("pcm"_L1)) {
        return u"PCM"_s;
    }
    if(lower == "dca"_L1) {
        return u"DTS"_s;
    }
    return codec.toUpper();
}

uint64_t parseJellyfinDate(const QString& date)
{
    if(date.isEmpty()) {
        return 0;
    }

    // Jellyfin writes 7 fractional digits ("2019-11-28T06:07:13.4661661Z"); Qt only reads up to 3.
    static const QRegularExpression fraction{uR"((\.\d{3})\d+)"_s};
    QString normalised{date};
    normalised.replace(fraction, u"\\1"_s);

    QDateTime dateTime = QDateTime::fromString(normalised, Qt::ISODateWithMs);
    if(!dateTime.isValid()) {
        dateTime = QDateTime::fromString(normalised, Qt::ISODate);
    }
    if(!dateTime.isValid() || dateTime.toMSecsSinceEpoch() <= 0) {
        return 0;
    }
    return static_cast<uint64_t>(dateTime.toMSecsSinceEpoch());
}

Track toTrack(const JellyfinItem& item, const QUrl& server, int artworkSize)
{
    Track track{trackPath(server, item.id)};

    track.setTitle(item.name);
    track.setArtists(item.artists);
    track.setAlbum(item.album);
    track.setAlbumArtists(item.albumArtists);
    track.setGenres(item.genres);
    track.setComposers(item.composers);

    if(item.indexNumber > 0) {
        track.setTrackNumber(QString::number(item.indexNumber));
    }
    if(item.parentIndexNumber > 0) {
        track.setDiscNumber(QString::number(item.parentIndexNumber));
    }

    if(item.premiereDate.size() >= 10) {
        track.setDate(item.premiereDate.left(10));
    }
    if(item.productionYear > 0) {
        track.setYear(item.productionYear);
    }

    if(!item.overview.isEmpty()) {
        track.setComment(item.overview);
    }

    track.setDuration(item.durationMs());
    if(item.bitrate > 0) {
        track.setBitrate(item.bitrate / 1000);
    }
    if(item.sampleRate > 0) {
        track.setSampleRate(item.sampleRate);
    }
    if(item.channels > 0) {
        track.setChannels(item.channels);
    }
    if(item.bitDepth > 0) {
        track.setBitDepth(item.bitDepth);
    }
    if(item.size > 0) {
        track.setFileSize(static_cast<uint64_t>(item.size));
    }
    if(const QString codec = displayCodec(item.codec.isEmpty() ? item.container : item.codec); !codec.isEmpty()) {
        track.setCodec(codec);
    }

    // Jellyfin's normalisation gain targets -18 LUFS, the ReplayGain 2.0 reference level.
    if(item.normalizationGain) {
        track.setRGTrackGain(*item.normalizationGain);
    }

    if(const uint64_t added = parseJellyfinDate(item.dateCreated); added > 0) {
        track.setAddedTime(added);
    }

    if(const QUrl artwork = trackArtworkUrl(server, item, artworkSize); artwork.isValid()) {
        track.replaceExtraTag(QString::fromLatin1(ArtworkTag), artwork.toString(QUrl::FullyEncoded));
    }

    track.setMetadataWasRead(true);
    track.generateHash();

    return track;
}
} // namespace Fooyin::Jellyfin
