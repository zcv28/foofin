/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "jellyfinitem.h"
#include "jellyfinsession.h"

#include <QJsonDocument>
#include <QObject>
#include <QUrlQuery>

#include <functional>
#include <memory>

class QNetworkReply;

namespace Fooyin {
class NetworkAccessManager;

namespace Jellyfin {
struct PublicServerInfo
{
    QUrl server; //!< Base URL after following redirects.
    QString name;
    QString id;
    QString version;
};

/*!
 * Thin asynchronous wrapper around the Jellyfin REST API.
 *
 * All callbacks run on the main thread. When a @c context object is given, the callback is dropped
 * if that object is destroyed before the reply arrives.
 */
class JellyfinClient : public QObject
{
    Q_OBJECT

public:
    using JsonCallback    = std::function<void(const QJsonDocument& document, const QString& error)>;
    using ItemsCallback   = std::function<void(const ItemPage& page, const QString& error)>;
    using SignInCallback  = std::function<void(const SignInResult& result, const QString& error)>;
    using InfoCallback    = std::function<void(const PublicServerInfo& info, const QString& error)>;
    using SuccessCallback = std::function<void(const QString& error)>;

    //! Fields requested for every item listing.
    static constexpr auto ItemFields = "MediaSources,Genres,DateCreated,SortName,ChildCount,ParentId,Overview";
    static constexpr int PageSize    = 500;

    JellyfinClient(std::shared_ptr<NetworkAccessManager> network, std::shared_ptr<JellyfinSession> session,
                   QObject* parent = nullptr);

    [[nodiscard]] std::shared_ptr<JellyfinSession> session() const;

    // Unauthenticated
    void publicInfo(const QUrl& server, QObject* context, InfoCallback callback);
    //! Resolves the server address, then authenticates by username/password.
    void signIn(const QString& serverAddress, const QString& username, const QString& password, QObject* context,
                SignInCallback callback);
    //! Ends the server session (best effort) and forgets the token locally.
    void signOut();

    // Authenticated
    void get(const QString& path, QUrlQuery query, QObject* context, JsonCallback callback);
    void post(const QString& path, const QUrlQuery& query, const QJsonDocument& body, QObject* context,
              JsonCallback callback);

    //! A single page of items from an item-listing endpoint (/Items, /Artists, /Genres, ...).
    void items(const QString& path, QUrlQuery query, QObject* context, ItemsCallback callback);
    //! Every item of a listing, paging through the results. @p maxItems < 0 means no limit.
    void allItems(const QString& path, QUrlQuery query, int maxItems, QObject* context, ItemsCallback callback);

    //! The user's music libraries.
    void musicLibraries(QObject* context, std::function<void(const JellyfinItems& views, const QString& error)> callback);

    void setFavourite(const QString& itemId, bool favourite, QObject* context, SuccessCallback callback);

    //! Fire-and-forget playback reporting (/Sessions/Playing, /Sessions/Playing/Progress, ...).
    void reportPlayback(const QString& endpoint, const QJsonObject& body);

Q_SIGNALS:
    //! An authenticated request was rejected: the token was revoked or has expired.
    void authenticationFailed();

private:
    QNetworkReply* send(const QByteArray& verb, const QUrl& url, const QByteArray& authorization,
                        const QByteArray& body = {});
    using ReplyCallback = std::function<void(const QJsonDocument& document, const QString& error, int status)>;

    void handleReply(QNetworkReply* reply, QObject* context, bool authenticated, ReplyCallback callback);
    static ReplyCallback ignoreStatus(JsonCallback callback);
    void addUserQuery(QUrlQuery& query) const;
    void fetchPages(const QString& path, const QUrlQuery& query, int maxItems, int startIndex,
                    std::shared_ptr<ItemPage> collected, QObject* context, ItemsCallback callback);

    std::shared_ptr<NetworkAccessManager> m_network;
    std::shared_ptr<JellyfinSession> m_session;
};
} // namespace Jellyfin
} // namespace Fooyin
