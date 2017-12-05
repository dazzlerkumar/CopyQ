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

#ifndef CLIPBOARDCLIENT_H
#define CLIPBOARDCLIENT_H

#include "app.h"
#include "client.h"

#include <QStringList>

class InputReader : public QObject
{
    Q_OBJECT

public slots:
    void readInput();

signals:
    void inputRead(const QByteArray &input);
};

/**
 * Application client.
 *
 * Sends a command to the server and exits after the command is executed.
 * Exit code is same as exit code send by ClipboardServer::sendMessage().
 * Also the received message is printed on standard output (if exit code is
 * zero) or standard error output.
 */
class ClipboardClient : public Client, public App
{
    Q_OBJECT

public:
    ClipboardClient(
            int &argc, char **argv, int skipArgc, const QString &sessionName);

private slots:
    void onMessageReceived(const QByteArray &data, int messageCode) override;

    void onDisconnected() override;

    void onConnectionFailed() override;

    void setInput(const QByteArray &input);

    void sendInput();

    void exit(int exitCode) override;

    void sendFunctionCall(const QByteArray &bytes);

    void startInputReader();

signals:
    void functionCallResultReceived(const QByteArray &returnValue);
    void inputReceived(const QByteArray &input);

private:
    void abortInputReader();
    bool isInputReaderFinished() const;
    void start(const QByteArray &scriptsData);

    QThread *m_inputReaderThread;
    QByteArray m_input;

    QStringList m_arguments;
};

#endif // CLIPBOARDCLIENT_H
