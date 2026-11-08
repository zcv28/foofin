/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <utils/settings/settingspage.h>

namespace Fooyin {
class SettingsManager;

namespace Jellyfin {
class JellyfinClient;
class JellyfinPlaylistSync;

class JellyfinSettingsPage : public SettingsPage
{
    Q_OBJECT

public:
    JellyfinSettingsPage(JellyfinClient* client, JellyfinPlaylistSync* playlistSync, SettingsManager* settings,
                         QObject* parent = nullptr);
};
} // namespace Jellyfin
} // namespace Fooyin
