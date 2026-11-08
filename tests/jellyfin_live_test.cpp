/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Exercises the plugin against a real Jellyfin server. Skipped unless configured:
 *
 *   FOOFIN_TEST_SERVER=https://demo.jellyfin.org/stable FOOFIN_TEST_USER=demo ./foofin_live_test
 *
 * Optional: FOOFIN_TEST_PASSWORD, FOOFIN_TEST_SCREENSHOT=/path/to/browser.png
 * The test only reads from the server (plus a zero-length playback session report).
 */

#include "jellyfinarchive.h"
#include "jellyfinbrowsermodel.h"
#include "jellyfinclient.h"
#include "jellyfinimagecache.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfinsourceprovider.h"
#include "jellyfintrack.h"

#include <core/network/networkaccessmanager.h>
#include <core/network/remoteioservice.h>
#include <utils/settings/settingsmanager.h>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QImage>
#include <QHeaderView>
#include <QIODevice>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeView>

#include <future>

using namespace Qt::StringLiterals;
using namespace Fooyin;
using namespace Fooyin::Jellyfin;

namespace {
constexpr int NetworkTimeoutMs = 20000;

QModelIndex childNamed(const QAbstractItemModel& model, const QModelIndex& parent, const QString& prefix)
{
    for(int row{0}; row < model.rowCount(parent); ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        if(index.data().toString().startsWith(prefix)) {
            return index;
        }
    }
    return {};
}

//! Expands @p index and waits for its children (or a status row) to arrive.
bool loadChildren(JellyfinBrowserModel& model, const QModelIndex& index)
{
    if(model.canFetchMore(index)) {
        model.fetchMore(index);
    }
    return QTest::qWaitFor(
        [&]() {
            return model.rowCount(index) > 0
                && model.kind(model.index(0, 0, index)) != NodeKind::Status;
        },
        NetworkTimeoutMs);
}
} // namespace

class JellyfinLiveTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void rejectsWrongPassword();
    void signsIn();
    void listsMusicLibraries();
    void browsesLibrary();
    void searches();
    void opensSeekableStream();
    void opensArchiveEntry();
    void reportsPlayback();
    void cleanupTestCase();

private:
    QString m_server;
    QString m_user;
    QString m_password;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::shared_ptr<NetworkAccessManager> m_network;
    std::shared_ptr<JellyfinSession> m_session;
    std::unique_ptr<JellyfinClient> m_client;
    std::unique_ptr<JellyfinImageCache> m_images;
    TrackList m_albumTracks;
};

void JellyfinLiveTest::initTestCase()
{
    m_server   = qEnvironmentVariable("FOOFIN_TEST_SERVER");
    m_user     = qEnvironmentVariable("FOOFIN_TEST_USER");
    m_password = qEnvironmentVariable("FOOFIN_TEST_PASSWORD");
    if(m_server.isEmpty() || m_user.isEmpty()) {
        QSKIP("Set FOOFIN_TEST_SERVER and FOOFIN_TEST_USER to run the live tests");
    }

    m_dir = std::make_unique<QTemporaryDir>();
    // The session writes its per-server archive file below the data directory.
    qputenv("XDG_DATA_HOME", m_dir->filePath(u"data"_s).toLocal8Bit());
    m_settings = std::make_unique<SettingsManager>(m_dir->filePath(u"fooyin.conf"_s));
    registerSettings(m_settings.get());

    m_network = std::make_shared<NetworkAccessManager>(m_settings.get());
    m_session = std::make_shared<JellyfinSession>(m_settings.get(), nullptr);
    m_client  = std::make_unique<JellyfinClient>(m_network, m_session);
    m_images  = std::make_unique<JellyfinImageCache>(m_network);
}

void JellyfinLiveTest::rejectsWrongPassword()
{
    bool done{false};
    QString error;
    m_client->signIn(m_server, m_user, m_password + u"-definitely-wrong"_s, this,
                     [&](const SignInResult&, const QString& signInError) {
                         error = signInError;
                         done  = true;
                     });
    QTRY_VERIFY_WITH_TIMEOUT(done, NetworkTimeoutMs);
    QCOMPARE(error, u"Incorrect username or password."_s);
    QVERIFY(!m_session->isSignedIn());
}

