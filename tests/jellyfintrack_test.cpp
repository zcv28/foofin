/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinitem.h"
#include "jellyfintrack.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace Fooyin;
using namespace Fooyin::Jellyfin;

namespace {
// Trimmed from a real /Items response (Jellyfin demo server).
constexpr auto AudioJson = R"({
  "Name": "Bend", "Id": "a794b7c125d436df8dc2d870ad7b7e1e", "Type": "Audio",
  "DateCreated": "2019-11-28T06:07:13.4661661Z", "Container": "mp3",
  "PremiereDate": "2006-01-01T00:00:00.0000000Z", "ProductionYear": 2006,
  "IndexNumber": 1, "ParentIndexNumber": 2, "RunTimeTicks": 2675461220,
  "MediaSources": [{ "Container": "mp3", "Size": 6429605, "Bitrate": 192254,
    "MediaStreams": [{ "Codec": "mp3", "BitRate": 192000, "Channels": 2, "SampleRate": 44100, "Type": "Audio" }] }],
  "Genres": ["Electronic"],
  "UserData": { "PlayCount": 36, "IsFavorite": true },
  "Artists": ["Binaerpilot"],
  "ArtistItems": [{ "Name": "Binaerpilot", "Id": "9ebcc113abb3ee375dbf1d68c9576ed5" }],
  "Album": "You Can\u0027t Stop Da Funk", "AlbumId": "f21ea9ece4e6e26ec26ebfb81b82ad78",
  "AlbumPrimaryImageTag": "95b159065964e1210d8c39d255920ebc", "AlbumArtist": "Binaerpilot",
  "AlbumArtists": [{ "Name": "Binaerpilot", "Id": "9ebcc113abb3ee375dbf1d68c9576ed5" }],
  "ImageTags": {}, "NormalizationGain": -6.6000004
})";
} // namespace

class JellyfinTrackTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalisesServerUrls_data();
    void normalisesServerUrls();
    void trackPathRoundTrip();
    void rejectsForeignPaths();
    void buildsStreamUrls();
    void parsesItems();
    void convertsToTrack();
    void parsesDates();
    void parsesPages();
    void archivePathRoundTrip();
    void picksEntryExtensions();
};

void JellyfinTrackTest::normalisesServerUrls_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("bare host") << u"192.168.1.10:8096"_s << u"http://192.168.1.10:8096"_s;
    QTest::newRow("trailing slash") << u"https://jf.example.com/"_s << u"https://jf.example.com"_s;
    QTest::newRow("sub path") << u"https://example.com/jellyfin/"_s << u"https://example.com/jellyfin"_s;
    QTest::newRow("web client") << u"https://example.com/web/#/home.html"_s << u"https://example.com"_s;
    QTest::newRow("web index") << u"https://example.com/jf/web/index.html"_s << u"https://example.com/jf"_s;
    QTest::newRow("whitespace") << u"  http://nas:8096  "_s << u"http://nas:8096"_s;
}

void JellyfinTrackTest::normalisesServerUrls()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(normaliseServerUrl(input).toString(), expected);
}

void JellyfinTrackTest::trackPathRoundTrip()
{
    const QUrl server{u"https://example.com/jellyfin"_s};
    const QString id   = u"a794b7c125d436df8dc2d870ad7b7e1e"_s;
    const QString path = trackPath(server, id);

    QCOMPARE(path, u"https://example.com/jellyfin/Audio/a794b7c125d436df8dc2d870ad7b7e1e/stream"_s);
    QVERIFY(Track::isRemotePath(path));
    QCOMPARE(itemIdFromTrackPath(server, path), id);

    // Same server spelled with an explicit default port.
    QCOMPARE(itemIdFromTrackPath(QUrl{u"https://EXAMPLE.com:443/jellyfin"_s}, path), id);
}

void JellyfinTrackTest::rejectsForeignPaths()
{
    const QUrl server{u"http://nas:8096"_s};
    const QString id = u"a794b7c125d436df8dc2d870ad7b7e1e"_s;

    QVERIFY(itemIdFromTrackPath(server, u"http://other:8096/Audio/%1/stream"_s.arg(id)).isEmpty());
    QVERIFY(itemIdFromTrackPath(server, u"http://nas:8097/Audio/%1/stream"_s.arg(id)).isEmpty());
    QVERIFY(itemIdFromTrackPath(server, u"http://nas:8096/Audio/%1/universal"_s.arg(id)).isEmpty());
    QVERIFY(itemIdFromTrackPath(server, u"http://nas:8096/stream.mp3"_s).isEmpty());
    QVERIFY(itemIdFromTrackPath(server, u"/music/song.flac"_s).isEmpty());
    QVERIFY(itemIdFromTrackPath(QUrl{}, u"http://nas:8096/Audio/%1/stream"_s.arg(id)).isEmpty());
}

