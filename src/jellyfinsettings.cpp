/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinsettings.h"

#include "jellyfintrack.h"

#include <gui/trackselectioncontroller.h>
#include <utils/settings/settingsmanager.h>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(FOOFIN, "fy.foofin")

namespace Fooyin::Jellyfin {
void registerSettings(SettingsManager* settings)
{
    settings->createSetting(QString::fromLatin1(Settings::ServerUrl), QString{});
    settings->createSetting(QString::fromLatin1(Settings::ServerName), QString{});
    settings->createSetting(QString::fromLatin1(Settings::ServerId), QString{});
    settings->createSetting(QString::fromLatin1(Settings::Username), QString{});
    settings->createSetting(QString::fromLatin1(Settings::UserId), QString{});
    settings->createSetting(QString::fromLatin1(Settings::DeviceId), QString{});

    settings->createSetting(QString::fromLatin1(Settings::LibraryId), QString{});
    settings->createSetting(QString::fromLatin1(Settings::StreamFormat), streamFormatKey(StreamFormat::Original));
    settings->createSetting(QString::fromLatin1(Settings::MaxBitrate), 320);
    settings->createSetting(QString::fromLatin1(Settings::ReportPlayback), true);
    settings->createSetting(QString::fromLatin1(Settings::ArtworkSize), 600);
    settings->createSetting(QString::fromLatin1(Settings::BrowserCovers), true);
    settings->createSetting(QString::fromLatin1(Settings::DoubleClick), static_cast<int>(TrackAction::Play));
}
} // namespace Fooyin::Jellyfin
