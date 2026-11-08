/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "jellyfintrack.h"

#include <utils/database/dbconnectionpool.h>

#include <QObject>
#include <QUrl>

#include <memory>
#include <mutex>

namespace Fooyin {
class AccountStore;
class SettingsManager;

namespace Jellyfin {
struct Credentials
{
    QUrl server;
    QString serverName;
    QString serverId;
    QString username;
    QString userId;
    QString token;
    QString deviceId;

    [[nodiscard]] bool isValid() const
    {
        return server.isValid() && !userId.isEmpty() && !token.isEmpty();
    }
};

struct SignInResult
{
    QUrl server;
    QString serverName;
    QString serverId;
    QString username;
    QString userId;
    QString token;
};

/*!
 * The signed-in connection to a Jellyfin server.
 *
 * Owned through a shared pointer because the stream source provider (which lives inside fooyin's
 * AudioLoader and is called from the engine thread) needs it too. Everything readable from other
 * threads is a snapshot guarded by a mutex; mutation happens on the main thread.
 */
class JellyfinSession : public QObject
{
    Q_OBJECT

public:
    JellyfinSession(SettingsManager* settings, DbConnectionPoolPtr dbPool);
    ~JellyfinSession() override;

    //! Thread-safe.
    [[nodiscard]] Credentials credentials() const;
    //! Thread-safe.
    [[nodiscard]] StreamOptions streamOptions() const;
    //! Thread-safe.
    [[nodiscard]] bool isSignedIn() const;
    [[nodiscard]] QString deviceId() const;
    [[nodiscard]] QString libraryId() const;
    [[nodiscard]] int artworkSize() const;

    //! The value of the "Authorization" header for API requests. Thread-safe.
    [[nodiscard]] QByteArray authorizationHeader() const;
    [[nodiscard]] static QByteArray authorizationHeader(const QString& deviceId, const QString& token);

    void signIn(const SignInResult& result);
    void signOut();

    //! Item id of @p track's stream on the signed-in server, or empty. Thread-safe.
    [[nodiscard]] QString itemIdForPath(const QString& path) const;

Q_SIGNALS:
    void signedInChanged(bool signedIn);
    void libraryChanged();

private:
    void reloadPreferences();

    SettingsManager* m_settings;
    std::unique_ptr<AccountStore> m_accounts;

    mutable std::mutex m_mutex;
    Credentials m_credentials;
    StreamOptions m_streamOptions;
};
} // namespace Jellyfin
} // namespace Fooyin
