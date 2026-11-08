/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinbrowserwidget.h"

#include "jellyfinbrowsermodel.h"
#include "jellyfinclient.h"
#include "jellyfinplaylistsync.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"

#include <gui/guiconstants.h>
#include <gui/iconloader.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/widgetcontext.h>
#include <utils/settings/settingsmanager.h>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
constexpr int SearchDelayMs = 400;
} // namespace

namespace Fooyin::Jellyfin {
JellyfinBrowserWidget::JellyfinBrowserWidget(JellyfinClient* client, JellyfinImageCache* images,
                                             JellyfinPlaylistSync* playlistSync, ActionManager* actionManager,
                                             TrackSelectionController* trackSelection, SettingsManager* settings,
                                             QWidget* parent)
    : FyWidget{parent}
    , m_client{client}
    , m_playlistSync{playlistSync}
    , m_actionManager{actionManager}
    , m_trackSelection{trackSelection}
    , m_settings{settings}
    , m_context{new WidgetContext(
          this, Context{IdList{Constants::Context::TrackSelection, Id{"Fooyin.Context.Foofin."}.append(id())}}, this)}
    , m_search{new QLineEdit(this)}
    , m_refresh{new QToolButton(this)}
    , m_view{new QTreeView(this)}
    , m_status{new QLabel(this)}
    , m_model{new JellyfinBrowserModel(client, images, settings, this)}
    , m_searchTimer{new QTimer(this)}
{
    setObjectName(JellyfinBrowserWidget::name());

    m_search->setPlaceholderText(tr("Search Jellyfin…"));
    m_search->setClearButtonEnabled(true);

    m_refresh->setAutoRaise(true);
    m_refresh->setToolTip(tr("Reload from the server"));
    Gui::setThemeIcon(m_refresh, Constants::Icons::RescanLibrary);

    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(m_refresh);

    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setUniformRowHeights(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setDefaultDropAction(Qt::CopyAction);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setIconSize({24, 24});
    m_view->setExpandsOnDoubleClick(true);
    m_view->setAllColumnsShowFocus(true);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(JellyfinBrowserModel::NameColumn, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(JellyfinBrowserModel::InfoColumn, QHeaderView::ResizeToContents);
    m_view->installEventFilter(this);

    m_status->setWordWrap(true);
    m_status->setContentsMargins(4, 2, 4, 2);
    m_status->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addLayout(searchRow);
    layout->addWidget(m_view, 1);
    layout->addWidget(m_status);

    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SearchDelayMs);

    m_actionManager->addContextObject(m_context);

    QObject::connect(m_search, &QLineEdit::textChanged, m_searchTimer, qOverload<>(&QTimer::start));
    QObject::connect(m_search, &QLineEdit::returnPressed, this, [this]() {
        m_searchTimer->stop();
        m_model->setSearch(m_search->text());
    });
    QObject::connect(m_searchTimer, &QTimer::timeout, this, [this]() { m_model->setSearch(m_search->text()); });
    QObject::connect(m_refresh, &QToolButton::clicked, this, [this]() { m_model->reconnect(); });

    QObject::connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                     &JellyfinBrowserWidget::updateSelection);
    QObject::connect(m_view, &QTreeView::doubleClicked, this, &JellyfinBrowserWidget::activate);
    QObject::connect(m_view, &QWidget::customContextMenuRequested, this, &JellyfinBrowserWidget::showContextMenu);
    QObject::connect(m_model, &JellyfinBrowserModel::loaded, this, [this]() {
        for(const QModelIndex& index : m_model->autoExpandIndexes()) {
            m_view->expand(index);
        }
    });

    const auto session = m_client->session();
    QObject::connect(session.get(), &JellyfinSession::signedInChanged, this, [this]() {
        updateConnectionState();
        m_model->reconnect();
    });
    QObject::connect(session.get(), &JellyfinSession::libraryChanged, m_model, &JellyfinBrowserModel::reconnect);
    QObject::connect(m_client, &JellyfinClient::authenticationFailed, this, [this]() {
        m_status->setText(tr("The Jellyfin server rejected the saved login. Sign in again in Settings → Jellyfin."));
        m_status->show();
    });

    updateConnectionState();
    m_model->reload();
}

// The action manager drops m_context by itself when it is destroyed.
JellyfinBrowserWidget::~JellyfinBrowserWidget() = default;

QString JellyfinBrowserWidget::name() const
{
    return tr("Jellyfin Browser");
}

QString JellyfinBrowserWidget::layoutName() const
{
    return u"JellyfinBrowser"_s;
}

void JellyfinBrowserWidget::saveLayoutData(QJsonObject& layout)
{
    if(!m_search->text().isEmpty()) {
        layout["Search"_L1] = m_search->text();
    }
}

void JellyfinBrowserWidget::loadLayoutData(const QJsonObject& layout)
{
    const QString search = layout.value("Search"_L1).toString();
    if(!search.isEmpty()) {
        const QSignalBlocker blocker{m_search};
        m_search->setText(search);
        m_model->setSearch(search);
    }
}

bool JellyfinBrowserWidget::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == m_view && event->type() == QEvent::KeyPress) {
        const auto* keyEvent = static_cast<QKeyEvent*>(event);
        if(keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            const QModelIndex current = m_view->currentIndex().siblingAtColumn(0);
            if(!current.isValid()) {
                return true;
            }
            if(m_model->kind(current) == NodeKind::Track) {
                activate(current);
            }
            else if(m_model->canResolve(current)) {
                runAction(selectedRows(), TrackAction::Play,
                          PlaylistAction::TempPlaylist | PlaylistAction::StartPlayback);
            }
            else {
                m_view->setExpanded(current, !m_view->isExpanded(current));
            }
            return true;
        }
    }
    return FyWidget::eventFilter(watched, event);
}

