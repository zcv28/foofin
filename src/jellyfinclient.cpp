/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinclient.h"

#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <core/network/networkaccessmanager.h>
#include <core/network/networkutils.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace {
constexpr int RequestTimeoutMs = 30000;

QString errorText(QNetworkReply* reply, int status, const QByteArray& data)
{
    if(status == 401) {
        return Fooyin::Jellyfin::JellyfinClient::tr("Not authorised. Sign in again in the Jellyfin settings.");
    }
    if(status == 403) {
        return Fooyin::Jellyfin::JellyfinClient::tr("Access denied by the server.");
    }
    if(status >= 400) {
        QString detail = QString::fromUtf8(data.left(200)).trimmed();
        if(const auto document = QJsonDocument::fromJson(data); document.isObject()) {
            const QJsonObject object = document.object();
            detail = object.value("detail"_L1).toString(object.value("title"_L1).toString(detail));
        }
        return detail.isEmpty() ? Fooyin::Jellyfin::JellyfinClient::tr("Server error (HTTP %1)").arg(status)
                                : Fooyin::Jellyfin::JellyfinClient::tr("Server error (HTTP %1): %2").arg(status).arg(detail);
    }
    return reply->errorString();
}

//! Strips the endpoint path from a (possibly redirected) reply URL to recover the server base URL.
QUrl baseFromReply(const QUrl& replyUrl, const QString& endpoint, const QUrl& fallback)
{
    QUrl base{replyUrl};
    QString path = base.path();
    if(!path.endsWith(endpoint, Qt::CaseInsensitive)) {
        return fallback;
    }
    path.chop(endpoint.size());
    base.setPath(path);
    base.setQuery(QString{});
    return base;
}
} // namespace

namespace Fooyin::Jellyfin {
JellyfinClient::JellyfinClient(std::shared_ptr<NetworkAccessManager> network, std::shared_ptr<JellyfinSession> session,
                               QObject* parent)
    : QObject{parent}
    , m_network{std::move(network)}
    , m_session{std::move(session)}
{ }

std::shared_ptr<JellyfinSession> JellyfinClient::session() const
{
    return m_session;
}

QNetworkReply* JellyfinClient::send(const QByteArray& verb, const QUrl& url, const QByteArray& authorization,
                                    const QByteArray& body)
{
    QNetworkRequest request = makeNetworkRequest(url, NetworkRequestOption::AlwaysNetwork);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", authorization);
    request.setTransferTimeout(RequestTimeoutMs);
    if(!body.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    }

    qCDebug(FOOFIN) << verb << url.toString(QUrl::RemoveQuery) << url.query();
    return m_network->sendCustomRequest(request, verb, body);
}

JellyfinClient::ReplyCallback JellyfinClient::ignoreStatus(JsonCallback callback)
{
    return [callback = std::move(callback)](const QJsonDocument& document, const QString& error, int /*status*/) {
        callback(document, error);
    };
}

void JellyfinClient::handleReply(QNetworkReply* reply, QObject* context, bool authenticated, ReplyCallback callback)
{
    const bool hasContext = context != nullptr;
    const QPointer<QObject> guard{context};

    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, this,
                     [this, reply, hasContext, guard, authenticated, callback = std::move(callback)]() {
                         const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                         const QByteArray data = reply->readAll();

                         if(authenticated && status == 401) {
                             Q_EMIT authenticationFailed();
                         }
                         if(hasContext && !guard) {
                             return;
                         }

                         if(reply->error() != QNetworkReply::NoError || status >= 400) {
                             qCDebug(FOOFIN) << "Request failed:" << reply->url().toString(QUrl::RemoveQuery)
                                             << status << reply->errorString();
                             callback({}, errorText(reply, status, data), status);
                             return;
                         }

                         if(data.trimmed().isEmpty()) {
                             callback({}, {}, status);
                             return;
                         }

                         QJsonParseError parseError;
                         const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
                         if(parseError.error != QJsonParseError::NoError) {
                             callback({}, tr("Invalid response from the server: %1").arg(parseError.errorString()),
                                      status);
                             return;
                         }
                         callback(document, {}, status);
                     });
}

void JellyfinClient::addUserQuery(QUrlQuery& query) const
{
    if(!query.hasQueryItem(u"userId"_s)) {
        query.addQueryItem(u"userId"_s, m_session->credentials().userId);
    }
}

