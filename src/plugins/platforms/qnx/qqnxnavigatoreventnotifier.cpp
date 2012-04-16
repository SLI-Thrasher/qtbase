/***************************************************************************
**
** Copyright (C) 2011 - 2012 Research In Motion
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qqnxnavigatoreventnotifier.h"

#include "qqnxnavigatoreventhandler.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QList>
#include <QtCore/QSocketNotifier>
#include <QtCore/private/qcore_unix_p.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

static const char *navigatorControlPath = "/pps/services/navigator/control";
static const int ppsBufferSize = 4096;

QT_BEGIN_NAMESPACE

QQnxNavigatorEventNotifier::QQnxNavigatorEventNotifier(QQnxNavigatorEventHandler *eventHandler, QObject *parent)
    : QObject(parent),
      m_fd(-1),
      m_readNotifier(0),
      m_eventHandler(eventHandler)
{
}

QQnxNavigatorEventNotifier::~QQnxNavigatorEventNotifier()
{
    delete m_readNotifier;

    // close connection to navigator
    if (m_fd != -1)
        close(m_fd);

#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "QQNX: navigator event notifier stopped";
#endif
}

void QQnxNavigatorEventNotifier::start()
{
#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "QQNX: navigator event notifier started";
#endif

    // open connection to navigator
    errno = 0;
    m_fd = open(navigatorControlPath, O_RDWR);
    if (m_fd == -1) {
        qWarning("QQNX: failed to open navigator pps, errno=%d", errno);
        return;
    }

    m_readNotifier = new QSocketNotifier(m_fd, QSocketNotifier::Read);
    connect(m_readNotifier, SIGNAL(activated(int)), this, SLOT(readData()));
}

void QQnxNavigatorEventNotifier::parsePPS(const QByteArray &ppsData, QByteArray &msg, QByteArray &dat, QByteArray &id)
{
#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "PPS: data=" << ppsData;
#endif

    // tokenize pps data into lines
    QList<QByteArray> lines = ppsData.split('\n');

    // validate pps object
    if (lines.size() == 0 || lines.at(0) != "@control")
        qFatal("QQNX: unrecognized pps object, data=%s", ppsData.constData());

    // parse pps object attributes and extract values
    for (int i = 1; i < lines.size(); ++i) {

        // tokenize current attribute
        const QByteArray &attr = lines.at(i);

#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
        qDebug() << "PPS: attr=" << attr;
#endif

        int firstColon = attr.indexOf(':');
        if (firstColon == -1) {
            // abort - malformed attribute
            continue;
        }

        int secondColon = attr.indexOf(':', firstColon + 1);
        if (secondColon == -1) {
            // abort - malformed attribute
            continue;
        }

        QByteArray key = attr.left(firstColon);
        QByteArray value = attr.mid(secondColon + 1);

#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
        qDebug() << "PPS: key=" << key;
        qDebug() << "PPS: val=" << value;
#endif

        // save attribute value
        if (key == "msg")
            msg = value;
        else if (key == "dat")
            dat = value;
        else if (key == "id")
            id = value;
        else
            qFatal("QQNX: unrecognized pps attribute, attr=%s", key.constData());
    }
}

void QQnxNavigatorEventNotifier::replyPPS(const QByteArray &res, const QByteArray &id, const QByteArray &dat)
{
    // construct pps message
    QByteArray ppsData = "res::";
    ppsData += res;
    ppsData += "\nid::";
    ppsData += id;
    if (!dat.isEmpty()) {
        ppsData += "\ndat::";
        ppsData += dat;
    }
    ppsData += "\n";

#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "PPS reply=" << ppsData;
#endif

    // send pps message to navigator
    errno = 0;
    int bytes = write(m_fd, ppsData.constData(), ppsData.size());
    if (bytes == -1)
        qFatal("QQNX: failed to write navigator pps, errno=%d", errno);
}

void QQnxNavigatorEventNotifier::handleMessage(const QByteArray &msg, const QByteArray &dat, const QByteArray &id)
{
#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "PPS: msg=" << msg << ", dat=" << dat << ", id=" << id;
#endif

    // check message type
    if (msg == "orientationCheck") {
        const bool response = m_eventHandler->handleOrientationCheck(dat.toInt());

        // reply to navigator that (any) orientation is acceptable
        replyPPS(msg, id, response ? "true" : "false");
    } else if (msg == "orientation") {
        m_eventHandler->handleOrientationChange(dat.toInt());
        replyPPS(msg, id, "");
    } else if (msg == "SWIPE_DOWN") {
        m_eventHandler->handleSwipeDown();
    } else if (msg == "exit") {
        m_eventHandler->handleExit();
    }
}

void QQnxNavigatorEventNotifier::readData()
{
#if defined(QQNXNAVIGATOREVENTNOTIFIER_DEBUG)
    qDebug() << "QQNX: reading navigator data";
#endif
    // allocate buffer for pps data
    char buffer[ppsBufferSize];

    // attempt to read pps data
    errno = 0;
    int bytes = qt_safe_read(m_fd, buffer, ppsBufferSize - 1);
    if (bytes == -1)
        qFatal("QQNX: failed to read navigator pps, errno=%d", errno);

    // check if pps data was received
    if (bytes > 0) {

        // ensure data is null terminated
        buffer[bytes] = '\0';

        // process received message
        QByteArray ppsData(buffer);
        QByteArray msg;
        QByteArray dat;
        QByteArray id;
        parsePPS(ppsData, msg, dat, id);
        handleMessage(msg, dat, id);
    }
}

QT_END_NAMESPACE