QModelIndexList JellyfinBrowserWidget::selectedRows() const
{
    return m_view->selectionModel()->selectedRows(JellyfinBrowserModel::NameColumn);
}

void JellyfinBrowserWidget::updateSelection()
{
    const int request      = ++m_selectionRequest;
    const QModelIndexList rows = selectedRows();

    if(rows.empty()) {
        setSelection({});
        return;
    }

    if(const auto tracks = m_model->cachedTracks(rows)) {
        setSelection(*tracks);
        return;
    }

    // Resolve cheap selections straight away so fooyin's own actions (and dragging) work on them.
    // Genres and album shelves can be large, so those are only fetched when an action needs them.
    const bool cheap = std::ranges::all_of(rows, [this](const QModelIndex& index) {
        switch(m_model->kind(index)) {
            case NodeKind::Track:
            case NodeKind::Album:
            case NodeKind::Playlist:
            case NodeKind::Artist:
                return true;
            default:
                return false;
        }
    });
    if(!cheap) {
        setSelection({});
        return;
    }

    m_model->resolveTracks(rows, this, [this, request](const TrackList& tracks) {
        if(request == m_selectionRequest) {
            setSelection(tracks);
        }
    });
}

void JellyfinBrowserWidget::setSelection(const TrackList& tracks)
{
    TrackSelection selection;
    selection.tracks = tracks;
    m_trackSelection->changeSelectedTracks(m_context, selection);
}

void JellyfinBrowserWidget::runAction(const QModelIndexList& rows, TrackAction action,
                                      PlaylistAction::ActionOptions options, const QString& playlistName)
{
    if(rows.empty() || action == TrackAction::None) {
        return;
    }

    m_status->setText(tr("Fetching tracks…"));
    m_status->show();

    m_model->resolveTracks(rows, this, [this, action, options, playlistName](const TrackList& tracks) {
        m_status->hide();
        if(tracks.empty()) {
            m_status->setText(tr("No tracks found."));
            m_status->show();
            QTimer::singleShot(3000, m_status, &QWidget::hide);
            return;
        }
        setSelection(tracks);
        m_trackSelection->executeAction(action, options, playlistName);
    });
}

void JellyfinBrowserWidget::activate(const QModelIndex& index)
{
    if(!index.isValid() || m_model->kind(index) != NodeKind::Track) {
        // Folders expand/collapse on double-click (handled by the view).
        return;
    }

    const auto action
        = static_cast<TrackAction>(m_settings->value(QLatin1StringView{Settings::DoubleClick}).toInt());

    if(action == TrackAction::Play) {
        // Play the surrounding album/list, starting from this track.
        int position{0};
        const TrackList tracks = m_model->siblingTracks(index.siblingAtColumn(0), position);
        if(!tracks.empty()) {
            m_trackSelection->startPlayback(tracks, position);
        }
        return;
    }

    PlaylistAction::ActionOptions options;
    if(action == TrackAction::SendCurrentPlaylist || action == TrackAction::SendNewPlaylist) {
        options |= PlaylistAction::Switch | PlaylistAction::StartPlayback;
    }
    runAction({index.siblingAtColumn(0)}, action, options, m_model->label(index));
}

