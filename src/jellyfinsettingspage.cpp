/*
 * Foofin - Jellyfin plugin for fooyin
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "jellyfinsettingspage.h"

#include "jellyfinclient.h"
#include "jellyfinsession.h"
#include "jellyfinsettings.h"
#include "jellyfintrack.h"

#include <gui/trackselectioncontroller.h>
#include <utils/settings/settingsmanager.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace Fooyin::Jellyfin {
namespace {
class JellyfinSettingsPageWidget : public SettingsPageWidget
{
    Q_OBJECT

public:
    JellyfinSettingsPageWidget(JellyfinClient* client, SettingsManager* settings);

    void load() override;
    void apply() override;
    void reset() override;

private:
    void signIn();
    void signOut();
    void updateConnectionState();
    void loadLibraries();
    void updateFormatState();
    void setStatus(const QString& text, bool error = false);

    JellyfinClient* m_client;
    SettingsManager* m_settings;

    QLineEdit* m_server;
    QLineEdit* m_username;
    QLineEdit* m_password;
    QPushButton* m_signIn;
    QPushButton* m_signOut;
    QLabel* m_status;

    QComboBox* m_library;
    QPushButton* m_refreshLibraries;

    QComboBox* m_format;
    QSpinBox* m_maxBitrate;
    QLabel* m_formatNote;
    QCheckBox* m_reportPlayback;

    QCheckBox* m_browserCovers;
    QSpinBox* m_artworkSize;
    QComboBox* m_doubleClick;

    bool m_busy{false};
};

JellyfinSettingsPageWidget::JellyfinSettingsPageWidget(JellyfinClient* client, SettingsManager* settings)
    : m_client{client}
    , m_settings{settings}
    , m_server{new QLineEdit(this)}
    , m_username{new QLineEdit(this)}
    , m_password{new QLineEdit(this)}
    , m_signIn{new QPushButton(tr("Sign In"), this)}
    , m_signOut{new QPushButton(tr("Sign Out"), this)}
    , m_status{new QLabel(this)}
    , m_library{new QComboBox(this)}
    , m_refreshLibraries{new QPushButton(tr("Refresh"), this)}
    , m_format{new QComboBox(this)}
    , m_maxBitrate{new QSpinBox(this)}
    , m_formatNote{new QLabel(this)}
    , m_reportPlayback{new QCheckBox(tr("Report playback to the server (now playing, play counts)"), this)}
    , m_browserCovers{new QCheckBox(tr("Show artwork in the browser"), this)}
    , m_artworkSize{new QSpinBox(this)}
    , m_doubleClick{new QComboBox(this)}
{
    m_server->setPlaceholderText(u"http://192.168.1.10:8096"_s);
    m_password->setEchoMode(QLineEdit::Password);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* buttons = new QHBoxLayout();
    buttons->addWidget(m_signIn);
    buttons->addWidget(m_signOut);
    buttons->addStretch();

    auto* connectionGroup  = new QGroupBox(tr("Server"), this);
    auto* connectionLayout = new QFormLayout(connectionGroup);
    connectionLayout->addRow(tr("Address:"), m_server);
    connectionLayout->addRow(tr("Username:"), m_username);
    connectionLayout->addRow(tr("Password:"), m_password);
    connectionLayout->addRow(buttons);
    connectionLayout->addRow(m_status);

    auto* libraryRow = new QHBoxLayout();
    libraryRow->addWidget(m_library, 1);
    libraryRow->addWidget(m_refreshLibraries);

    m_format->addItem(tr("Original file (seekable)"), streamFormatKey(StreamFormat::Original));
    m_format->addItem(tr("MP3"), streamFormatKey(StreamFormat::Mp3));
    m_format->addItem(tr("Opus"), streamFormatKey(StreamFormat::Opus));
    m_format->addItem(tr("AAC"), streamFormatKey(StreamFormat::Aac));

    m_maxBitrate->setRange(32, 1536);
    m_maxBitrate->setSingleStep(32);
    m_maxBitrate->setSuffix(tr(" kbps"));

    m_formatNote->setWordWrap(true);
    m_formatNote->setText(tr("Transcoded streams are converted on the fly by the server and can't be seeked."));

    auto* playbackGroup  = new QGroupBox(tr("Playback"), this);
    auto* playbackLayout = new QFormLayout(playbackGroup);
    playbackLayout->addRow(tr("Stream format:"), m_format);
    playbackLayout->addRow(tr("Maximum bitrate:"), m_maxBitrate);
    playbackLayout->addRow(m_formatNote);
    playbackLayout->addRow(m_reportPlayback);

    m_artworkSize->setRange(100, 3000);
    m_artworkSize->setSingleStep(100);
    m_artworkSize->setSuffix(tr(" px"));
    m_artworkSize->setToolTip(tr("Size of the cover art fooyin downloads for Jellyfin tracks."));

    m_doubleClick->addItem(tr("Play from this track"), static_cast<int>(TrackAction::Play));
    TrackSelectionController::addStandardActions(m_doubleClick);

    auto* browserGroup  = new QGroupBox(tr("Browser"), this);
    auto* browserLayout = new QFormLayout(browserGroup);
    browserLayout->addRow(tr("Library:"), libraryRow);
    browserLayout->addRow(m_browserCovers);
    browserLayout->addRow(tr("Cover art size:"), m_artworkSize);
    browserLayout->addRow(tr("Double-click on a track:"), m_doubleClick);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(connectionGroup);
    layout->addWidget(playbackGroup);
    layout->addWidget(browserGroup);
    layout->addStretch();

    QObject::connect(m_signIn, &QPushButton::clicked, this, &JellyfinSettingsPageWidget::signIn);
    QObject::connect(m_password, &QLineEdit::returnPressed, this, &JellyfinSettingsPageWidget::signIn);
    QObject::connect(m_signOut, &QPushButton::clicked, this, &JellyfinSettingsPageWidget::signOut);
    QObject::connect(m_refreshLibraries, &QPushButton::clicked, this, &JellyfinSettingsPageWidget::loadLibraries);
    QObject::connect(m_format, &QComboBox::currentIndexChanged, this, &JellyfinSettingsPageWidget::updateFormatState);
}

void JellyfinSettingsPageWidget::load()
{
    const Credentials credentials = m_client->session()->credentials();
    m_server->setText(credentials.server.toString());
    m_username->setText(credentials.username);
    m_password->clear();

    const QString format = m_settings->value(QLatin1StringView{Settings::StreamFormat}).toString();
    m_format->setCurrentIndex(std::max(0, m_format->findData(format)));
    m_maxBitrate->setValue(m_settings->value(QLatin1StringView{Settings::MaxBitrate}).toInt());
    m_reportPlayback->setChecked(m_settings->value(QLatin1StringView{Settings::ReportPlayback}).toBool());
    m_browserCovers->setChecked(m_settings->value(QLatin1StringView{Settings::BrowserCovers}).toBool());
    m_artworkSize->setValue(m_settings->value(QLatin1StringView{Settings::ArtworkSize}).toInt());
    m_doubleClick->setCurrentIndex(
        std::max(0, m_doubleClick->findData(m_settings->value(QLatin1StringView{Settings::DoubleClick}).toInt())));

    updateFormatState();
    updateConnectionState();
    loadLibraries();
}

void JellyfinSettingsPageWidget::apply()
{
    m_settings->set(QLatin1StringView{Settings::StreamFormat}, m_format->currentData().toString());
    m_settings->set(QLatin1StringView{Settings::MaxBitrate}, m_maxBitrate->value());
    m_settings->set(QLatin1StringView{Settings::ReportPlayback}, m_reportPlayback->isChecked());
    m_settings->set(QLatin1StringView{Settings::BrowserCovers}, m_browserCovers->isChecked());
    m_settings->set(QLatin1StringView{Settings::ArtworkSize}, m_artworkSize->value());
    m_settings->set(QLatin1StringView{Settings::DoubleClick}, m_doubleClick->currentData().toInt());
    if(m_library->isEnabled()) {
        m_settings->set(QLatin1StringView{Settings::LibraryId}, m_library->currentData().toString());
    }
}

void JellyfinSettingsPageWidget::reset()
{
    for(const char* key : {Settings::StreamFormat, Settings::MaxBitrate, Settings::ReportPlayback,
                           Settings::BrowserCovers, Settings::ArtworkSize, Settings::DoubleClick, Settings::LibraryId}) {
        m_settings->reset(QLatin1StringView{key});
    }
}

void JellyfinSettingsPageWidget::signIn()
{
    if(m_busy) {
        return;
    }
    if(m_server->text().trimmed().isEmpty() || m_username->text().trimmed().isEmpty()) {
        setStatus(tr("Enter the server address and your username."), true);
        return;
    }

    m_busy = true;
    updateConnectionState();
    setStatus(tr("Connecting…"));

    m_client->signIn(m_server->text(), m_username->text().trimmed(), m_password->text(), this,
                     [this](const SignInResult& result, const QString& error) {
                         m_busy = false;
                         if(!error.isEmpty()) {
                             updateConnectionState();
                             setStatus(error, true);
                             return;
                         }
                         m_password->clear();
                         m_server->setText(result.server.toString());
                         m_client->session()->signIn(result);
                         updateConnectionState();
                         loadLibraries();
                     });
}

void JellyfinSettingsPageWidget::signOut()
{
    m_client->signOut();
    m_library->clear();
    updateConnectionState();
}

void JellyfinSettingsPageWidget::updateConnectionState()
{
    const Credentials credentials = m_client->session()->credentials();
    const bool signedIn           = credentials.isValid();

    m_server->setEnabled(!m_busy);
    m_username->setEnabled(!m_busy);
    m_password->setEnabled(!m_busy);
    m_signIn->setEnabled(!m_busy);
    m_signIn->setText(signedIn ? tr("Sign In Again") : tr("Sign In"));
    m_signOut->setEnabled(signedIn && !m_busy);
    m_library->setEnabled(signedIn && m_library->count() > 0);
    m_refreshLibraries->setEnabled(signedIn);

    if(m_busy) {
        return;
    }
    if(signedIn) {
        const QString server = credentials.serverName.isEmpty() ? credentials.server.host() : credentials.serverName;
        setStatus(tr("Signed in as <b>%1</b> on <b>%2</b>.")
                      .arg(credentials.username.toHtmlEscaped(), server.toHtmlEscaped()));
    }
    else {
        setStatus(tr("Not signed in."));
    }
}

void JellyfinSettingsPageWidget::loadLibraries()
{
    if(!m_client->session()->isSignedIn()) {
        m_library->clear();
        updateConnectionState();
        return;
    }

    m_client->musicLibraries(this, [this](const JellyfinItems& libraries, const QString& error) {
        const QString selected = m_settings->value(QLatin1StringView{Settings::LibraryId}).toString();

        m_library->clear();
        if(!error.isEmpty()) {
            setStatus(error, true);
            updateConnectionState();
            return;
        }

        m_library->addItem(tr("All music libraries"), QString{});
        for(const JellyfinItem& library : libraries) {
            m_library->addItem(library.name, library.id);
        }
        m_library->setCurrentIndex(std::max(0, m_library->findData(selected)));
        updateConnectionState();
    });
}

void JellyfinSettingsPageWidget::updateFormatState()
{
    const bool transcoding = m_format->currentData().toString() != streamFormatKey(StreamFormat::Original);
    m_maxBitrate->setEnabled(transcoding);
    m_formatNote->setVisible(transcoding);
}

void JellyfinSettingsPageWidget::setStatus(const QString& text, bool error)
{
    m_status->setText(error ? u"<span style=\"color:#c0392b\">%1</span>"_s.arg(text.toHtmlEscaped()) : text);
}
} // namespace

JellyfinSettingsPage::JellyfinSettingsPage(JellyfinClient* client, SettingsManager* settings, QObject* parent)
    : SettingsPage{settings->settingsDialog(), parent}
{
    setId(SettingsPageId);
    setName(tr("Server"));
    setCategory({tr("Jellyfin")});
    setSearchKeywords({u"jellyfin"_s, u"foofin"_s, u"server"_s, u"stream"_s});
    setWidgetCreator([client, settings] { return new JellyfinSettingsPageWidget(client, settings); });
}
} // namespace Fooyin::Jellyfin

#include "jellyfinsettingspage.moc"
#include "moc_jellyfinsettingspage.cpp"
