/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinarchive.h"

#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <core/engine/audioloader.h>
#include <core/network/remoteioservice.h>
#include <core/network/remotestreamdevice.h>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QThread>

using namespace Qt::StringLiterals;

namespace {
constexpr int OpenTimeoutMs  = 15000;
constexpr int StallTimeoutMs = 15000;
constexpr int MaxCoverBytes  = 32 * 1024 * 1024;

/*!
 * Presents fooyin's HTTP stream device as an ordinary blocking file.
 *
 * Archive entries are read by decoders that expect file semantics: a read returns data or fails.
 * The underlying device is put in non-blocking mode (each read waits briefly) so that a stalled
 * connection turns into a read error after a while instead of blocking playback forever.
 */
class StreamEntryDevice : public QIODevice
{
public:
    explicit StreamEntryDevice(Fooyin::RemoteStreamSource source)
        : m_source{std::move(source)}
    { }

    ~StreamEntryDevice() override
    {
        StreamEntryDevice::close();
    }

    bool open(OpenMode mode) override
    {
        if(!m_source.device || (mode & WriteOnly)) {
            setErrorString(u"Invalid Jellyfin stream"_s);
            return false;
        }
        if(!m_source.device->isOpen() && !m_source.device->open(QIODevice::ReadOnly)) {
            setErrorString(m_source.device->errorString());
            return false;
        }

        // The response headers decide whether the stream is seekable; wait for them. (Waiting is
        // impossible on the network thread itself, where the wait returns immediately.)
        m_source.device->waitForReadyRead(OpenTimeoutMs);

        if(m_source.remoteDevice) {
            m_source.remoteDevice->setNonBlockingReadsEnabled(true);
        }
        return QIODevice::open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    void close() override
    {
        if(m_source.device) {
            m_source.device->close();
        }
        QIODevice::close();
    }

    [[nodiscard]] bool isSequential() const override
    {
        return !m_source.device || m_source.device->isSequential();
    }

    [[nodiscard]] qint64 size() const override
    {
        return m_source.device ? std::max<qint64>(0, m_source.device->size()) : 0;
    }

    bool seek(qint64 pos) override
    {
        if(!m_source.device || !m_source.device->seek(pos)) {
            return false;
        }
        return QIODevice::seek(pos);
    }

    [[nodiscard]] bool atEnd() const override
    {
        return !m_source.device || m_source.device->atEnd();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        QElapsedTimer stalled;
        stalled.start();

        while(true) {
            const qint64 read = m_source.device->read(data, maxSize);
            if(read >= 0) {
                return read;
            }
            if(!m_source.remoteDevice || !m_source.remoteDevice->readWouldBlock()) {
                setErrorString(m_source.device->errorString());
                return -1;
            }
            if(stalled.elapsed() > StallTimeoutMs) {
                setErrorString(u"The Jellyfin server stopped sending data"_s);
                return -1;
            }
        }
    }

    qint64 writeData(const char* /*data*/, qint64 /*maxSize*/) override
    {
        return -1;
    }

private:
    Fooyin::RemoteStreamSource m_source;
};
} // namespace

namespace Fooyin::Jellyfin {
JellyfinArchiveReader::JellyfinArchiveReader(std::shared_ptr<RemoteIoService> io,
                                             std::shared_ptr<QNetworkAccessManager> network,
                                             std::shared_ptr<JellyfinSession> session)
    : m_io{std::move(io)}
    , m_network{std::move(network)}
    , m_session{std::move(session)}
{ }

QStringList JellyfinArchiveReader::extensions() const
{
    return {QString::fromLatin1(ArchiveExtension)};
}

QString JellyfinArchiveReader::type() const
{
    return QString::fromLatin1(ArchiveExtension);
}

bool JellyfinArchiveReader::init(const QString& file)
{
    QFile archive{file};
    if(!archive.open(QIODevice::ReadOnly)) {
        qCWarning(FOOFIN) << "Can't open" << file << "- sign in to the Jellyfin server again";
        return false;
    }

    const QJsonObject object = QJsonDocument::fromJson(archive.readAll()).object();
    m_serverId               = object.value("ServerId"_L1).toString();
    m_fileServerUrl          = QUrl{object.value("ServerUrl"_L1).toString()};
    return !m_serverId.isEmpty() && m_fileServerUrl.isValid();
}

QUrl JellyfinArchiveReader::serverUrl() const
{
    // Prefer the signed-in address: it's the most recent one for this server.
    const Credentials credentials = m_session->credentials();
    return credentials.serverId == m_serverId && credentials.server.isValid() ? credentials.server : m_fileServerUrl;
}

QString JellyfinArchiveReader::token() const
{
    const Credentials credentials = m_session->credentials();
    return credentials.serverId == m_serverId ? credentials.token : QString{};
}

ArchiveEntryData JellyfinArchiveReader::entry(const QString& file)
{
    const QString itemId = itemIdFromEntry(file);
    if(itemId.isEmpty()) {
        return {};
    }

    const QString accessToken = token();
    if(accessToken.isEmpty()) {
        qCWarning(FOOFIN) << "Not signed in to the Jellyfin server" << m_serverId << "- the stream may be refused";
    }

    const QUrl url = resolveStreamUrl(serverUrl(), itemId, m_session->streamOptions(), accessToken,
                                      m_session->deviceId());
    qCDebug(FOOFIN) << "Opening Jellyfin entry" << itemId << "format"
                    << streamFormatKey(m_session->streamOptions().format);

    auto device = std::make_unique<StreamEntryDevice>(m_io->createStreamSource(url));
    if(!device->open(QIODevice::ReadOnly)) {
        qCWarning(FOOFIN) << "Failed to open Jellyfin stream" << itemId << device->errorString();
        return {};
    }

    ArchiveEntryData data;
    data.info.path          = file;
    data.info.size          = static_cast<uint64_t>(device->size());
    data.info.isRegularFile = true;
    data.device             = std::move(device);
    return data;
}

bool JellyfinArchiveReader::readTracks(ReadEntryCallback /*readEntry*/, const StopRequestedCallback& /*stopRequested*/)
{
    // Tracks are added from the Jellyfin browser, not by scanning the "archive".
    return false;
}

QByteArray JellyfinArchiveReader::readCover(const Track& track, Track::Cover cover)
{
    if(cover != Track::Cover::Front) {
        return {};
    }
    if(m_network && QThread::currentThread() == m_network->thread()) {
        qCDebug(FOOFIN) << "Skipping cover download on the network thread for" << track.pathInArchive();
        return {}; // can't wait for a download on the thread that performs it
    }
    qCDebug(FOOFIN) << "Downloading cover for" << track.pathInArchive();

    QUrl url;
    if(const QStringList artwork = track.extraTag(u"ARTWORKURL"_s); !artwork.isEmpty()) {
        url = QUrl{artwork.front()};
    }
    if(!url.isValid()) {
        const QString itemId = itemIdFromEntry(track.pathInArchive());
        if(itemId.isEmpty()) {
            return {};
        }
        url = imageUrl(serverUrl(), itemId, {}, m_session->artworkSize());
    }

    StreamEntryDevice device{m_io->createStreamSource(url)};
    if(!device.open(QIODevice::ReadOnly)) {
        return {};
    }

    QByteArray data;
    while(data.size() < MaxCoverBytes) {
        const QByteArray chunk = device.read(256 * 1024);
        if(chunk.isEmpty()) {
            break;
        }
        data += chunk;
    }
    return data;
}

void registerArchiveReader(AudioLoader& loader, const std::shared_ptr<RemoteIoService>& io,
                           const std::shared_ptr<QNetworkAccessManager>& network,
                           const std::shared_ptr<JellyfinSession>& session)
{
    loader.addArchiveReader(
        u"Jellyfin"_s, [io, network, session]() { return std::make_unique<JellyfinArchiveReader>(io, network, session); });
}
} // namespace Fooyin::Jellyfin