void JellyfinBrowserWidget::showContextMenu(const QPoint& pos)
{
    const QModelIndex clicked = m_view->indexAt(pos);
    QModelIndexList rows      = selectedRows();
    if(clicked.isValid() && !m_view->selectionModel()->isRowSelected(clicked.row(), clicked.parent())) {
        m_view->selectionModel()->select(clicked, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        rows = selectedRows();
    }

    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    const bool resolvable = !rows.empty() && std::ranges::any_of(rows, [this](const QModelIndex& index) {
        return m_model->canResolve(index);
    });

    if(resolvable) {
        const QString playlistName = rows.size() == 1 ? m_model->label(rows.front()) : tr("Jellyfin");

        auto* play = menu->addAction(tr("&Play"), this, [this, rows]() {
            runAction(rows, TrackAction::Play, PlaylistAction::TempPlaylist | PlaylistAction::StartPlayback);
        });
        Gui::setThemeIcon(play, Constants::Icons::Play);

        menu->addAction(tr("&Add to current playlist"), this,
                        [this, rows]() { runAction(rows, TrackAction::AddCurrentPlaylist); });
        menu->addAction(tr("&Replace current playlist"), this, [this, rows]() {
            runAction(rows, TrackAction::SendCurrentPlaylist,
                      PlaylistAction::Switch | PlaylistAction::StartPlayback);
        });
        menu->addAction(tr("Send to &new playlist"), this, [this, rows, playlistName]() {
            runAction(rows, TrackAction::SendNewPlaylist, PlaylistAction::Switch, playlistName);
        });
        menu->addSeparator();
        menu->addAction(tr("Add to playback &queue"), this,
                        [this, rows]() { runAction(rows, TrackAction::AddToQueue); });
        menu->addAction(tr("Add to &front of playback queue"), this,
                        [this, rows]() { runAction(rows, TrackAction::QueueNext); });
    }

    const bool favouritable = !rows.empty() && std::ranges::all_of(rows, [this](const QModelIndex& index) {
        return m_model->canFavourite(index);
    });
    if(favouritable) {
        const bool allFavourite = std::ranges::all_of(rows, [](const QModelIndex& index) {
            return index.data(JellyfinBrowserModel::FavouriteRole).toBool();
        });
        menu->addSeparator();
        auto* favourite = menu->addAction(allFavourite ? tr("Remove from &favourites") : tr("Add to &favourites"), this,
                                          [this, rows, allFavourite]() { setFavourite(rows, !allFavourite); });
        Gui::setThemeIcon(favourite, allFavourite ? Constants::Icons::Unlove : Constants::Icons::Love);
    }

    const bool playlistsClicked = clicked.isValid()
                               && (m_model->kind(clicked) == NodeKind::Playlist
                                   || m_model->category(clicked) == Category::Playlists);
    if(m_playlistSync && playlistsClicked) {
        menu->addSeparator();
        menu->addAction(tr("&Sync to fooyin playlists"), this, [this]() {
            m_settings->set(QLatin1StringView{Settings::MirrorPlaylists}, true);
            m_playlistSync->sync();
        });
    }

    if(!menu->isEmpty()) {
        menu->addSeparator();
    }
    auto* refresh = menu->addAction(clicked.isValid() && m_model->hasChildren(clicked) ? tr("Re&fresh") : tr("Re&load"),
                                    this, [this, clicked]() { m_model->refresh(clicked); });
    Gui::setThemeIcon(refresh, Constants::Icons::RescanLibrary);

    menu->popup(m_view->viewport()->mapToGlobal(pos));
}

void JellyfinBrowserWidget::setFavourite(const QModelIndexList& rows, bool favourite)
{
    for(const QModelIndex& row : rows) {
        const QString itemId = row.data(JellyfinBrowserModel::ItemIdRole).toString();
        if(itemId.isEmpty()) {
            continue;
        }
        const QPersistentModelIndex persistent{row};
        m_client->setFavourite(itemId, favourite, this, [this, persistent, favourite](const QString& error) {
            if(!error.isEmpty()) {
                m_status->setText(tr("Couldn't update favourite: %1").arg(error));
                m_status->show();
                QTimer::singleShot(5000, m_status, &QWidget::hide);
                return;
            }
            if(persistent.isValid()) {
                m_model->updateFavourite(persistent, favourite);
            }
        });
    }
}

void JellyfinBrowserWidget::updateConnectionState()
{
    const bool signedIn = m_client->session()->isSignedIn();
    m_search->setEnabled(signedIn);
    if(signedIn) {
        m_status->hide();
    }
}
} // namespace Fooyin::Jellyfin