void JellyfinLiveTest::signsIn()
{
    bool done{false};
    SignInResult result;
    QString error;
    m_client->signIn(m_server, m_user, m_password, this, [&](const SignInResult& signInResult, const QString& e) {
        result = signInResult;
        error  = e;
        done   = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, NetworkTimeoutMs);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!result.token.isEmpty());
    QVERIFY(!result.userId.isEmpty());

    QSignalSpy signedIn{m_session.get(), &JellyfinSession::signedInChanged};
    m_session->signIn(result);
    QCOMPARE(signedIn.count(), 1);
    QVERIFY(m_session->isSignedIn());
    QCOMPARE(m_settings->value(QLatin1StringView{Jellyfin::Settings::UserId}).toString(), result.userId);
    qInfo() << "Signed in to" << result.serverName << result.server.toString();
}

void JellyfinLiveTest::listsMusicLibraries()
{
    bool done{false};
    JellyfinItems libraries;
    m_client->musicLibraries(this, [&](const JellyfinItems& views, const QString& error) {
        QVERIFY2(error.isEmpty(), qPrintable(error));
        libraries = views;
        done      = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, NetworkTimeoutMs);
    QVERIFY(!libraries.empty());
    qInfo() << "Music libraries:" << libraries.size() << libraries.front().name;
}

void JellyfinLiveTest::browsesLibrary()
{
    JellyfinBrowserModel model{m_client.get(), m_images.get(), m_settings.get()};
    const QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::QtTest};

    model.reload();
    QTRY_VERIFY_WITH_TIMEOUT(model.rowCount({}) > 1, NetworkTimeoutMs);

    // Albums -> first album -> tracks
    const QModelIndex albums = childNamed(model, {}, u"Albums"_s);
    QVERIFY(albums.isValid());
    QVERIFY(loadChildren(model, albums));
    const QModelIndex album = model.index(0, 0, albums);
    QCOMPARE(model.kind(album), NodeKind::Album);
    QVERIFY(loadChildren(model, album));
    QCOMPARE(model.kind(model.index(0, 0, album)), NodeKind::Track);
    qInfo() << "First album:" << album.data().toString() << "with" << model.rowCount(album) << "tracks";

    bool resolved{false};
    model.resolveTracks({album}, this, [&](const TrackList& tracks) {
        m_albumTracks = tracks;
        resolved      = true;
    });
    QVERIFY(resolved); // loaded children resolve without another request
    QCOMPARE(static_cast<int>(m_albumTracks.size()), model.rowCount(album));
    for(const Track& track : m_albumTracks) {
        QVERIFY(track.isInArchive()); // archive paths: gapless + waveform support
        QVERIFY(!track.isRemote());
        QVERIFY(!m_session->itemIdForPath(track.filepath()).isEmpty());
        QVERIFY(!track.title().isEmpty());
        QVERIFY(track.duration() > 0);
    }
    QVERIFY(model.cachedTracks({album}).has_value());

    // Artists -> first artist; resolving an artist fetches all of their tracks in one go
    const QModelIndex artists = childNamed(model, {}, u"Artists"_s);
    QVERIFY(loadChildren(model, artists));
    const QModelIndex artist = model.index(0, 0, artists);
    QCOMPARE(model.kind(artist), NodeKind::Artist);
    QVERIFY(loadChildren(model, artist));

    resolved = false;
    TrackList artistTracks;
    model.resolveTracks({artist}, this, [&](const TrackList& tracks) {
        artistTracks = tracks;
        resolved     = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(resolved, NetworkTimeoutMs);
    QVERIFY(!artistTracks.empty());
    qInfo() << "Artist" << artist.data().toString() << "has" << artistTracks.size() << "tracks";

    // A shelf of albums resolves album by album
    const QModelIndex recent = childNamed(model, {}, u"Recently Added"_s);
    resolved = false;
    TrackList recentTracks;
    model.resolveTracks({recent}, this, [&](const TrackList& tracks) {
        recentTracks = tracks;
        resolved     = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(resolved, NetworkTimeoutMs);
    QVERIFY(!recentTracks.empty());

    // Selecting a parent and its child must not add the child twice.
    resolved = false;
    TrackList deduplicated;
    model.resolveTracks({model.index(0, 0, album), album}, this, [&](const TrackList& tracks) {
        deduplicated = tracks;
        resolved     = true;
    });
    QTRY_VERIFY(resolved);
    QCOMPARE(deduplicated.size(), m_albumTracks.size());

    for(const QString& name : {u"Genres"_s, u"Playlists"_s, u"Random Tracks"_s}) {
        const QModelIndex category = childNamed(model, {}, name);
        QVERIFY2(loadChildren(model, category), qPrintable(name));
    }

    if(const QString screenshot = qEnvironmentVariable("FOOFIN_TEST_SCREENSHOT"); !screenshot.isEmpty()) {
        QTreeView view;
        view.setModel(&model);
        view.setHeaderHidden(true);
        view.setIconSize({24, 24});
        view.header()->setStretchLastSection(false);
        view.header()->setSectionResizeMode(0, QHeaderView::Stretch);
        view.header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        view.resize(460, 620);
        view.expand(albums);
        view.expand(album);
        view.expand(childNamed(model, {}, u"Playlists"_s));
        view.show();
        QTest::qWait(3000); // let thumbnails arrive
        QVERIFY(view.grab().save(screenshot));
    }
}

void JellyfinLiveTest::searches()
{
    JellyfinBrowserModel model{m_client.get(), m_images.get(), m_settings.get()};
    const QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::QtTest};
    model.reload();
    QTRY_VERIFY_WITH_TIMEOUT(model.rowCount({}) > 1, NetworkTimeoutMs);

    QVERIFY(!m_albumTracks.empty());
    const QString term = m_albumTracks.front().album().section(u' ', 0, 0);

    QSignalSpy loaded{&model, &JellyfinBrowserModel::loaded};
    model.setSearch(term);
    QVERIFY(loaded.wait(NetworkTimeoutMs));

    const QModelIndex albums = childNamed(model, {}, u"Albums ("_s);
    QVERIFY2(albums.isValid(), "search groups should be labelled with their result counts");
    QVERIFY(model.rowCount(albums) > 0);
    QCOMPARE(model.kind(model.index(0, 0, albums)), NodeKind::Album);
    QCOMPARE(model.autoExpandIndexes().size(), 3);
    qInfo() << "Search" << term << "->" << childNamed(model, {}, u"Artists ("_s).data().toString()
            << albums.data().toString() << childNamed(model, {}, u"Tracks ("_s).data().toString();
}

void JellyfinLiveTest::opensSeekableStream()
{
    QVERIFY(!m_albumTracks.empty());
    const Track& track = m_albumTracks.front();

    const JellyfinSourceProvider provider{std::make_shared<RemoteIoService>(m_network, m_settings.get()), m_session};
    // Track paths used to be plain http stream URLs; those still go through the source provider.
    const QString legacyPath = trackPath(m_session->credentials().server, m_session->itemIdForPath(track.filepath()));
    RemoteStreamSource source = provider.createStreamSource(QUrl{legacyPath});
    QVERIFY(source.device);
    QVERIFY(source.remoteDevice);
    QVERIFY(source.device->open(QIODevice::ReadOnly));

    // Once the response headers arrive, a range-capable original stream reports its size.
    QTRY_VERIFY_WITH_TIMEOUT(source.device->size() > 0, NetworkTimeoutMs);
    QVERIFY2(!source.device->isSequential(), "the original-file stream should be seekable");
    if(track.fileSize() > 0) {
        QCOMPARE(static_cast<uint64_t>(source.device->size()), track.fileSize());
    }

    // Like fooyin's decoder, read from a worker thread: the device refuses reads on the network
    // (here: main) thread, which has to keep running its event loop to deliver the data.
    const qint64 middle = source.device->size() / 2;
    struct Reads
    {
        QByteArray head;
        QByteArray tail;
        bool seeked{false};
    };
    QIODevice* device = source.device.get();
    auto reader       = std::async(std::launch::async, [device, middle]() {
        const auto readSome = [device]() {
            QByteArray bytes;
            while(bytes.size() < 4096) {
                const QByteArray chunk = device->read(4096 - bytes.size());
                if(chunk.isEmpty()) {
                    break;
                }
                bytes += chunk;
            }
            return bytes;
        };
        Reads reads;
        reads.head   = readSome();
        reads.seeked = device->seek(middle);
        reads.tail   = readSome();
        return reads;
    });

    QTRY_VERIFY_WITH_TIMEOUT(reader.wait_for(std::chrono::seconds{0}) == std::future_status::ready, NetworkTimeoutMs);
    const Reads reads = reader.get();
    QCOMPARE(reads.head.size(), 4096);
    QVERIFY(reads.seeked);
    QCOMPARE(reads.tail.size(), 4096);
    QVERIFY(reads.head != reads.tail);
    qInfo() << "Stream of" << track.title() << "is" << source.device->size() << "bytes; read, seeked to" << middle
            << "and read again";

    source.device->close();
}

void JellyfinLiveTest::opensArchiveEntry()
{
    QVERIFY(!m_albumTracks.empty());
    const Track track = m_albumTracks.front();
    QVERIFY(QFile::exists(track.archivePath()));

    JellyfinArchiveReader reader{std::make_shared<RemoteIoService>(m_network, m_settings.get()), m_network, m_session};
    QVERIFY(reader.init(track.archivePath()));

    // Decoders and cover loading run off the network (main) thread; so does this.
    struct Result
    {
        qint64 size{0};
        bool sequential{true};
        QByteArray head;
        QByteArray middle;
        QByteArray cover;
    };
    auto worker = std::async(std::launch::async, [&reader, &track]() {
        Result result;
        ArchiveEntryData entry = reader.entry(track.pathInArchive());
        if(!entry.device) {
            return result;
        }
        result.size       = entry.device->size();
        result.sequential = entry.device->isSequential();
        result.head       = entry.device->read(4096);
        if(entry.device->seek(result.size / 2)) {
            result.middle = entry.device->read(4096);
        }
        result.cover = reader.readCover(track, Track::Cover::Front);
        return result;
    });

    QTRY_VERIFY_WITH_TIMEOUT(worker.wait_for(std::chrono::seconds{0}) == std::future_status::ready, NetworkTimeoutMs);
    const Result result = worker.get();
    QVERIFY(result.size > 0);
    QVERIFY2(!result.sequential, "archive entries must be seekable");
    QCOMPARE(result.head.size(), 4096);
    QCOMPARE(result.middle.size(), 4096);
    QVERIFY(result.head != result.middle);
    QVERIFY2(!QImage::fromData(result.cover).isNull(), "readCover should return the album image");
    qInfo() << "Archive entry" << track.pathInArchive() << "is" << result.size << "bytes; cover"
            << result.cover.size() << "bytes";
}

void JellyfinLiveTest::reportsPlayback()
{
    QVERIFY(!m_albumTracks.empty());
    const QString itemId = m_session->itemIdForPath(m_albumTracks.front().filepath());

    QJsonObject report;
    report.insert("ItemId"_L1, itemId);
    report.insert("PositionTicks"_L1, 0);
    report.insert("PlayMethod"_L1, u"DirectPlay"_s);

    for(const QString& endpoint : {u"/Sessions/Playing"_s, u"/Sessions/Playing/Stopped"_s}) {
        bool done{false};
        QString error;
        m_client->post(endpoint, {}, QJsonDocument{report}, this, [&](const QJsonDocument&, const QString& e) {
            error = e;
            done  = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, NetworkTimeoutMs);
        QVERIFY2(error.isEmpty(), qPrintable(endpoint + u": "_s + error));
    }
}

void JellyfinLiveTest::cleanupTestCase()
{
    if(m_client && m_session && m_session->isSignedIn()) {
        m_client->signOut();
        QTest::qWait(500); // let the logout request go out
    }
}

QTEST_MAIN(JellyfinLiveTest)
#include "jellyfin_live_test.moc"
