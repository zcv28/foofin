/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(FOOFIN)

namespace Fooyin {
class SettingsManager;

namespace Jellyfin {
constexpr auto SettingsPageId = "Fooyin.Page.Foofin.Server";
//! Name of the AccountStore entry holding the access token.
constexpr auto AccountName = "Foofin";
constexpr auto AccountTokenField = "AccessToken";

namespace Settings {
// Connection (written on sign-in, cleared on sign-out)
constexpr auto ServerUrl  = "Foofin/ServerUrl";
constexpr auto ServerName = "Foofin/ServerName";
constexpr auto ServerId   = "Foofin/ServerId";
constexpr auto Username   = "Foofin/Username";
constexpr auto UserId     = "Foofin/UserId";
constexpr auto DeviceId   = "Foofin/DeviceId";

// Preferences
constexpr auto LibraryId      = "Foofin/LibraryId";
constexpr auto StreamFormat   = "Foofin/StreamFormat";
constexpr auto MaxBitrate     = "Foofin/MaxBitrate";
constexpr auto ReportPlayback = "Foofin/ReportPlayback";
constexpr auto ArtworkSize    = "Foofin/ArtworkSize";
constexpr auto BrowserCovers  = "Foofin/BrowserCovers";
constexpr auto DoubleClick    = "Foofin/DoubleClickAction";
} // namespace Settings

void registerSettings(SettingsManager* settings);
} // namespace Jellyfin
} // namespace Fooyin