void JellyfinTrackTest::buildsStreamUrls()
{
    const QUrl server{u"https://example.com/jf"_s};

    const QUrl original = resolveStreamUrl(server, u"abc"_s, {}, u"tok"_s, u"dev"_s);
    const QUrlQuery originalQuery{original};
    QCOMPARE(original.path(), u"/jf/Audio/abc/stream"_s);
    QCOMPARE(originalQuery.queryItemValue(u"static"_s), u"true"_s);
    QCOMPARE(originalQuery.queryItemValue(u"ApiKey"_s), u"tok"_s);
    QCOMPARE(originalQuery.queryItemValue(u"api_key"_s), u"tok"_s);
    QVERIFY(!originalQuery.hasQueryItem(u"audioCodec"_s));

    const QUrl opus = resolveStreamUrl(server, u"abc"_s, {.format = StreamFormat::Opus, .maxBitrateKbps = 128},
                                       u"tok"_s, u"dev"_s);
    const QUrlQuery opusQuery{opus};
    QVERIFY(!opusQuery.hasQueryItem(u"static"_s));
    QCOMPARE(opusQuery.queryItemValue(u"audioCodec"_s), u"opus"_s);
    QCOMPARE(opusQuery.queryItemValue(u"container"_s), u"ogg"_s);
    QCOMPARE(opusQuery.queryItemValue(u"audioBitRate"_s), u"128000"_s);
    QCOMPARE(opusQuery.queryItemValue(u"deviceId"_s), u"dev"_s);

    QCOMPARE(streamFormatFromKey(streamFormatKey(StreamFormat::Aac)), StreamFormat::Aac);
    QCOMPARE(streamFormatFromKey(u"bogus"_s), StreamFormat::Original);
}

void JellyfinTrackTest::parsesItems()
{
    const auto item = JellyfinItem::fromJson(QJsonDocument::fromJson(AudioJson).object());

    QCOMPARE(item.type, ItemType::Audio);
    QCOMPARE(item.name, u"Bend"_s);
    QCOMPARE(item.artists, QStringList{u"Binaerpilot"_s});
    QCOMPARE(item.albumArtists, QStringList{u"Binaerpilot"_s});
    QCOMPARE(item.album, u"You Can't Stop Da Funk"_s);
    QCOMPARE(item.indexNumber, 1);
    QCOMPARE(item.parentIndexNumber, 2);
    QCOMPARE(item.durationMs(), uint64_t{267546});
    QCOMPARE(item.codec, u"mp3"_s);
    QCOMPARE(item.bitrate, 192254);
    QCOMPARE(item.sampleRate, 44100);
    QCOMPARE(item.channels, 2);
    QCOMPARE(item.size, qint64{6429605});
    QVERIFY(item.isFavorite);
    QCOMPARE(item.playCount, 36);
    QVERIFY(item.normalizationGain.has_value());
}

void JellyfinTrackTest::convertsToTrack()
{
    const QUrl server{u"http://nas:8096"_s};
    const auto item  = JellyfinItem::fromJson(QJsonDocument::fromJson(AudioJson).object());
    const Track track = toTrack(item, server, {}, 600);

    QVERIFY(track.isRemote());
    QCOMPARE(track.filepath(), u"http://nas:8096/Audio/a794b7c125d436df8dc2d870ad7b7e1e/stream"_s);
    QCOMPARE(track.title(), u"Bend"_s);
    QCOMPARE(track.artists(), QStringList{u"Binaerpilot"_s});
    QCOMPARE(track.album(), u"You Can't Stop Da Funk"_s);
    QCOMPARE(track.trackNumber(), u"1"_s);
    QCOMPARE(track.discNumber(), u"2"_s);
    QCOMPARE(track.year(), 2006);
    QCOMPARE(track.duration(), uint64_t{267546});
    QCOMPARE(track.bitrate(), 192);
    QCOMPARE(track.sampleRate(), 44100);
    QCOMPARE(track.codec(), u"MP3"_s);
    QVERIFY(track.hasTrackGain());
    QVERIFY(track.metadataWasRead());
    QVERIFY(!track.hash().isEmpty());

    const QStringList artwork = track.extraTag(u"ARTWORKURL"_s);
    QCOMPARE(artwork.size(), 1);
    QVERIFY(artwork.front().startsWith(u"http://nas:8096/Items/f21ea9ece4e6e26ec26ebfb81b82ad78/Images/Primary"_s));
    QVERIFY(artwork.front().contains(u"tag=95b159065964e1210d8c39d255920ebc"_s));
    // No credentials anywhere in what fooyin persists.
    QVERIFY(!track.filepath().contains(u"api"_s, Qt::CaseInsensitive));
}

