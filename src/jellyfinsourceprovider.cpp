/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinsourceprovider.h"

#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <core/engine/audioloader.h>
#include <core/network/networkaccessmanager.h>
#include <core/network/remoteioservice.h>

#include <QAbstractNetworkCache>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

namespace Fooyin::Jellyfin {
JellyfinSourceProvider::JellyfinSourceProvider(std::shared_ptr<RemoteIoService> fallback,
                                               std::shared_ptr<JellyfinSession> session)
    : m_fallback{std::move(fallback)}
    , m_session{std::move(session)}
{ }

RemoteStreamSource JellyfinSourceProvider::createStreamSource(const QUrl& url) const
{
    // Called from the engine thread; the session only hands out locked snapshots.
    const Credentials credentials = m_session->credentials();
    const QString itemId          = itemIdFromTrackPath(credentials.server, url.toString(QUrl::FullyEncoded));

    if(itemId.isEmpty()) {
        return m_fallback->createStreamSource(url);
    }

    if(!credentials.isValid()) {
        qCWarning(FOOFIN) << "Playing a Jellyfin track while signed out; the server may refuse the stream";
    }

    const StreamOptions options = m_session->streamOptions();
    const QUrl streamUrl = resolveStreamUrl(credentials.server, itemId, options, credentials.token, credentials.deviceId);
    qCDebug(FOOFIN) << "Opening Jellyfin stream" << itemId << "format" << streamFormatKey(options.format);

    return m_fallback->createStreamSource(streamUrl);
}

void scrubStreamCache(QNetworkAccessManager* network, QObject* context)
{
    QObject::connect(network, &QNetworkAccessManager::finished, context, [network](QNetworkReply* reply) {
        const QUrl url = reply->url();
        if(!isAuthenticatedStreamUrl(url)) {
            return;
        }

        const auto remove = [network, url]() {
            if(auto* cache = network->cache(); cache && cache->remove(url)) {
                qCDebug(FOOFIN) << "Removed a Jellyfin stream from fooyin's network cache";
            }
        };
        remove();
        // The cache entry may only be committed after this signal; check again once it has been.
        QTimer::singleShot(0, network, remove);
    });
}

void installSourceProvider(AudioLoader& loader, const std::shared_ptr<RemoteIoService>& io,
                           const std::shared_ptr<JellyfinSession>& session)
{
    // fooyin has a single provider slot and no way to retrieve the current one, so wrap a fresh
    // instance of its default implementation (@p io) for everything that isn't ours.
    loader.setRemoteSourceProvider(std::make_shared<JellyfinSourceProvider>(io, session));
}
} // namespace Fooyin::Jellyfin
