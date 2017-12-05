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

#include "client.h"

#include "common/clientsocket.h"
#include "platform/platformnativeinterface.h"

#include <QCoreApplication>
#include <QDataStream>

Client::Client(QObject *parent)
    : QObject(parent)
    , m_socket(nullptr)
{
}

void Client::sendMessage(const QByteArray &message, int messageCode)
{
    m_socket->sendMessage(message, messageCode);
}

void Client::waitForReadyRead()
{
    m_socket->waitForReadyRead();
}

void Client::startClientSocket(const QString &serverName)
{
    m_socket = new ClientSocket(serverName, this);

    connect( m_socket, SIGNAL(messageReceived(QByteArray,int,ClientSocket*)),
             this, SLOT(onMessageReceived(QByteArray,int)), Qt::QueuedConnection );
    connect( m_socket, SIGNAL(disconnected(ClientSocket*)),
             this, SLOT(onDisconnected()) );
    connect( m_socket, SIGNAL(connectionFailed(ClientSocket*)),
             this, SLOT(onConnectionFailed()) );

    m_socket->start();
}
