/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gui/fywidget.h>
#include <gui/trackselectioncontroller.h>

#include <QModelIndex>

class QLabel;
class QLineEdit;
class QTimer;
class QToolButton;
class QTreeView;

namespace Fooyin {
class ActionManager;
class SettingsManager;
class WidgetContext;

namespace Jellyfin {
class JellyfinBrowserModel;
class JellyfinClient;
class JellyfinImageCache;
class JellyfinPlaylistSync;

//! Layout widget (and standalone window) for browsing a Jellyfin music library.
class JellyfinBrowserWidget : public FyWidget
{
    Q_OBJECT

public:
    JellyfinBrowserWidget(JellyfinClient* client, JellyfinImageCache* images, JellyfinPlaylistSync* playlistSync,
                          ActionManager* actionManager, TrackSelectionController* trackSelection,
                          SettingsManager* settings, QWidget* parent = nullptr);
    ~JellyfinBrowserWidget() override;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] QString layoutName() const override;

    void saveLayoutData(QJsonObject& layout) override;
    void loadLayoutData(const QJsonObject& layout) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QModelIndexList selectedRows() const;
    void updateSelection();
    void setSelection(const TrackList& tracks);
    void runAction(const QModelIndexList& rows, TrackAction action, PlaylistAction::ActionOptions options = {},
                   const QString& playlistName = {});
    void activate(const QModelIndex& index);
    void showContextMenu(const QPoint& pos);
    void setFavourite(const QModelIndexList& rows, bool favourite);
    void updateConnectionState();

    JellyfinClient* m_client;
    JellyfinPlaylistSync* m_playlistSync;
    ActionManager* m_actionManager;
    TrackSelectionController* m_trackSelection;
    SettingsManager* m_settings;
    WidgetContext* m_context;

    QLineEdit* m_search;
    QToolButton* m_refresh;
    QTreeView* m_view;
    QLabel* m_status;
    JellyfinBrowserModel* m_model;
    QTimer* m_searchTimer;

    int m_selectionRequest{0};
};
} // namespace Jellyfin
} // namespace Fooyin