void JellyfinTrackTest::parsesDates()
{
    QCOMPARE(parseJellyfinDate(u"1970-01-01T00:00:01.0000000Z"_s), uint64_t{1000});
    QCOMPARE(parseJellyfinDate(u"2019-11-28T06:07:13.4661661Z"_s), uint64_t{1574921233466});
    QCOMPARE(parseJellyfinDate(u"2019-11-28T06:07:13Z"_s), uint64_t{1574921233000});
    QCOMPARE(parseJellyfinDate(u"garbage"_s), uint64_t{0});
    QCOMPARE(parseJellyfinDate({}), uint64_t{0});
}

void JellyfinTrackTest::parsesPages()
{
    const QByteArray json = R"({"Items":[{"Name":"A","Id":"1","Type":"MusicAlbum","ProductionYear":1999},
                                          {"Name":"no id"},
                                          {"Name":"B","Id":"2","Type":"MusicArtist"}],
                                 "TotalRecordCount":42,"StartIndex":10})";
    const ItemPage page = ItemPage::fromJson(QJsonDocument::fromJson(json).object());

    QCOMPARE(page.items.size(), size_t{2});
    QCOMPARE(page.totalRecordCount, 42);
    QCOMPARE(page.startIndex, 10);
    QCOMPARE(page.items.at(0).type, ItemType::MusicAlbum);
    QCOMPARE(page.items.at(0).productionYear, 1999);
    QCOMPARE(page.items.at(1).type, ItemType::MusicArtist);
}

void JellyfinTrackTest::archivePathRoundTrip()
{
    const QString archive = u"/home/me/.local/share/fooyin/foofin/f0b3381645f04afb9a0e392e74b6a1b0.jellyfin"_s;
    const QString itemId  = u"a794b7c125d436df8dc2d870ad7b7e1e"_s;
    const QString path    = archiveTrackPath(archive, itemId, u"flac"_s);

    const Track track{path};
    QVERIFY(track.isInArchive());
    QVERIFY(!track.isRemote()); // what makes gapless playback and the waveform seekbar work
    QCOMPARE(track.archivePath(), archive);
    QCOMPARE(track.pathInArchive(), itemId + u".flac"_s);

    const ArchiveTrackRef ref = archiveTrackRef(path);
    QCOMPARE(ref.itemId, itemId);
    QCOMPARE(ref.serverId, u"f0b3381645f04afb9a0e392e74b6a1b0"_s);

    // Windows paths and other archives
    const QString windows = archiveTrackPath(u"C:/Users/me/AppData/Local/fooyin/foofin/abc.jellyfin"_s, itemId, u"mp3"_s);
    QCOMPARE(archiveTrackRef(windows).serverId, u"abc"_s);
    QCOMPARE(Track{windows}.archivePath(), u"C:/Users/me/AppData/Local/fooyin/foofin/abc.jellyfin"_s);
    QVERIFY(archiveTrackRef(u"unpack://zip|8|file:///a/b.zip!song.flac"_s).itemId.isEmpty());
    QVERIFY(archiveTrackRef(u"/music/song.flac"_s).itemId.isEmpty());

    const auto item   = JellyfinItem::fromJson(QJsonDocument::fromJson(AudioJson).object());
    const Track fromItem = toTrack(item, QUrl{u"http://nas:8096"_s}, archive, 600);
    QVERIFY(fromItem.isInArchive());
    QCOMPARE(fromItem.pathInArchive(), itemId + u".mp3"_s);
    QCOMPARE(fromItem.title(), u"Bend"_s);
}

void JellyfinTrackTest::picksEntryExtensions()
{
    QCOMPARE(entryExtension(u"flac"_s), u"flac"_s);
    QCOMPARE(entryExtension(u"mov,mp4,m4a,3gp,3g2,mj2"_s), u"m4a"_s);
    QCOMPARE(entryExtension(u"MP3"_s), u"mp3"_s);
    QCOMPARE(entryExtension(u"ogg"_s), u"ogg"_s);
    QCOMPARE(entryExtension({}), u"mka"_s);
    QCOMPARE(entryExtension(u"something-new"_s), u"mka"_s);
    QCOMPARE(itemIdFromEntry(u"a794b7c125d436df8dc2d870ad7b7e1e.flac"_s), u"a794b7c125d436df8dc2d870ad7b7e1e"_s);
    QVERIFY(itemIdFromEntry(u"cover.jpg"_s).isEmpty());
}

QTEST_GUILESS_MAIN(JellyfinTrackTest)
#include "jellyfintrack_test.moc"
