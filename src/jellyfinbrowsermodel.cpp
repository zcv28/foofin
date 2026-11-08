/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinbrowsermodel.h"

#include "jellyfinclient.h"
#include "jellyfinimagecache.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <gui/guiconstants.h>
#include <gui/trackmimedata.h>
#include <utils/settings/settingsmanager.h>

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QPointer>
#include <QSet>
#include <QStyle>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {
constexpr int ThumbnailRequestSize = 96;
constexpr int ShelfSize            = 100;

QString formatDuration(uint64_t ms)
{
    const auto totalSeconds = static_cast<int>(ms / 1000);
    const int hours         = totalSeconds / 3600;
    const int minutes       = (totalSeconds % 3600) / 60;
    const int seconds       = totalSeconds % 60;
    if(hours > 0) {
        return u"%1:%2:%3"_s.arg(hours).arg(minutes, 2, 10, QChar{u'0'}).arg(seconds, 2, 10, QChar{u'0'});
    }
    return u"%1:%2"_s.arg(minutes).arg(seconds, 2, 10, QChar{u'0'});
}

bool sameArtists(const QStringList& a, const QStringList& b)
{
    return a.join(u';').compare(b.join(u';'), Qt::CaseInsensitive) == 0;
}

using Fooyin::Jellyfin::Category;

bool categoryHasTracks(Category category)
{
    switch(category) {
        case Category::FavouriteTracks:
        case Category::RecentlyPlayed:
        case Category::MostPlayed:
        case Category::Random:
        case Category::SearchTracks:
            return true;
        default:
            return false;
    }
}

bool categoryHasAlbums(Category category)
{
    switch(category) {
        case Category::FavouriteAlbums:
        case Category::RecentlyAdded:
        case Category::SearchAlbums:
            return true;
        default:
            return false;
    }
}

bool isSearchCategory(Category category)
{
    return category == Category::SearchArtists || category == Category::SearchAlbums
        || category == Category::SearchTracks;
}
} // namespace

namespace Fooyin::Jellyfin {
struct JellyfinBrowserModel::Node
{
    quint64 id{0};
    NodeKind kind{NodeKind::Root};
    Category category{Category::None};
    JellyfinItem item;
    QString label;

    Node* parent{nullptr};
    int row{0};
    std::vector<std::unique_ptr<Node>> children;

    bool expandable{false};
    bool loaded{false};
    bool loading{false};
    //! For artists: listed as an album artist (rather than any contributing artist).
    bool albumArtist{false};

    std::optional<TrackList> tracks;
    std::vector<std::function<void()>> waiters;

