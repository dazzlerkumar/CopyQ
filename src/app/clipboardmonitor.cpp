/*
    Copyright (c) 2017, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "clipboardmonitor.h"

#include "common/common.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/monitormessagecode.h"
#include "item/serialize.h"
#include "platform/platformclipboard.h"
#include "platform/platformwindow.h"

#include <QApplication>

namespace {

bool hasSameData(const QVariantMap &data, const QVariantMap &lastData)
{
    for (auto it = lastData.constBegin(); it != lastData.constEnd(); ++it) {
        const auto &format = it.key();
        if ( !format.startsWith(COPYQ_MIME_PREFIX)
             && !data.contains(format) )
        {
            return false;
        }
    }

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        const auto &format = it.key();
        if ( !format.startsWith(COPYQ_MIME_PREFIX)
             && !data[format].toByteArray().isEmpty()
             && data[format] != lastData.value(format) )
        {
            return false;
        }
    }

    return true;
}

} // namespace

ClipboardMonitor::ClipboardMonitor(int &argc, char **argv, const QString &serverName, const QString &sessionName)
    : Client()
    , App("Monitor", createPlatformNativeInterface()->createMonitorApplication(argc, argv),
          sessionName)
    , m_clipboard(createPlatformNativeInterface()->clipboard())
{
    restoreSettings();

#ifdef HAS_TESTS
    if ( serverName == QString("copyq_TEST") )
        qApp->setProperty("CopyQ_testing", true);
#endif

    startClientSocket(serverName);

    initSingleShotTimer( &m_timerSetNewClipboard, 0, this, SLOT(setNewClipboard()) );
}

void ClipboardMonitor::onClipboardChanged(PlatformClipboard::Mode mode)
{
    QVariantMap data = m_clipboard->data(mode, m_formats);
    QVariantMap &lastData = m_lastData[mode];

    if ( hasSameData(data, lastData) ) {
        COPYQ_LOG( QString("Ignoring unchanged %1")
                   .arg(mode == PlatformClipboard::Clipboard ? "clipboard" : "selection") );
        return;
    }

    COPYQ_LOG( QString("%1 changed")
               .arg(mode == PlatformClipboard::Clipboard ? "Clipboard" : "Selection") );

    if (mode != PlatformClipboard::Clipboard) {
        const QString modeName = mode == PlatformClipboard::Selection
                ? "selection"
                : "find buffer";
        data.insert(mimeClipboardMode, modeName);
    }

    // add window title of clipboard owner
    if ( !data.contains(mimeOwner) && !data.contains(mimeWindowTitle) ) {
        PlatformPtr platform = createPlatformNativeInterface();
        PlatformWindowPtr currentWindow = platform->getCurrentWindow();
        if (currentWindow)
            data.insert( mimeWindowTitle, currentWindow->getTitle().toUtf8() );
    }

    sendMessage( serializeData(data), MonitorClipboardChanged );
    lastData = data;
}

void ClipboardMonitor::onMessageReceived(const QByteArray &message, int messageCode)
{
    if (messageCode == MonitorPing) {
        sendMessage( QByteArray(), MonitorPong );
    } else if (messageCode == MonitorSettings) {
        QVariantMap settings;
        QDataStream stream(message);
        stream >> settings;

        if ( hasLogLevel(LogDebug) ) {
            COPYQ_LOG("Loading configuration:");
            for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
                const auto &key = it.key();
                const auto &val = it.value();
                const QString str = val.canConvert<QStringList>() ? val.toStringList().join(",")
                                                                  : val.toString();
                COPYQ_LOG( QString(" %1=%2").arg(key, str) );
            }
        }

        if ( settings.contains("formats") )
            m_formats = settings["formats"].toStringList();

        connect( m_clipboard.get(), SIGNAL(changed(PlatformClipboard::Mode)),
                 this, SLOT(onClipboardChanged(PlatformClipboard::Mode)),
                 Qt::UniqueConnection );

        m_clipboard->loadSettings(settings);

        COPYQ_LOG("Configured");
    } else if (messageCode == MonitorChangeClipboard
            || messageCode == MonitorChangeSelection)
    {
        COPYQ_LOG( QString("Received change %1 request (%2 KiB)")
                   .arg(messageCode == MonitorChangeClipboard ? "clipboard" : "selection")
                   .arg(message.size() / 1024.0) );

        QVariantMap data;
        deserializeData(&data, message);
        // Set clipboard after processing all messages and returning to event loop which is safer.
        if (messageCode == MonitorChangeClipboard)
            m_newData.insert(PlatformClipboard::Clipboard, data);
        if (messageCode == MonitorChangeSelection)
            m_newData.insert(PlatformClipboard::Selection, data);
        m_timerSetNewClipboard.start();
    } else {
        log( QString("Unknown message code %1!").arg(messageCode), LogError );
    }
}

void ClipboardMonitor::onDisconnected()
{
    exit(0);
}

void ClipboardMonitor::onConnectionFailed()
{
    exit(1);
}

void ClipboardMonitor::setNewClipboard()
{
    if ( m_timerSetNewClipboard.isActive() )
        return;

    setNewClipboard(PlatformClipboard::Clipboard);
    setNewClipboard(PlatformClipboard::Selection);
}

void ClipboardMonitor::setNewClipboard(PlatformClipboard::Mode mode)
{
    if ( m_newData.contains(mode) )
        m_clipboard->setData( mode, m_newData.take(mode) );
}
