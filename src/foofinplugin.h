/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>

#include <memory>

namespace Fooyin {
class ActionManager;
class AudioLoader;
class NetworkAccessManager;
class SettingsManager;
class TrackSelectionController;

namespace Jellyfin {
class JellyfinBrowserWidget;
class JellyfinClient;
class JellyfinImageCache;
class JellyfinPlaybackReporter;
class JellyfinSession;

class FoofinPlugin : public QObject,
                     public Plugin,
                     public CorePlugin,
                     public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FOOYIN_PLUGIN_IID FILE "foofin.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    FoofinPlugin();
    ~FoofinPlugin() override;

    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;
    void shutdown() override;

private:
    [[nodiscard]] JellyfinBrowserWidget* createBrowser();

    std::shared_ptr<NetworkAccessManager> m_network;
    std::shared_ptr<AudioLoader> m_audioLoader;
    SettingsManager* m_settings{nullptr};
    std::shared_ptr<JellyfinSession> m_session;
    JellyfinClient* m_client{nullptr};
    JellyfinPlaybackReporter* m_reporter{nullptr};

    ActionManager* m_actionManager{nullptr};
    TrackSelectionController* m_trackSelection{nullptr};
    JellyfinImageCache* m_images{nullptr};
};
} // namespace Jellyfin
} // namespace Fooyin
