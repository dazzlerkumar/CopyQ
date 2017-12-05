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

#include "persistentdisplayitem.h"

#include "item/itemdelegate.h"

#include <QCoreApplication>
#include <QThread>

PersistentDisplayItem::PersistentDisplayItem(
        ItemDelegate *delegate,
        const QVariantMap &data,
        QObject *widget)
    : m_data(data)
    , m_widget(widget)
    , m_delegate(delegate)
{
}

bool PersistentDisplayItem::isValid() const
{
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    return !m_widget.isNull();
}

void PersistentDisplayItem::setData(const QVariantMap &data) const
{
    if ( isValid() && m_delegate && data != m_data )
        m_delegate->updateCache(m_widget, data);
}
