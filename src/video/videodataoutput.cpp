/*  libVLC backend for the Phonon library
    Copyright (C) 2006 Matthias Kretz <kretz@kde.org>
    Copyright (C) 2009 Martin Sandsmark <sandsmark@samfundet.no>
    Copyright (C) 2010 Harald Sitter <apachelogger@ubuntu.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), Nokia Corporation
    (or its successors, if any) and the KDE Free Qt Foundation, which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "videodataoutput.h"

#include <phonon/medianode.h>
#include <phonon/audiooutput.h>
#include <phonon/experimental/abstractvideodataoutput.h>

#include <QMetaObject>

#include "utils/debug.h"
#include "mediaobject.h"

using namespace Phonon::Experimental;

namespace Phonon
{
namespace VLC
{

VideoDataOutput::VideoDataOutput(QObject *parent)
    : QObject(parent)
    , m_frontend(0)
{
}

VideoDataOutput::~VideoDataOutput()
{
}

void VideoDataOutput::connectToMediaObject(MediaObject *mediaObject)
{
    SinkNode::connectToMediaObject(mediaObject);
    setCallbacks(m_player);
}

void VideoDataOutput::disconnectFromMediaObject(MediaObject *mediaObject)
{
    unsetCallbacks(m_player);
    SinkNode::disconnectFromMediaObject(mediaObject);
}

Experimental::AbstractVideoDataOutput *VideoDataOutput::frontendObject() const
{
    return m_frontend;
}

void VideoDataOutput::setFrontendObject(Experimental::AbstractVideoDataOutput *frontend)
{
    m_frontend = frontend;
}

void *VideoDataOutput::lockCallback(void **planes)
{
    m_mutex.lock();
    DEBUG_BLOCK;
    planes[0] = reinterpret_cast<void *>(m_frame.data0.data());
    planes[1] = reinterpret_cast<void *>(m_frame.data1.data());
    planes[2] = reinterpret_cast<void *>(m_frame.data2.data());
    return 0;
}

void VideoDataOutput::unlockCallback(void *picture, void *const*planes)
{
    DEBUG_BLOCK;

    // For some reason VLC yields BGR24, so we swap it to RGB
    if (m_frame.format == Experimental::VideoFrame2::Format_RGB888) {
        uchar *data = (uchar *) m_frame.data0.data();
        uchar tmp;
        for (int i = 0; i < m_frame.data0.size(); i += 3) {
            tmp = data[i];
            data[i] = data[i+2];
            data[i+2] = tmp;
        }
    }

    if (m_frontend)
        m_frontend->frameReady(m_frame);

    m_mutex.unlock();
}

void VideoDataOutput::displayCallback(void *picture)
{
    DEBUG_BLOCK;
    // We send the frame while unlocking as we could loose syncing otherwise.
    // With VDO the consumer is expected to ensure syncness while not blocking
    // unlock for long periods of time. Good luck with that... -.-
}

static VideoFrame2::Format fourccToFormat(const char *fourcc)
{
    if (strcmp(fourcc, "RV24"))
        return VideoFrame2::Format_RGB888;
    else if (strcmp(fourcc, "RV32"))
        return VideoFrame2::Format_RGB32;
    else if (strcmp(fourcc, "YV12"))
        return VideoFrame2::Format_YV12;
    else // No clue what YUV2 is in VLC
        return VideoFrame2::Format_Invalid;
}

static const vlc_chroma_description_t *setFormat(VideoFrame2::Format format, char **chroma)
{
    switch (format) {
    case VideoFrame2::Format_Invalid:
        *chroma = 0;
        return 0;
    case VideoFrame2::Format_RGB32:
        debug() << "32";
        qstrcpy(*chroma, "RV32");
        return vlc_fourcc_GetChromaDescription(VLC_CODEC_RGB32);
    case VideoFrame2::Format_RGB888:
        debug() << "24";
        qstrcpy(*chroma, "RV24");
        return vlc_fourcc_GetChromaDescription(VLC_CODEC_RGB24);
    case VideoFrame2::Format_YV12:
        debug() << "12";
        qstrcpy(*chroma, "YV12");
        return vlc_fourcc_GetChromaDescription(VLC_CODEC_YV12);
    }
    return 0;
}

unsigned VideoDataOutput::formatCallback(char *chroma,
                                         unsigned *width, unsigned *height,
                                         unsigned *pitches, unsigned *lines)
{
    DEBUG_BLOCK;

    m_frame.width = *width;
    m_frame.height = *height;

    const vlc_chroma_description_t *chromaDesc = 0;

    QSet<VideoFrame2::Format> allowedFormats = m_frontend->allowedFormats();
    VideoFrame2::Format suggestedFormat = fourccToFormat(chroma);
    if (suggestedFormat != VideoFrame2::Format_Invalid
            && allowedFormats.contains(suggestedFormat)) { // Use suggested
        debug() << "suggested:" << chroma;
        chromaDesc = setFormat(suggestedFormat, &chroma);
        m_frame.format = suggestedFormat;
    } else { // Pick first and use that
        debug() << "picking" << allowedFormats;
        foreach (VideoFrame2::Format format, allowedFormats) {
            chromaDesc = setFormat(format, &chroma);
            if (chroma) {
                m_frame.format = format;
                break;
            }
        }
    }

    qDebug() << "USING" << chroma;

    Q_ASSERT(chromaDesc);

    unsigned int bufferSize = setPitchAndLines(chromaDesc, *width, *height, pitches, lines);

    m_frame.data0.resize(pitches[0] * lines[0]);
    m_frame.data1.resize(pitches[1] * lines[1]);
    m_frame.data2.resize(pitches[2] * lines[0]);

    return bufferSize;
}

void VideoDataOutput::formatCleanUpCallback()
{
    DEBUG_BLOCK;
}

} // namespace VLC
} // namespace Phonon