void JellyfinClient::publicInfo(const QUrl& server, QObject* context, InfoCallback callback)
{
    static const QString endpoint = u"/System/Info/Public"_s;

    auto* reply = send("GET", apiUrl(server, endpoint), JellyfinSession::authorizationHeader(m_session->deviceId(), {}));
    handleReply(reply, context, false,
                [reply, server, callback = std::move(callback)](const QJsonDocument& document, const QString& error,
                                                                int /*status*/) {
                    if(!error.isEmpty()) {
                        callback({}, error);
                        return;
                    }

                    const QJsonObject object = document.object();
                    const QString product    = object.value("ProductName"_L1).toString();
                    if(object.value("Id"_L1).toString().isEmpty()
                       || (!product.isEmpty() && !product.contains("Jellyfin"_L1, Qt::CaseInsensitive))) {
                        callback({}, tr("This doesn't look like a Jellyfin server."));
                        return;
                    }

                    PublicServerInfo info;
                    info.server  = baseFromReply(reply->url(), endpoint, server);
                    info.name    = object.value("ServerName"_L1).toString();
                    info.id      = object.value("Id"_L1).toString();
                    info.version = object.value("Version"_L1).toString();
                    callback(info, {});
                });
}

void JellyfinClient::signIn(const QString& serverAddress, const QString& username, const QString& password,
                            QObject* context, SignInCallback callback)
{
    const QUrl server = normaliseServerUrl(serverAddress);
    if(!server.isValid()) {
        callback({}, tr("Enter a valid server address, e.g. http://192.168.1.10:8096"));
        return;
    }

    const QPointer<QObject> guard{context};
    const bool hasContext = context != nullptr;

    publicInfo(server, context,
               [this, username, password, guard, hasContext, callback](const PublicServerInfo& info,
                                                                        const QString& error) {
                   if(hasContext && !guard) {
                       return;
                   }
                   if(!error.isEmpty()) {
                       callback({}, error);
                       return;
                   }

                   QJsonObject body;
                   body.insert("Username"_L1, username);
                   body.insert("Pw"_L1, password);

                   auto* reply = send("POST", apiUrl(info.server, u"/Users/AuthenticateByName"_s),
                                      JellyfinSession::authorizationHeader(m_session->deviceId(), {}),
                                      QJsonDocument{body}.toJson(QJsonDocument::Compact));
                   handleReply(reply, guard.data(), false,
                               [info, callback](const QJsonDocument& document, const QString& authError, int status) {
                                   if(!authError.isEmpty()) {
                                       callback({}, status == 401 ? tr("Incorrect username or password.") : authError);
                                       return;
                                   }

                                   const QJsonObject object = document.object();
                                   const QJsonObject user   = object.value("User"_L1).toObject();

                                   SignInResult result;
                                   result.server     = info.server;
                                   result.serverName = info.name;
                                   result.serverId   = object.value("ServerId"_L1).toString(info.id);
                                   result.username   = user.value("Name"_L1).toString();
                                   result.userId     = user.value("Id"_L1).toString();
                                   result.token      = object.value("AccessToken"_L1).toString();

                                   if(result.userId.isEmpty() || result.token.isEmpty()) {
                                       callback({}, tr("The server did not return an access token."));
                                       return;
                                   }
                                   callback(result, {});
                               });
               });
}

