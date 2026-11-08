/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "jellyfinitem.h"

#include <core/track.h>

#include <QAbstractItemModel>
#include <QHash>
#include <QSet>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace Fooyin {
class SettingsManager;

namespace Jellyfin {
class JellyfinClient;
class JellyfinImageCache;

enum class NodeKind : uint8_t
{
    Root = 0,
    Category,
    Artist,
    Album,
    Playlist,
    Genre,
    Track,
    Status, //!< "Loading…", errors, "No items"
};

enum class Category : uint8_t
{
    None = 0,
    Artists,
    Albums,
    Playlists,
    Genres,
    Favourites,
    FavouriteArtists,
    FavouriteAlbums,
    FavouriteTracks,
    RecentlyAdded,
    RecentlyPlayed,
    MostPlayed,
    Random,
    SearchArtists,
    SearchAlbums,
    SearchTracks,
};

/*!
 * Lazily-populated tree of the server's music library.
 *
 * Children are requested when a node is first expanded (canFetchMore/fetchMore). Every node can
 * also be "resolved" to the list of tracks it stands for, which is what playback, playlist and
 * drag-and-drop actions use.
 */
class JellyfinBrowserModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Role
    {
        KindRole = Qt::UserRole + 1,
        ItemIdRole,
        FavouriteRole,
    };

    enum Column
    {
        NameColumn = 0,
        InfoColumn,
        ColumnCount,
    };

    using TracksCallback = std::function<void(const TrackList& tracks)>;

    JellyfinBrowserModel(JellyfinClient* client, JellyfinImageCache* images, SettingsManager* settings,
                         QObject* parent = nullptr);
    ~JellyfinBrowserModel() override;

    //! Rebuilds the tree from scratch (browse mode, or search results when a search is set).
    void reload();
    //! Like reload(), but also re-reads the user's libraries (after signing in or switching library).
    void reconnect();
    void setSearch(const QString& search);
    [[nodiscard]] QString search() const;
    //! Drops and re-requests the children of @p index.
    void refresh(const QModelIndex& index);

    [[nodiscard]] NodeKind kind(const QModelIndex& index) const;
    [[nodiscard]] Category category(const QModelIndex& index) const;
    [[nodiscard]] JellyfinItem item(const QModelIndex& index) const;
    [[nodiscard]] QString label(const QModelIndex& index) const;
    [[nodiscard]] bool canResolve(const QModelIndex& index) const;
    [[nodiscard]] bool canFavourite(const QModelIndex& index) const;
    //! Indexes that should be auto-expanded after a reload (search result groups).
    [[nodiscard]] QModelIndexList autoExpandIndexes() const;

    /*!
     * Resolves @p indexes to tracks, in order, fetching from the server where needed. Children of
     * selected parents are skipped so nothing is added twice. @p callback is not called if
     * @p context is destroyed first.
     */
    void resolveTracks(const QModelIndexList& indexes, QObject* context, TracksCallback callback);
    //! Tracks for @p indexes if every one of them has already been resolved.
    [[nodiscard]] std::optional<TrackList> cachedTracks(const QModelIndexList& indexes) const;
    //! The track rows next to @p index (same parent), and the position of @p index among them.
    [[nodiscard]] TrackList siblingTracks(const QModelIndex& index, int& position) const;

    void updateFavourite(const QModelIndex& index, bool favourite);

    // QAbstractItemModel
    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& index) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex& index) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    [[nodiscard]] QStringList mimeTypes() const override;
    [[nodiscard]] QMimeData* mimeData(const QModelIndexList& indexes) const override;
    [[nodiscard]] Qt::DropActions supportedDragActions() const override;

Q_SIGNALS:
    //! Emitted when search results (or the root) finished loading.
    void loaded();

private:
    struct Node;
    struct Listing
    {
        QString path;
        QUrlQuery query;
        NodeKind childKind{NodeKind::Track};
        int maxItems{-1};
    };

    void resetTree();
    void buildBrowseRoot();
    void buildSearchRoot();
    Node* addCategory(Node* parent, Category category, const QString& label);

    [[nodiscard]] Node* nodeFromIndex(const QModelIndex& index) const;
    [[nodiscard]] QModelIndex indexForNode(const Node* node, int column = 0) const;
    [[nodiscard]] Node* findNode(quint64 id) const;

    [[nodiscard]] std::optional<Listing> childListing(const Node& node) const;
    [[nodiscard]] std::optional<Listing> trackListing(const Node& node) const;
    [[nodiscard]] QString libraryScope() const;
    void addLibraryScope(QUrlQuery& query) const;

    void fetchChildren(Node* node, std::function<void()> whenDone = {});
    void populate(Node* node, const JellyfinItems& items, NodeKind kind, const QString& error);
    //! Replaces @p node's children with a single status row. Pass @p notify false inside a model reset.
    void setStatus(Node* node, const QString& text, bool notify = true);
    void removeChildren(Node* node);
    void unregisterSubtree(Node* node);

    void resolveNode(quint64 id, std::function<void(TrackList)> done);
    [[nodiscard]] Track trackForItem(const JellyfinItem& item) const;
    [[nodiscard]] QString displayText(const Node& node) const;
    [[nodiscard]] QString infoText(const Node& node) const;
    [[nodiscard]] QString toolTip(const Node& node) const;
    [[nodiscard]] QVariant decoration(const Node& node) const;

    JellyfinClient* m_client;
    JellyfinImageCache* m_images;
    SettingsManager* m_settings;

    std::unique_ptr<Node> m_root;
    std::unordered_map<quint64, Node*> m_nodes;
    quint64 m_nextId{1};
    int m_generation{0};

    QString m_search;
    QStringList m_musicLibraries;
    bool m_librariesLoaded{false};
    int m_librariesRequest{0}; //!< Id of the in-flight library request, 0 if none.
    int m_lastLibrariesRequest{0};

    //! Nodes waiting for a thumbnail, keyed by image URL.
    mutable QHash<QString, QSet<quint64>> m_imageNodes;
};
} // namespace Jellyfin
} // namespace Fooyin
