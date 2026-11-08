/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "foofinplugin.h"

#include "jellyfinbrowserwidget.h"
#include "jellyfinclient.h"
#include "jellyfinimagecache.h"
#include "jellyfinplaybackreporter.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfinsettingspage.h"
#include "jellyfinsourceprovider.h"

#include <core/engine/audioloader.h>
#include <core/network/networkaccessmanager.h>
#include <gui/guiconstants.h>
#include <gui/widgetprovider.h>
#include <utils/actions/actioncontainer.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>

#include <QAction>

using namespace Qt::StringLiterals;

namespace Fooyin::Jellyfin {
FoofinPlugin::FoofinPlugin()  = default;
FoofinPlugin::~FoofinPlugin() = default;

void FoofinPlugin::initialise(const CorePluginContext& context)
{
    m_network  = context.networkAccess;
    m_settings = context.settingsManager;

    registerSettings(m_settings);

    // Shared with the stream provider, which fooyin's AudioLoader keeps (and calls from the engine thread).
    m_session = std::make_shared<JellyfinSession>(m_settings, context.dbPool);
    m_client  = new JellyfinClient(m_network, m_session, this);

    m_audioLoader = context.audioLoader;
    if(m_audioLoader) {
        installSourceProvider(*m_audioLoader, m_network, m_settings, m_session);
    }

    m_reporter = new JellyfinPlaybackReporter(m_client, context.playerController, m_settings, this);

    QObject::connect(m_client, &JellyfinClient::authenticationFailed, this,
                     []() { qCWarning(FOOFIN) << "Jellyfin rejected the stored access token; sign in again"; });

    const Credentials credentials = m_session->credentials();
    if(credentials.isValid()) {
        qCInfo(FOOFIN) << "Using Jellyfin server" << credentials.server.toString() << "as" << credentials.username;
    }
}

void FoofinPlugin::initialise(const GuiPluginContext& context)
{
    m_actionManager  = context.actionManager;
    m_trackSelection = context.trackSelection;
    m_images         = new JellyfinImageCache(m_network, this);

    new JellyfinSettingsPage(m_client, m_settings, this);

    context.widgetProvider->registerWidget(u"JellyfinBrowser"_s, [this]() { return createBrowser(); },
                                           tr("Jellyfin Browser"));
    context.widgetProvider->setSubMenus(u"JellyfinBrowser"_s, {tr("Internet")});

    auto* showBrowser = new QAction(tr("&Jellyfin Browser"), this);
    showBrowser->setStatusTip(tr("Browse and play music from your Jellyfin server"));
    auto* command = m_actionManager->registerAction(showBrowser, "Foofin.ShowBrowser");
    command->setCategories({tr("View")});
    m_actionManager->actionContainer(Constants::Menus::View)->addAction(command);

    QObject::connect(showBrowser, &QAction::triggered, this, [this]() {
        auto* browser = createBrowser();
        browser->showStandaloneWindow(tr("Jellyfin"), u"Foofin/WindowState"_s, QSize{520, 640});
    });
}

void FoofinPlugin::shutdown()
{
    if(m_reporter) {
        m_reporter->finish();
    }

    // fooyin unloads plugin libraries right after shutdown(), but the AudioLoader outlives them.
    // Release the provider (whose code lives in this library) while we are still loaded.
    if(m_audioLoader) {
        m_audioLoader->setRemoteSourceProvider(nullptr);
        m_audioLoader.reset();
    }
}

JellyfinBrowserWidget* FoofinPlugin::createBrowser()
{
    return new JellyfinBrowserWidget(m_client, m_images, m_actionManager, m_trackSelection, m_settings);
}
} // namespace Fooyin::Jellyfin

#include "moc_foofinplugin.cpp"
