/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinimagecache.h"

#include <core/network/networkaccessmanager.h>
#include <core/network/networkutils.h>

#include <QImage>
#include <QNetworkReply>

namespace {
constexpr int MaxConcurrent   = 4;
constexpr int CacheCostLimitK = 32 * 1024; // ~32 MiB of decoded pixels
} // namespace

namespace Fooyin::Jellyfin {
JellyfinImageCache::JellyfinImageCache(std::shared_ptr<NetworkAccessManager> network, QObject* parent)
    : QObject{parent}
    , m_network{std::move(network)}
    , m_cache{CacheCostLimitK}
{ }

QPixmap JellyfinImageCache::image(const QUrl& url)
{
    if(!url.isValid()) {
        return {};
    }

    const QString key = url.toString();
    if(const QPixmap* pixmap = m_cache.object(key)) {
        return *pixmap;
    }
    if(m_failed.contains(key) || m_pending.contains(key)) {
        return {};
    }

    m_pending.insert(key);
    m_queue.push_back(url);
    startNext();
    return {};
}

void JellyfinImageCache::clear()
{
    m_cache.clear();
    m_failed.clear();
    m_queue.clear();
    m_pending.clear();
}

void JellyfinImageCache::startNext()
{
    while(m_active < MaxConcurrent && !m_queue.empty()) {
        // Most recently requested first: those are the rows the user is looking at now.
        const QUrl url = m_queue.back();
        m_queue.pop_back();

        QNetworkRequest request = makeNetworkRequest(url);
        request.setTransferTimeout(20000);

        ++m_active;
        auto* reply = m_network->get(request);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
            reply->deleteLater();
            --m_active;

            const QString key = url.toString();
            if(!m_pending.remove(key)) {
                // Cleared while in flight.
                startNext();
                return;
            }

            QImage image;
            if(reply->error() == QNetworkReply::NoError) {
                image.loadFromData(reply->readAll());
            }

            if(image.isNull()) {
                m_failed.insert(key);
            }
            else {
                auto* pixmap   = new QPixmap(QPixmap::fromImage(std::move(image)));
                const int cost = std::max(1, pixmap->width() * pixmap->height() * 4 / 1024);
                m_cache.insert(key, pixmap, cost);
                Q_EMIT imageReady(url);
            }

            startNext();
        });
    }
}
} // namespace Fooyin::Jellyfin