void JellyfinClient::signOut()
{
    const Credentials credentials = m_session->credentials();
    if(credentials.isValid()) {
        auto* reply = send("POST", apiUrl(credentials.server, u"/Sessions/Logout"_s), m_session->authorizationHeader());
        QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_session->signOut();
}

void JellyfinClient::get(const QString& path, QUrlQuery query, QObject* context, JsonCallback callback)
{
    const Credentials credentials = m_session->credentials();
    if(!credentials.isValid()) {
        callback({}, tr("Not signed in to a Jellyfin server."));
        return;
    }

    auto* reply = send("GET", apiUrl(credentials.server, path, query), m_session->authorizationHeader());
    handleReply(reply, context, true, ignoreStatus(std::move(callback)));
}

void JellyfinClient::post(const QString& path, const QUrlQuery& query, const QJsonDocument& body, QObject* context,
                          JsonCallback callback)
{
    const Credentials credentials = m_session->credentials();
    if(!credentials.isValid()) {
        callback({}, tr("Not signed in to a Jellyfin server."));
        return;
    }

    auto* reply = send("POST", apiUrl(credentials.server, path, query), m_session->authorizationHeader(),
                       body.isNull() ? QByteArray{} : body.toJson(QJsonDocument::Compact));
    handleReply(reply, context, true, ignoreStatus(std::move(callback)));
}

void JellyfinClient::items(const QString& path, QUrlQuery query, QObject* context, ItemsCallback callback)
{
    addUserQuery(query);
    if(!query.hasQueryItem(u"fields"_s)) {
        query.addQueryItem(u"fields"_s, QString::fromLatin1(ItemFields));
    }
    if(!query.hasQueryItem(u"enableImageTypes"_s)) {
        query.addQueryItem(u"enableImageTypes"_s, u"Primary"_s);
        query.addQueryItem(u"imageTypeLimit"_s, u"1"_s);
    }

    get(path, query, context, [callback = std::move(callback)](const QJsonDocument& document, const QString& error) {
        if(!error.isEmpty()) {
            callback({}, error);
            return;
        }
        callback(ItemPage::fromJson(document.object()), {});
    });
}

void JellyfinClient::allItems(const QString& path, QUrlQuery query, int maxItems, QObject* context,
                              ItemsCallback callback)
{
    query.removeAllQueryItems(u"startIndex"_s);
    query.removeAllQueryItems(u"limit"_s);
    fetchPages(path, query, maxItems, 0, std::make_shared<ItemPage>(), context, std::move(callback));
}

void JellyfinClient::fetchPages(const QString& path, const QUrlQuery& query, int maxItems, int startIndex,
                                std::shared_ptr<ItemPage> collected, QObject* context, ItemsCallback callback)
{
    const int remaining = maxItems < 0 ? PageSize : std::min(PageSize, maxItems - startIndex);

    QUrlQuery pageQuery{query};
    pageQuery.addQueryItem(u"startIndex"_s, QString::number(startIndex));
    pageQuery.addQueryItem(u"limit"_s, QString::number(remaining));

    const QPointer<QObject> guard{context};
    const bool hasContext = context != nullptr;

    items(path, pageQuery, context,
          [this, path, query, maxItems, startIndex, collected, guard, hasContext,
           callback = std::move(callback)](const ItemPage& page, const QString& error) mutable {
              if(!error.isEmpty()) {
                  callback(*collected, error);
                  return;
              }

              collected->totalRecordCount = page.totalRecordCount;
              collected->items.insert(collected->items.end(), page.items.cbegin(), page.items.cend());

              const int fetched = startIndex + static_cast<int>(page.items.size());
              const bool limitReached = maxItems >= 0 && fetched >= maxItems;
              if(page.items.empty() || fetched >= page.totalRecordCount || limitReached || (hasContext && !guard)) {
                  callback(*collected, {});
                  return;
              }

              fetchPages(path, query, maxItems, fetched, collected, guard.data(), std::move(callback));
          });
}

void JellyfinClient::musicLibraries(QObject* context,
                                    std::function<void(const JellyfinItems& views, const QString& error)> callback)
{
    QUrlQuery query;
    addUserQuery(query);

    get(u"/UserViews"_s, query, context,
        [callback = std::move(callback)](const QJsonDocument& document, const QString& error) {
            if(!error.isEmpty()) {
                callback({}, error);
                return;
            }

            JellyfinItems libraries;
            for(JellyfinItem& item : ItemPage::fromJson(document.object()).items) {
                if(item.collectionType == "music"_L1) {
                    libraries.push_back(std::move(item));
                }
            }
            callback(libraries, {});
        });
}

void JellyfinClient::setFavourite(const QString& itemId, bool favourite, QObject* context, SuccessCallback callback)
{
    const Credentials credentials = m_session->credentials();
    if(!credentials.isValid()) {
        callback(tr("Not signed in to a Jellyfin server."));
        return;
    }

    const QByteArray verb = favourite ? "POST" : "DELETE";

    QUrlQuery query;
    query.addQueryItem(u"userId"_s, credentials.userId);

    auto* reply = send(verb, apiUrl(credentials.server, u"/UserFavoriteItems/%1"_s.arg(itemId), query),
                       m_session->authorizationHeader());
    const QPointer<QObject> guard{context};
    handleReply(reply, context, true,
                [this, verb, itemId, credentials, guard, callback](const QJsonDocument&, const QString& error,
                                                                   int status) {
                    if(error.isEmpty() || status != 404) {
                        callback(error);
                        return;
                    }
                    // Servers older than 10.9 only know the per-user route.
                    auto* legacy = send(
                        verb,
                        apiUrl(credentials.server, u"/Users/%1/FavoriteItems/%2"_s.arg(credentials.userId, itemId)),
                        m_session->authorizationHeader());
                    handleReply(legacy, guard.data(), true,
                                [callback](const QJsonDocument&, const QString& legacyError, int /*status*/) {
                                    callback(legacyError);
                                });
                });
}

void JellyfinClient::reportPlayback(const QString& endpoint, const QJsonObject& body)
{
    post(endpoint, {}, QJsonDocument{body}, nullptr, [endpoint](const QJsonDocument&, const QString& error) {
        if(!error.isEmpty()) {
            qCDebug(FOOFIN) << "Playback report" << endpoint << "failed:" << error;
        }
    });
}
} // namespace Fooyin::Jellyfin
