/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/network/remotesourceprovider.h>

#include <memory>

namespace Fooyin {
class AudioLoader;
class NetworkAccessManager;
class RemoteIoService;
class SettingsManager;

namespace Jellyfin {
class JellyfinSession;

/*!
 * Opens Jellyfin tracks for playback.
 *
 * Track paths stored in playlists are bare stream URLs ("<server>/Audio/<id>/stream"). When fooyin
 * opens one, this provider adds the access token and the configured stream format, then hands the
 * resulting URL to fooyin's own HTTP stream device, which supports range requests and therefore
 * seeking. Any other URL is passed through untouched.
 */
class JellyfinSourceProvider : public RemoteSourceProvider
{
public:
    JellyfinSourceProvider(std::shared_ptr<RemoteIoService> fallback, std::shared_ptr<JellyfinSession> session);

    [[nodiscard]] RemoteStreamSource createStreamSource(const QUrl& url) const override;

private:
    std::shared_ptr<RemoteIoService> m_fallback;
    std::shared_ptr<JellyfinSession> m_session;
};

//! Installs the provider into @p loader; @p io handles every URL that isn't a Jellyfin stream.
void installSourceProvider(AudioLoader& loader, const std::shared_ptr<RemoteIoService>& io,
                           const std::shared_ptr<JellyfinSession>& session);
} // namespace Jellyfin
} // namespace Fooyin
