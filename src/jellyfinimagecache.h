/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QUrl>

#include <deque>
#include <memory>

namespace Fooyin {
class NetworkAccessManager;

namespace Jellyfin {
//! Small in-memory cache of downloaded thumbnails for the browser tree.
class JellyfinImageCache : public QObject
{
    Q_OBJECT

public:
    explicit JellyfinImageCache(std::shared_ptr<NetworkAccessManager> network, QObject* parent = nullptr);

    //! The cached thumbnail for @p url, or a null pixmap after queueing a download.
    [[nodiscard]] QPixmap image(const QUrl& url);
    void clear();

Q_SIGNALS:
    void imageReady(const QUrl& url);

private:
    void startNext();

    std::shared_ptr<NetworkAccessManager> m_network;
    QCache<QString, QPixmap> m_cache;
    QSet<QString> m_failed;
    QSet<QString> m_pending;
    std::deque<QUrl> m_queue;
    int m_active{0};
};
} // namespace Jellyfin
} // namespace Fooyin
