/*
    SPDX-FileCopyrightText: 2021 David Redondo <kde@david-redondo.de>
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "datasource.h"

namespace KWin
{
namespace Xwl
{
void XwlDataSource::requestData(const QString &mimeType, qint32 fd)
{
    Q_EMIT dataRequested(mimeType, fd);
}

void XwlDataSource::cancel()
{
}

QStringList XwlDataSource::mimeTypes() const
{
    return m_mimeTypes;
}
void XwlDataSource::setMimeTypes(const QStringList &mimeTypes)
{
    m_mimeTypes = mimeTypes;
}
}
}
