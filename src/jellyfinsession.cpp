/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinsession.h"

#include "jellyfinsettings.h"

#include <core/accountstore.h>
#include <utils/fypaths.h>
#include <utils/settings/settingsmanager.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QUuid>

#include <tuple>

using namespace Qt::StringLiterals;

#ifndef FOOFIN_VERSION
#define FOOFIN_VERSION "0.0.0"
#endif

namespace {
QString setting(Fooyin::SettingsManager* settings, const char* key)
{
    return settings->value(QLatin1StringView{key}).toString();
}

//! Jellyfin parses the header's quoted values URL-decoded, so encode anything unusual.
QByteArray headerValue(const QString& value)
{
    return QUrl::toPercentEncoding(value, " ._-()");
}
} // namespace

namespace Fooyin::Jellyfin {
JellyfinSession::JellyfinSession(SettingsManager* settings, DbConnectionPoolPtr dbPool)
    : m_settings{settings}
    , m_accounts{dbPool ? std::make_unique<AccountStore>(std::move(dbPool)) : nullptr}
{
    QString deviceId = setting(m_settings, Settings::DeviceId);
    if(deviceId.isEmpty()) {
        deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings->set(QLatin1StringView{Settings::DeviceId}, deviceId);
    }

    Credentials credentials;
    credentials.server     = QUrl{setting(m_settings, Settings::ServerUrl)};
    credentials.serverName = setting(m_settings, Settings::ServerName);
    credentials.serverId   = setting(m_settings, Settings::ServerId);
    credentials.username   = setting(m_settings, Settings::Username);
    credentials.userId     = setting(m_settings, Settings::UserId);
    credentials.deviceId   = deviceId;

    if(m_accounts) {
        const QString field = QString::fromLatin1(AccountTokenField);
        if(const auto values = m_accounts->load(QString::fromLatin1(AccountName), {field})) {
            if(const auto it = values->find(field); it != values->end()) {
                credentials.token = it->second;
            }
        }
    }

    if(credentials.isValid()) {
        writeArchiveFile(credentials);
    }

    {
        const std::scoped_lock lock{m_mutex};
        m_credentials = std::move(credentials);
    }

    reloadPreferences();

    m_settings->subscribe(QLatin1StringView{Settings::StreamFormat}, this, &JellyfinSession::reloadPreferences);
    m_settings->subscribe(QLatin1StringView{Settings::MaxBitrate}, this, &JellyfinSession::reloadPreferences);
    m_settings->subscribe(QLatin1StringView{Settings::LibraryId}, this, &JellyfinSession::libraryChanged);
}

JellyfinSession::~JellyfinSession() = default;

Credentials JellyfinSession::credentials() const
{
    const std::scoped_lock lock{m_mutex};
    return m_credentials;
}

StreamOptions JellyfinSession::streamOptions() const
{
    const std::scoped_lock lock{m_mutex};
    return m_streamOptions;
}

bool JellyfinSession::isSignedIn() const
{
    const std::scoped_lock lock{m_mutex};
    return m_credentials.isValid();
}

QString JellyfinSession::deviceId() const
{
    const std::scoped_lock lock{m_mutex};
    return m_credentials.deviceId;
}

QString JellyfinSession::libraryId() const
{
    return setting(m_settings, Settings::LibraryId);
}

int JellyfinSession::artworkSize() const
{
    return std::clamp(m_settings->value(QLatin1StringView{Settings::ArtworkSize}).toInt(), 100, 3000);
}

QByteArray JellyfinSession::authorizationHeader() const
{
    const Credentials credentials = this->credentials();
    return authorizationHeader(credentials.deviceId, credentials.token);
}

QByteArray JellyfinSession::authorizationHeader(const QString& deviceId, const QString& token)
{
    QString device = QSysInfo::machineHostName();
    if(device.isEmpty()) {
        device = u"fooyin"_s;
    }

    QByteArray header = "MediaBrowser Client=\"fooyin\", Device=\"" + headerValue(device) + "\", DeviceId=\""
                      + headerValue(deviceId) + "\", Version=\"" FOOFIN_VERSION "\"";
    if(!token.isEmpty()) {
        header += ", Token=\"" + headerValue(token) + '"';
    }
    return header;
}

void JellyfinSession::signIn(const SignInResult& result)
{
    if(setting(m_settings, Settings::ServerId) != result.serverId) {
        // A library chosen on another server means nothing here.
        m_settings->set(QLatin1StringView{Settings::LibraryId}, QString{});
    }

    m_settings->set(QLatin1StringView{Settings::ServerUrl}, result.server.toString());
    m_settings->set(QLatin1StringView{Settings::ServerName}, result.serverName);
    m_settings->set(QLatin1StringView{Settings::ServerId}, result.serverId);
    m_settings->set(QLatin1StringView{Settings::Username}, result.username);
    m_settings->set(QLatin1StringView{Settings::UserId}, result.userId);

    if(m_accounts
       && !m_accounts->save(QString::fromLatin1(AccountName), {{QString::fromLatin1(AccountTokenField), result.token}})) {
        qCWarning(FOOFIN) << "Unable to store the Jellyfin access token; you will need to sign in again next time";
    }

    {
        const std::scoped_lock lock{m_mutex};
        m_credentials.server     = result.server;
        m_credentials.serverName = result.serverName;
        m_credentials.serverId   = result.serverId;
        m_credentials.username   = result.username;
        m_credentials.userId     = result.userId;
        m_credentials.token      = result.token;
    }
    writeArchiveFile(credentials());

    qCInfo(FOOFIN) << "Signed in to" << result.server.toString() << "as" << result.username;
    Q_EMIT signedInChanged(true);
}

void JellyfinSession::signOut()
{
    if(m_accounts) {
        std::ignore = m_accounts->remove(QString::fromLatin1(AccountName), {QString::fromLatin1(AccountTokenField)});
    }
    m_settings->set(QLatin1StringView{Settings::UserId}, QString{});

    {
        const std::scoped_lock lock{m_mutex};
        m_credentials.userId.clear();
        m_credentials.token.clear();
    }

    Q_EMIT signedInChanged(false);
}

QString JellyfinSession::itemIdForPath(const QString& path) const
{
    const std::scoped_lock lock{m_mutex};
    if(const ArchiveTrackRef ref = archiveTrackRef(path); !ref.itemId.isEmpty()) {
        return ref.serverId == m_credentials.serverId ? ref.itemId : QString{};
    }
    return itemIdFromTrackPath(m_credentials.server, path);
}

QString JellyfinSession::archiveFile() const
{
    const std::scoped_lock lock{m_mutex};
    return m_credentials.serverId.isEmpty() ? QString{} : archiveFilePath(m_credentials.serverId);
}

QString JellyfinSession::archiveFilePath(const QString& serverId)
{
    return Utils::sharePath(u"foofin"_s) + u'/' + serverId + u'.' + QLatin1StringView{ArchiveExtension};
}

void JellyfinSession::writeArchiveFile(const Credentials& credentials) const
{
    if(credentials.serverId.isEmpty()) {
        return;
    }

    // Holds what the archive reader needs to reach the server. Rewritten on every sign-in, so
    // playlists keep working when the server's address changes.
    QJsonObject object;
    object.insert("ServerId"_L1, credentials.serverId);
    object.insert("ServerName"_L1, credentials.serverName);
    object.insert("ServerUrl"_L1, credentials.server.toString());

    const QString path = archiveFilePath(credentials.serverId);
    QDir{}.mkpath(QFileInfo{path}.absolutePath());

    QSaveFile file{path};
    if(!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument{object}.toJson()) < 0 || !file.commit()) {
        qCWarning(FOOFIN) << "Unable to write" << path << file.errorString();
    }
}

void JellyfinSession::reloadPreferences()
{
    StreamOptions options;
    options.format         = streamFormatFromKey(setting(m_settings, Settings::StreamFormat));
    options.maxBitrateKbps = m_settings->value(QLatin1StringView{Settings::MaxBitrate}).toInt();

    const std::scoped_lock lock{m_mutex};
    m_streamOptions = options;
}
} // namespace Fooyin::Jellyfin
