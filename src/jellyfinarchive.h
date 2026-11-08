/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/engine/audioinput.h>

#include <memory>

class QNetworkAccessManager;

namespace Fooyin {
class AudioLoader;
class RemoteIoService;

namespace Jellyfin {
class JellyfinSession;

/*!
 * Serves Jellyfin tracks as entries of a per-server "<serverId>.jellyfin" archive file.
 *
 * The archive file only records how to reach the server. Each entry ("<itemId>.<ext>") is opened
 * as an HTTP range stream of the original file (or a transcode), which fooyin's decoders read like
 * a local file. Because fooyin doesn't treat archive tracks as remote streams, gapless playback,
 * next-track preloading and the waveform seekbar work for them.
 */
class JellyfinArchiveReader : public ArchiveReader
{
public:
    JellyfinArchiveReader(std::shared_ptr<RemoteIoService> io, std::shared_ptr<QNetworkAccessManager> network,
                          std::shared_ptr<JellyfinSession> session);

    [[nodiscard]] QStringList extensions() const override;
    [[nodiscard]] QString type() const override;

    bool init(const QString& file) override;
    ArchiveEntryData entry(const QString& file) override;
    bool readTracks(ReadEntryCallback readEntry, const StopRequestedCallback& stopRequested) override;
    QByteArray readCover(const Track& track, Track::Cover cover) override;

private:
    [[nodiscard]] QUrl serverUrl() const;
    [[nodiscard]] QString token() const;

    std::shared_ptr<RemoteIoService> m_io;
    std::shared_ptr<QNetworkAccessManager> m_network;
    std::shared_ptr<JellyfinSession> m_session;

    QString m_serverId;
    QUrl m_fileServerUrl;
};

void registerArchiveReader(AudioLoader& loader, const std::shared_ptr<RemoteIoService>& io,
                           const std::shared_ptr<QNetworkAccessManager>& network,
                           const std::shared_ptr<JellyfinSession>& session);
} // namespace Jellyfin
} // namespace Fooyin