    [[nodiscard]] bool childrenAreTracks() const
    {
        return loaded && !children.empty()
            && std::ranges::all_of(children, [](const auto& child) { return child->kind == NodeKind::Track; });
    }
};

JellyfinBrowserModel::JellyfinBrowserModel(JellyfinClient* client, JellyfinImageCache* images,
                                           SettingsManager* settings, QObject* parent)
    : QAbstractItemModel{parent}
    , m_client{client}
    , m_images{images}
    , m_settings{settings}
    , m_root{std::make_unique<Node>()}
{
    QObject::connect(m_images, &JellyfinImageCache::imageReady, this, [this](const QUrl& url) {
        const auto it = m_imageNodes.find(url.toString());
        if(it == m_imageNodes.end()) {
            return;
        }
        for(const quint64 id : std::as_const(it.value())) {
            if(const Node* node = findNode(id)) {
                const QModelIndex index = indexForNode(node);
                Q_EMIT dataChanged(index, index, {Qt::DecorationRole});
            }
        }
        m_imageNodes.erase(it);
    });
}

JellyfinBrowserModel::~JellyfinBrowserModel() = default;

void JellyfinBrowserModel::reconnect()
{
    m_librariesLoaded  = false;
    m_librariesRequest = 0; // ignore any request still in flight
    m_musicLibraries.clear();
    m_images->clear();
    reload();
}

void JellyfinBrowserModel::reload()
{
    beginResetModel();
    resetTree();

    if(!m_client->session()->isSignedIn()) {
        setStatus(m_root.get(), tr("Not signed in. Open Settings → Jellyfin to connect to a Jellyfin server."),
                  false);
        endResetModel();
        return;
    }

    if(!m_librariesLoaded) {
        setStatus(m_root.get(), tr("Connecting…"), false);
        endResetModel();

        if(m_librariesRequest != 0) {
            return; // the pending request rebuilds the tree for whatever the state is then
        }

        const int request  = ++m_lastLibrariesRequest;
        m_librariesRequest = request;
        m_client->musicLibraries(this, [this, request](const JellyfinItems& libraries, const QString& error) {
            if(request != m_librariesRequest) {
                return;
            }
            m_librariesRequest = 0;
            if(!error.isEmpty()) {
                beginResetModel();
                resetTree();
                setStatus(m_root.get(), error, false);
                endResetModel();
                return;
            }
            m_musicLibraries.clear();
            for(const JellyfinItem& library : libraries) {
                m_musicLibraries.push_back(library.id);
            }
            m_librariesLoaded = true;
            reload();
        });
        return;
    }

    if(m_search.isEmpty()) {
        buildBrowseRoot();
        endResetModel();
        Q_EMIT loaded();
        return;
    }

    buildSearchRoot();
    endResetModel();

    // Search groups load straight away; announce once all three are in.
    auto remaining = std::make_shared<int>(static_cast<int>(m_root->children.size()));
    for(const auto& child : m_root->children) {
        fetchChildren(child.get(), [this, remaining]() {
            if(--*remaining == 0) {
                Q_EMIT loaded();
            }
        });
    }
}

void JellyfinBrowserModel::setSearch(const QString& search)
{
    const QString trimmed = search.trimmed();
    if(trimmed == m_search) {
        return;
    }
    m_search = trimmed;
    reload();
}

QString JellyfinBrowserModel::search() const
{
    return m_search;
}

void JellyfinBrowserModel::refresh(const QModelIndex& index)
{
    Node* node = nodeFromIndex(index);
    if(node && node->kind == NodeKind::Status) {
        node = node->parent;
    }
    if(!node || node == m_root.get()) {
        reconnect();
        return;
    }
    if(!node->expandable || node->loading) {
        return;
    }

    for(Node* ancestor = node; ancestor; ancestor = ancestor->parent) {
        ancestor->tracks.reset();
    }

    if(childListing(*node)) {
        removeChildren(node);
        node->loaded = false;
        fetchChildren(node);
    }
}

void JellyfinBrowserModel::resetTree()
{
    ++m_generation;
    m_nodes.clear();
    m_imageNodes.clear();
    m_root           = std::make_unique<Node>();
    m_root->loaded   = true;
    m_root->expandable = true;
}

JellyfinBrowserModel::Node* JellyfinBrowserModel::addCategory(Node* parent, Category category, const QString& label)
{
    auto node        = std::make_unique<Node>();
    node->id         = m_nextId++;
    node->kind       = NodeKind::Category;
    node->category   = category;
    node->label      = label;
    node->parent     = parent;
    node->row        = static_cast<int>(parent->children.size());
    node->expandable = true;

    Node* raw = node.get();
    m_nodes.emplace(raw->id, raw);
    parent->children.push_back(std::move(node));
    return raw;
}

void JellyfinBrowserModel::buildBrowseRoot()
{
    Node* root = m_root.get();

    addCategory(root, Category::Artists, tr("Artists"));
    addCategory(root, Category::Albums, tr("Albums"));
    addCategory(root, Category::Genres, tr("Genres"));
    addCategory(root, Category::Playlists, tr("Playlists"));

    Node* favourites   = addCategory(root, Category::Favourites, tr("Favourites"));
    favourites->loaded = true;
    addCategory(favourites, Category::FavouriteArtists, tr("Artists"));
    addCategory(favourites, Category::FavouriteAlbums, tr("Albums"));
    addCategory(favourites, Category::FavouriteTracks, tr("Tracks"));

    addCategory(root, Category::RecentlyAdded, tr("Recently Added"));
    addCategory(root, Category::RecentlyPlayed, tr("Recently Played"));
    addCategory(root, Category::MostPlayed, tr("Most Played"));
    addCategory(root, Category::Random, tr("Random Tracks"));
}

void JellyfinBrowserModel::buildSearchRoot()
{
    Node* root = m_root.get();
    addCategory(root, Category::SearchArtists, tr("Artists"));
    addCategory(root, Category::SearchAlbums, tr("Albums"));
    addCategory(root, Category::SearchTracks, tr("Tracks"));
}

JellyfinBrowserModel::Node* JellyfinBrowserModel::nodeFromIndex(const QModelIndex& index) const
{
    if(!index.isValid() || index.model() != this) {
        return nullptr;
    }
    return static_cast<Node*>(index.internalPointer());
}

QModelIndex JellyfinBrowserModel::indexForNode(const Node* node, int column) const
{
    if(!node || node == m_root.get()) {
        return {};
    }
    return createIndex(node->row, column, const_cast<Node*>(node));
}

JellyfinBrowserModel::Node* JellyfinBrowserModel::findNode(quint64 id) const
{
    const auto it = m_nodes.find(id);
    return it != m_nodes.end() ? it->second : nullptr;
}

QString JellyfinBrowserModel::libraryScope() const
{
    const QString selected = m_client->session()->libraryId();
    if(!selected.isEmpty()) {
        return selected;
    }
    // A single music library is the common case; scoping to it keeps genres from other
    // libraries (films, shows) out of the listings.
    return m_musicLibraries.size() == 1 ? m_musicLibraries.front() : QString{};
}

void JellyfinBrowserModel::addLibraryScope(QUrlQuery& query) const
{
    if(const QString scope = libraryScope(); !scope.isEmpty()) {
        query.addQueryItem(u"parentId"_s, scope);
    }
}

std::optional<JellyfinBrowserModel::Listing> JellyfinBrowserModel::childListing(const Node& node) const
{
    Listing listing;
    listing.path = u"/Items"_s;
    QUrlQuery& query = listing.query;

    const auto items = [&query](const QString& types) {
        query.addQueryItem(u"includeItemTypes"_s, types);
        query.addQueryItem(u"recursive"_s, u"true"_s);
    };
    const auto sort = [&query](const QString& sortBy, bool descending = false) {
        query.addQueryItem(u"sortBy"_s, sortBy);
        query.addQueryItem(u"sortOrder"_s, descending ? u"Descending"_s : u"Ascending"_s);
    };
    static const QString TrackOrder = u"AlbumArtist,Album,ParentIndexNumber,IndexNumber,SortName"_s;

    switch(node.kind) {
        case NodeKind::Category:
            switch(node.category) {
                case Category::Artists:
                    listing.path      = u"/Artists/AlbumArtists"_s;
                    listing.childKind = NodeKind::Artist;
                    sort(u"SortName"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::Albums:
                    listing.childKind = NodeKind::Album;
                    items(u"MusicAlbum"_s);
                    sort(u"SortName"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::Playlists:
                    listing.childKind = NodeKind::Playlist;
                    items(u"Playlist"_s);
                    query.addQueryItem(u"mediaTypes"_s, u"Audio"_s);
                    sort(u"SortName"_s);
                    return listing;
                case Category::Genres:
                    listing.path      = u"/MusicGenres"_s;
                    listing.childKind = NodeKind::Genre;
                    sort(u"SortName"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::FavouriteArtists:
                    listing.path      = u"/Artists"_s;
                    listing.childKind = NodeKind::Artist;
                    query.addQueryItem(u"isFavorite"_s, u"true"_s);
                    sort(u"SortName"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::FavouriteAlbums:
                    listing.childKind = NodeKind::Album;
                    items(u"MusicAlbum"_s);
                    query.addQueryItem(u"filters"_s, u"IsFavorite"_s);
                    sort(u"SortName"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::FavouriteTracks:
                    listing.childKind = NodeKind::Track;
                    items(u"Audio"_s);
                    query.addQueryItem(u"filters"_s, u"IsFavorite"_s);
                    sort(TrackOrder);
                    addLibraryScope(query);
                    return listing;
                case Category::RecentlyAdded:
                    listing.childKind = NodeKind::Album;
                    listing.maxItems  = ShelfSize;
                    items(u"MusicAlbum"_s);
                    sort(u"DateCreated"_s, true);
                    addLibraryScope(query);
                    return listing;
                case Category::RecentlyPlayed:
                    listing.childKind = NodeKind::Track;
                    listing.maxItems  = ShelfSize;
                    items(u"Audio"_s);
                    query.addQueryItem(u"filters"_s, u"IsPlayed"_s);
                    sort(u"DatePlayed"_s, true);
                    addLibraryScope(query);
                    return listing;
                case Category::MostPlayed:
                    listing.childKind = NodeKind::Track;
                    listing.maxItems  = ShelfSize;
                    items(u"Audio"_s);
                    query.addQueryItem(u"filters"_s, u"IsPlayed"_s);
                    sort(u"PlayCount"_s, true);
                    addLibraryScope(query);
                    return listing;
                case Category::Random:
                    listing.childKind = NodeKind::Track;
                    listing.maxItems  = ShelfSize;
                    items(u"Audio"_s);
                    query.addQueryItem(u"sortBy"_s, u"Random"_s);
                    addLibraryScope(query);
                    return listing;
                case Category::SearchArtists:
                    listing.path      = u"/Artists"_s;
                    listing.childKind = NodeKind::Artist;
                    listing.maxItems  = 100;
                    query.addQueryItem(u"searchTerm"_s, m_search);
                    addLibraryScope(query);
                    return listing;
                case Category::SearchAlbums:
                    listing.childKind = NodeKind::Album;
                    listing.maxItems  = 200;
                    items(u"MusicAlbum"_s);
                    query.addQueryItem(u"searchTerm"_s, m_search);
                    addLibraryScope(query);
                    return listing;
                case Category::SearchTracks:
                    listing.childKind = NodeKind::Track;
                    listing.maxItems  = 500;
                    items(u"Audio"_s);
                    query.addQueryItem(u"searchTerm"_s, m_search);
                    addLibraryScope(query);
                    return listing;
                case Category::Favourites:
                case Category::None:
                    return {};
            }
            return {};
        case NodeKind::Artist:
            listing.childKind = NodeKind::Album;
            items(u"MusicAlbum"_s);
            query.addQueryItem(node.albumArtist ? u"albumArtistIds"_s : u"artistIds"_s, node.item.id);
            sort(u"ProductionYear,SortName"_s);
            addLibraryScope(query);
            return listing;
        case NodeKind::Genre:
            listing.childKind = NodeKind::Album;
            items(u"MusicAlbum"_s);
            query.addQueryItem(u"genreIds"_s, node.item.id);
            sort(u"AlbumArtist,SortName"_s);
            addLibraryScope(query);
            return listing;
        case NodeKind::Album:
            listing.childKind = NodeKind::Track;
            query.addQueryItem(u"parentId"_s, node.item.id);
            items(u"Audio"_s);
            sort(u"ParentIndexNumber,IndexNumber,SortName"_s);
            return listing;
        case NodeKind::Playlist:
            listing.path      = u"/Playlists/%1/Items"_s.arg(node.item.id);
            listing.childKind = NodeKind::Track;
            return listing;
        case NodeKind::Root:
        case NodeKind::Track:
        case NodeKind::Status:
            return {};
    }
    return {};
}

std::optional<JellyfinBrowserModel::Listing> JellyfinBrowserModel::trackListing(const Node& node) const
{
    Listing listing;
    listing.path      = u"/Items"_s;
    listing.childKind = NodeKind::Track;
    QUrlQuery& query  = listing.query;

    switch(node.kind) {
        case NodeKind::Artist:
            query.addQueryItem(u"includeItemTypes"_s, u"Audio"_s);
            query.addQueryItem(u"recursive"_s, u"true"_s);
            query.addQueryItem(node.albumArtist ? u"albumArtistIds"_s : u"artistIds"_s, node.item.id);
            query.addQueryItem(u"sortBy"_s, u"ProductionYear,Album,ParentIndexNumber,IndexNumber,SortName"_s);
            addLibraryScope(query);
            return listing;
        case NodeKind::Genre:
            query.addQueryItem(u"includeItemTypes"_s, u"Audio"_s);
            query.addQueryItem(u"recursive"_s, u"true"_s);
            query.addQueryItem(u"genreIds"_s, node.item.id);
            query.addQueryItem(u"sortBy"_s, u"AlbumArtist,ProductionYear,Album,ParentIndexNumber,IndexNumber"_s);
            addLibraryScope(query);
            return listing;
        case NodeKind::Album:
        case NodeKind::Playlist:
            return childListing(node);
        case NodeKind::Category:
            if(categoryHasTracks(node.category)) {
                return childListing(node);
            }
            return {};
        case NodeKind::Root:
        case NodeKind::Track:
        case NodeKind::Status:
            return {};
    }
    return {};
}

void JellyfinBrowserModel::fetchChildren(Node* node, std::function<void()> whenDone)
{
    if(!node) {
        return;
    }
    if(whenDone) {
        node->waiters.push_back(std::move(whenDone));
    }

    const auto runWaiters = [](Node* target) {
        auto waiters = std::exchange(target->waiters, {});
        for(auto& waiter : waiters) {
            waiter();
        }
    };

    if(node->loaded) {
        runWaiters(node);
        return;
    }
    if(node->loading) {
        return;
    }

    const auto listing = childListing(*node);
    if(!listing) {
        node->loaded = true;
        runWaiters(node);
        return;
    }

    // No placeholder row: fetchMore() can be called from within a view's (or the model tester's)
    // reset handling, where inserting rows is not allowed. The node shows "Loading…" instead.
    node->loading = true;
    if(const QModelIndex index = indexForNode(node, InfoColumn); index.isValid()) {
        Q_EMIT dataChanged(index, index, {Qt::DisplayRole});
    }

    const quint64 id     = node->id;
    const int generation = m_generation;
    const NodeKind kind  = listing->childKind;

    m_client->allItems(listing->path, listing->query, listing->maxItems, this,
                       [this, id, generation, kind, runWaiters](const ItemPage& page, const QString& error) {
                           if(generation != m_generation) {
                               return;
                           }
                           Node* target = findNode(id);
                           if(!target) {
                               return;
                           }
                           populate(target, page.items, kind, error);
                           runWaiters(target);
                       });
}

void JellyfinBrowserModel::populate(Node* node, const JellyfinItems& items, NodeKind kind, const QString& error)
{
    removeChildren(node);
    node->loading = false;
    node->loaded  = true;

    if(!error.isEmpty()) {
        qCWarning(FOOFIN) << "Failed to load" << node->label << node->item.name << ':' << error;
    }

    if(items.empty()) {
        setStatus(node, error.isEmpty() ? tr("No items") : error);
    }
    else {
        const QModelIndex parentIndex = indexForNode(node);
        beginInsertRows(parentIndex, 0, static_cast<int>(items.size()) - 1);
        node->children.reserve(items.size());
        for(const JellyfinItem& item : items) {
            auto child         = std::make_unique<Node>();
            child->id          = m_nextId++;
            child->kind        = kind;
            child->item        = item;
            child->label       = item.name;
            child->parent      = node;
            child->row         = static_cast<int>(node->children.size());
            child->expandable  = kind != NodeKind::Track;
            child->albumArtist = kind == NodeKind::Artist && node->category == Category::Artists;
            m_nodes.emplace(child->id, child.get());
            node->children.push_back(std::move(child));
        }
        endInsertRows();
    }

    if(node->kind == NodeKind::Category && isSearchCategory(node->category)) {
        const int count = static_cast<int>(items.size());
        switch(node->category) {
            case Category::SearchArtists:
                node->label = tr("Artists (%1)").arg(count);
                break;
            case Category::SearchAlbums:
                node->label = tr("Albums (%1)").arg(count);
                break;
            case Category::SearchTracks:
                node->label = tr("Tracks (%1)").arg(count);
                break;
            default:
                break;
        }
        const QModelIndex index = indexForNode(node);
        Q_EMIT dataChanged(index, index, {Qt::DisplayRole});
    }
}

void JellyfinBrowserModel::setStatus(Node* node, const QString& text, bool notify)
{
    if(notify) {
        removeChildren(node);
    }
    else {
        for(const auto& child : node->children) {
            unregisterSubtree(child.get());
        }
        node->children.clear();
    }

    auto status    = std::make_unique<Node>();
    status->id     = m_nextId++;
    status->kind   = NodeKind::Status;
    status->label  = text;
    status->parent = node;
    status->row    = 0;

    if(notify) {
        beginInsertRows(indexForNode(node), 0, 0);
    }
    m_nodes.emplace(status->id, status.get());
    node->children.push_back(std::move(status));
    if(notify) {
        endInsertRows();
    }
}

void JellyfinBrowserModel::removeChildren(Node* node)
{
    if(node->children.empty()) {
        return;
    }

    beginRemoveRows(indexForNode(node), 0, static_cast<int>(node->children.size()) - 1);
    for(const auto& child : node->children) {
        unregisterSubtree(child.get());
    }
    node->children.clear();
    endRemoveRows();
}

void JellyfinBrowserModel::unregisterSubtree(Node* node)
{
    m_nodes.erase(node->id);
    for(const auto& child : node->children) {
        unregisterSubtree(child.get());
    }
}

Track JellyfinBrowserModel::trackForItem(const JellyfinItem& item) const
{
    return toTrack(item, m_client->session()->credentials().server, m_client->session()->artworkSize());
}

void JellyfinBrowserModel::resolveNode(quint64 id, std::function<void(TrackList)> done)
{
    Node* node = findNode(id);
    if(!node || node->kind == NodeKind::Status || node->kind == NodeKind::Root) {
        done({});
        return;
    }
    if(node->tracks) {
        done(*node->tracks);
        return;
    }

    if(node->kind == NodeKind::Track) {
        done({trackForItem(node->item)});
        return;
    }

    // Already-loaded track rows are used as shown (this keeps e.g. a random shelf stable).
    if(node->childrenAreTracks()) {
        TrackList tracks;
        tracks.reserve(node->children.size());
        for(const auto& child : node->children) {
            tracks.push_back(trackForItem(child->item));
        }
        node->tracks = tracks;
        done(std::move(tracks));
        return;
    }

    if(node->kind == NodeKind::Category && categoryHasAlbums(node->category)) {
        // A shelf of albums: load the albums, then gather each album's tracks in order.
        fetchChildren(node, [this, id, done = std::move(done)]() mutable {
            Node* target = findNode(id);
            if(!target) {
                done({});
                return;
            }

            std::vector<quint64> albumIds;
            for(const auto& child : target->children) {
                if(child->kind == NodeKind::Album) {
                    albumIds.push_back(child->id);
                }
            }
            if(albumIds.empty()) {
                done({});
                return;
            }

            struct Gather
            {
                std::vector<TrackList> parts;
                size_t remaining{0};
                std::function<void(TrackList)> done;
            };
            auto gather       = std::make_shared<Gather>();
            gather->parts.resize(albumIds.size());
            gather->remaining = albumIds.size();
            gather->done      = std::move(done);

            for(size_t i{0}; i < albumIds.size(); ++i) {
                resolveNode(albumIds[i], [gather, i](TrackList tracks) {
                    gather->parts[i] = std::move(tracks);
                    if(--gather->remaining == 0) {
                        TrackList all;
                        for(auto& part : gather->parts) {
                            all.insert(all.end(), std::make_move_iterator(part.begin()),
                                       std::make_move_iterator(part.end()));
                        }
                        gather->done(std::move(all));
                    }
                });
            }
        });
        return;
    }

    const auto listing = trackListing(*node);
    if(!listing) {
        done({});
        return;
    }

    const int generation = m_generation;
    m_client->allItems(listing->path, listing->query, -1, this,
                       [this, id, generation, done = std::move(done)](const ItemPage& page, const QString& error) {
                           if(!error.isEmpty()) {
                               qCWarning(FOOFIN) << "Failed to fetch tracks:" << error;
                           }
                           TrackList tracks;
                           tracks.reserve(page.items.size());
                           for(const JellyfinItem& item : page.items) {
                               if(item.type == ItemType::Audio) {
                                   tracks.push_back(trackForItem(item));
                               }
                           }
                           if(generation == m_generation && error.isEmpty()) {
                               if(Node* target = findNode(id)) {
                                   target->tracks = tracks;
                               }
                           }
                           done(std::move(tracks));
                       });
}

namespace {
//! Path of rows from the root, for sorting indexes into tree order.
std::vector<int> rowPath(QModelIndex index)
{
    std::vector<int> path;
    while(index.isValid()) {
        path.push_back(index.row());
        index = index.parent();
    }
    std::ranges::reverse(path);
    return path;
}

//! Column-0 indexes in tree order, without any whose ancestor is also present.
QModelIndexList topLevelIndexes(const QModelIndexList& indexes)
{
    QModelIndexList unique;
    QSet<QModelIndex> present;
    for(const QModelIndex& index : indexes) {
        const QModelIndex first = index.siblingAtColumn(0);
        if(first.isValid() && !present.contains(first)) {
            present.insert(first);
            unique.push_back(first);
        }
    }

    QModelIndexList result;
    for(const QModelIndex& index : std::as_const(unique)) {
        bool covered{false};
        for(QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent()) {
            if(present.contains(parent)) {
                covered = true;
                break;
            }
        }
        if(!covered) {
            result.push_back(index);
        }
    }

    std::ranges::sort(result, [](const QModelIndex& a, const QModelIndex& b) { return rowPath(a) < rowPath(b); });
    return result;
}
} // namespace

void JellyfinBrowserModel::resolveTracks(const QModelIndexList& indexes, QObject* context, TracksCallback callback)
{
    std::vector<quint64> ids;
    for(const QModelIndex& index : topLevelIndexes(indexes)) {
        if(canResolve(index)) {
            ids.push_back(nodeFromIndex(index)->id);
        }
    }

    const QPointer<QObject> guard{context};
    if(ids.empty()) {
        if(guard) {
            callback({});
        }
        return;
    }

    struct Gather
    {
        std::vector<TrackList> parts;
        size_t remaining{0};
    };
    auto gather = std::make_shared<Gather>();
    gather->parts.resize(ids.size());
    gather->remaining = ids.size();

    for(size_t i{0}; i < ids.size(); ++i) {
        resolveNode(ids[i], [gather, i, guard, callback](TrackList tracks) {
            gather->parts[i] = std::move(tracks);
            if(--gather->remaining > 0 || !guard) {
                return;
            }
            TrackList all;
            for(auto& part : gather->parts) {
                all.insert(all.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
            callback(all);
        });
    }
}

std::optional<TrackList> JellyfinBrowserModel::cachedTracks(const QModelIndexList& indexes) const
{
    TrackList tracks;
    bool any{false};

    for(const QModelIndex& index : topLevelIndexes(indexes)) {
        const Node* node = nodeFromIndex(index);
        if(!node || !canResolve(index)) {
            continue;
        }
        any = true;

        if(node->kind == NodeKind::Track) {
            tracks.push_back(trackForItem(node->item));
        }
        else if(node->tracks) {
            tracks.insert(tracks.end(), node->tracks->cbegin(), node->tracks->cend());
        }
        else if(node->childrenAreTracks()) {
            for(const auto& child : node->children) {
                tracks.push_back(trackForItem(child->item));
            }
        }
        else {
            return {};
        }
    }

    if(!any) {
        return {};
    }
    return tracks;
}

TrackList JellyfinBrowserModel::siblingTracks(const QModelIndex& index, int& position) const
{
    position         = 0;
    const Node* node = nodeFromIndex(index);
    if(!node || node->kind != NodeKind::Track || !node->parent) {
        return {};
    }

    TrackList tracks;
    for(const auto& sibling : node->parent->children) {
        if(sibling->kind != NodeKind::Track) {
            continue;
        }
        if(sibling.get() == node) {
            position = static_cast<int>(tracks.size());
        }
        tracks.push_back(trackForItem(sibling->item));
    }
    return tracks;
}

NodeKind JellyfinBrowserModel::kind(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    return node ? node->kind : NodeKind::Root;
}

JellyfinItem JellyfinBrowserModel::item(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    return node ? node->item : JellyfinItem{};
}

QString JellyfinBrowserModel::label(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    return node ? displayText(*node) : QString{};
}

bool JellyfinBrowserModel::canResolve(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    if(!node) {
        return false;
    }
    switch(node->kind) {
        case NodeKind::Track:
        case NodeKind::Album:
        case NodeKind::Artist:
        case NodeKind::Playlist:
        case NodeKind::Genre:
            return true;
        case NodeKind::Category:
            return categoryHasTracks(node->category) || categoryHasAlbums(node->category);
        case NodeKind::Root:
        case NodeKind::Status:
            return false;
    }
    return false;
}

bool JellyfinBrowserModel::canFavourite(const QModelIndex& index) const
{
    switch(kind(index)) {
        case NodeKind::Track:
        case NodeKind::Album:
        case NodeKind::Artist:
        case NodeKind::Playlist:
            return true;
        default:
            return false;
    }
}

QModelIndexList JellyfinBrowserModel::autoExpandIndexes() const
{
    QModelIndexList indexes;
    if(m_search.isEmpty()) {
        return indexes;
    }
    for(const auto& child : m_root->children) {
        if(child->kind == NodeKind::Category) {
            indexes.push_back(indexForNode(child.get()));
        }
    }
    return indexes;
}

void JellyfinBrowserModel::updateFavourite(const QModelIndex& index, bool favourite)
{
    Node* node = nodeFromIndex(index);
    if(!node) {
        return;
    }
    node->item.isFavorite = favourite;
    const QModelIndex first = indexForNode(node, NameColumn);
    Q_EMIT dataChanged(first, first.siblingAtColumn(InfoColumn));
}

QString JellyfinBrowserModel::displayText(const Node& node) const
{
    switch(node.kind) {
        case NodeKind::Root:
        case NodeKind::Status:
        case NodeKind::Category:
            return node.label;
        case NodeKind::Artist:
        case NodeKind::Genre:
        case NodeKind::Playlist:
            return node.item.name;
        case NodeKind::Album: {
            const bool underArtist = node.parent && node.parent->kind == NodeKind::Artist;
            if(underArtist || node.item.albumArtist.isEmpty()) {
                return node.item.name;
            }
            return u"%1 — %2"_s.arg(node.item.name, node.item.albumArtist);
        }
        case NodeKind::Track: {
            const JellyfinItem& track = node.item;
            const bool underAlbum     = node.parent && node.parent->kind == NodeKind::Album;
            if(!underAlbum) {
                const QString artist = track.artists.join(u", "_s);
                return artist.isEmpty() ? track.name : u"%1 — %2"_s.arg(artist, track.name);
            }

            QString text;
            if(track.indexNumber > 0) {
                text = track.parentIndexNumber > 1
                         ? u"%1.%2. "_s.arg(track.parentIndexNumber).arg(track.indexNumber, 2, 10, QChar{u'0'})
                         : u"%1. "_s.arg(track.indexNumber, 2, 10, QChar{u'0'});
            }
            text += track.name;
            if(!track.artists.empty() && !sameArtists(track.artists, track.albumArtists)) {
                text += u" — "_s + track.artists.join(u", "_s);
            }
            return text;
        }
    }
    return {};
}

QString JellyfinBrowserModel::infoText(const Node& node) const
{
    if(node.loading) {
        return tr("Loading…");
    }

    switch(node.kind) {
        case NodeKind::Track:
            return node.item.runTimeTicks > 0 ? formatDuration(node.item.durationMs()) : QString{};
        case NodeKind::Album:
            return node.item.productionYear > 0 ? QString::number(node.item.productionYear) : QString{};
        case NodeKind::Playlist:
            if(node.item.childCount < 0) {
                return {};
            }
            return node.item.childCount == 1 ? tr("1 track") : tr("%1 tracks").arg(node.item.childCount);
        default:
            return {};
    }
}

QString JellyfinBrowserModel::toolTip(const Node& node) const
{
    const JellyfinItem& item = node.item;
    QStringList lines;

    switch(node.kind) {
        case NodeKind::Track: {
            lines.push_back(u"<b>%1</b>"_s.arg(item.name.toHtmlEscaped()));
            if(!item.artists.empty()) {
                lines.push_back(item.artists.join(u", "_s).toHtmlEscaped());
            }
            if(!item.album.isEmpty()) {
                lines.push_back(item.productionYear > 0
                                    ? u"%1 (%2)"_s.arg(item.album.toHtmlEscaped()).arg(item.productionYear)
                                    : item.album.toHtmlEscaped());
            }
            QStringList technical;
            if(const QString codec = displayCodec(item.codec.isEmpty() ? item.container : item.codec);
               !codec.isEmpty()) {
                technical.push_back(codec);
            }
            if(item.bitrate > 0) {
                technical.push_back(tr("%1 kbps").arg(item.bitrate / 1000));
            }
            if(item.sampleRate > 0) {
                technical.push_back(tr("%1 Hz").arg(item.sampleRate));
            }
            if(item.runTimeTicks > 0) {
                technical.push_back(formatDuration(item.durationMs()));
            }
            if(!technical.empty()) {
                lines.push_back(technical.join(u" · "_s));
            }
            if(item.playCount > 0) {
                lines.push_back(item.playCount == 1 ? tr("Played once") : tr("Played %1 times").arg(item.playCount));
            }
            break;
        }
        case NodeKind::Album:
            lines.push_back(u"<b>%1</b>"_s.arg(item.name.toHtmlEscaped()));
            if(!item.albumArtist.isEmpty()) {
                lines.push_back(item.albumArtist.toHtmlEscaped());
            }
            if(item.productionYear > 0) {
                lines.push_back(QString::number(item.productionYear));
            }
            if(!item.genres.empty()) {
                lines.push_back(item.genres.join(u", "_s).toHtmlEscaped());
            }
            break;
        case NodeKind::Artist:
        case NodeKind::Genre:
        case NodeKind::Playlist:
            lines.push_back(u"<b>%1</b>"_s.arg(item.name.toHtmlEscaped()));
            if(!item.overview.isEmpty()) {
                lines.push_back(item.overview.left(300).toHtmlEscaped());
            }
            break;
        default:
            return {};
    }

    if(item.isFavorite) {
        lines.push_back(tr("♥ Favourite"));
    }
    return lines.join(u"<br>"_s);
}

QVariant JellyfinBrowserModel::decoration(const Node& node) const
{
    auto* style = QApplication::style();

    switch(node.kind) {
        case NodeKind::Category:
            return style->standardIcon(QStyle::SP_DirIcon);
        case NodeKind::Album:
        case NodeKind::Artist:
        case NodeKind::Playlist: {
            if(m_settings->value(QLatin1StringView{Settings::BrowserCovers}).toBool()
               && !node.item.primaryImageTag.isEmpty()) {
                const QUrl url = imageUrl(m_client->session()->credentials().server, node.item.id,
                                          node.item.primaryImageTag, ThumbnailRequestSize);
                const QPixmap pixmap = m_images->image(url);
                if(!pixmap.isNull()) {
                    return QIcon{pixmap}; // an icon is scaled to the view's icon size, a pixmap is not
                }
                m_imageNodes[url.toString()].insert(node.id);
            }
            if(node.kind == NodeKind::Album) {
                return QIcon::fromTheme(u"media-optical-audio"_s, style->standardIcon(QStyle::SP_DriveCDIcon));
            }
            if(node.kind == NodeKind::Playlist) {
                return QIcon::fromTheme(u"view-media-playlist"_s, style->standardIcon(QStyle::SP_FileIcon));
            }
            return QIcon::fromTheme(u"view-media-artist"_s, style->standardIcon(QStyle::SP_DirHomeIcon));
        }
        case NodeKind::Genre:
            return QIcon::fromTheme(u"view-media-genre"_s, style->standardIcon(QStyle::SP_DirIcon));
        default:
            return {};
    }
}

QModelIndex JellyfinBrowserModel::index(int row, int column, const QModelIndex& parent) const
{
    if(column < 0 || column >= ColumnCount || row < 0) {
        return {};
    }
    const Node* parentNode = parent.isValid() ? nodeFromIndex(parent) : m_root.get();
    if(!parentNode || row >= static_cast<int>(parentNode->children.size())) {
        return {};
    }
    return createIndex(row, column, parentNode->children.at(row).get());
}

QModelIndex JellyfinBrowserModel::parent(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    if(!node || !node->parent || node->parent == m_root.get()) {
        return {};
    }
    return indexForNode(node->parent);
}

int JellyfinBrowserModel::rowCount(const QModelIndex& parent) const
{
    if(parent.column() > 0) {
        return 0;
    }
    const Node* node = parent.isValid() ? nodeFromIndex(parent) : m_root.get();
    return node ? static_cast<int>(node->children.size()) : 0;
}

int JellyfinBrowserModel::columnCount(const QModelIndex& /*parent*/) const
{
    return ColumnCount;
}

QVariant JellyfinBrowserModel::data(const QModelIndex& index, int role) const
{
    const Node* node = nodeFromIndex(index);
    if(!node) {
        return {};
    }

    const bool nameColumn = index.column() == NameColumn;

    switch(role) {
        case Qt::DisplayRole:
            return nameColumn ? displayText(*node) : infoText(*node);
        case Qt::ToolTipRole:
            return toolTip(*node);
        case Qt::DecorationRole:
            return nameColumn ? decoration(*node) : QVariant{};
        case Qt::TextAlignmentRole:
            return nameColumn ? QVariant{} : QVariant{Qt::AlignRight | Qt::AlignVCenter};
        case Qt::FontRole:
            if(node->kind == NodeKind::Status) {
                QFont font;
                font.setItalic(true);
                return font;
            }
            return {};
        case Qt::ForegroundRole:
            if(node->kind == NodeKind::Status || !nameColumn) {
                return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
            }
            return {};
        case KindRole:
            return static_cast<int>(node->kind);
        case ItemIdRole:
            return node->item.id;
        case FavouriteRole:
            return node->item.isFavorite;
        default:
            return {};
    }
}

Qt::ItemFlags JellyfinBrowserModel::flags(const QModelIndex& index) const
{
    const Node* node = nodeFromIndex(index);
    if(!node) {
        return Qt::NoItemFlags;
    }
    if(node->kind == NodeKind::Status) {
        return Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
    }

    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if(canResolve(index)) {
        flags |= Qt::ItemIsDragEnabled;
    }
    if(!node->expandable) {
        flags |= Qt::ItemNeverHasChildren;
    }
    return flags;
}

bool JellyfinBrowserModel::hasChildren(const QModelIndex& parent) const
{
    if(parent.column() > 0) {
        return false;
    }
    const Node* node = parent.isValid() ? nodeFromIndex(parent) : m_root.get();
    if(!node) {
        return false;
    }
    if(!node->children.empty()) {
        return true;
    }
    return node->expandable && !node->loaded;
}

bool JellyfinBrowserModel::canFetchMore(const QModelIndex& parent) const
{
    const Node* node = nodeFromIndex(parent);
    return node && node->expandable && !node->loaded && !node->loading;
}

void JellyfinBrowserModel::fetchMore(const QModelIndex& parent)
{
    fetchChildren(nodeFromIndex(parent));
}

QStringList JellyfinBrowserModel::mimeTypes() const
{
    return {QString::fromLatin1(Constants::Mime::Tracks)};
}

QMimeData* JellyfinBrowserModel::mimeData(const QModelIndexList& indexes) const
{
    // Drag data has to exist up front; the widget pre-resolves the selection so this usually hits.
    const auto tracks = cachedTracks(indexes);
    if(!tracks || tracks->empty()) {
        return nullptr;
    }
    return new TrackMimeData(*tracks);
}

Qt::DropActions JellyfinBrowserModel::supportedDragActions() const
{
    return Qt::CopyAction;
}
} // namespace Fooyin::Jellyfin
