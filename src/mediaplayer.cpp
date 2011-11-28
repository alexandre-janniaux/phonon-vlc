/*
    Copyright (C) 2011 Harald Sitter <sitter@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "mediaplayer.h"

#include <QtCore/QDebug>
#include <QtCore/QMetaType>
#include <QtCore/QString>

#include <vlc/libvlc_version.h>

#include "libvlc.h"
#include "media.h"

#define P_EMIT_HAS_VIDEO(hasVideo) \
    QMetaObject::invokeMethod(\
        that, "hasVideoChanged", \
        Qt::QueuedConnection, \
        Q_ARG(bool, hasVideo))

#define P_EMIT_STATE(__state) \
    QMetaObject::invokeMethod(\
        that, "stateChanged", \
        Qt::QueuedConnection, \
        Q_ARG(MediaPlayer::State, __state))

namespace Phonon {
namespace VLC {

MediaPlayer::MediaPlayer(QObject *parent) :
    QObject(parent),
    m_player(libvlc_media_player_new(libvlc)),
    m_bufferCache(-1)
{
    Q_ASSERT(m_player);

    qRegisterMetaType<MediaPlayer::State>("MediaPlayer::State");

    libvlc_event_manager_t *manager = libvlc_media_player_event_manager(m_player);
    libvlc_event_type_t events[] = {
        libvlc_MediaPlayerMediaChanged,
        libvlc_MediaPlayerNothingSpecial,
        libvlc_MediaPlayerOpening,
        libvlc_MediaPlayerBuffering,
        libvlc_MediaPlayerPlaying,
        libvlc_MediaPlayerPaused,
        libvlc_MediaPlayerStopped,
        libvlc_MediaPlayerForward,
        libvlc_MediaPlayerBackward,
        libvlc_MediaPlayerEndReached,
        libvlc_MediaPlayerEncounteredError,
        libvlc_MediaPlayerTimeChanged,
        libvlc_MediaPlayerPositionChanged,
        libvlc_MediaPlayerSeekableChanged,
        libvlc_MediaPlayerPausableChanged,
        libvlc_MediaPlayerTitleChanged,
        libvlc_MediaPlayerSnapshotTaken,
        libvlc_MediaPlayerLengthChanged
    #if (LIBVLC_VERSION_INT >= LIBVLC_VERSION(1, 2, 0, 0))
        , libvlc_MediaPlayerVout
    #endif // VLC >= 1.2
    };
    const int eventCount = sizeof(events) / sizeof(*events);
    for (int i = 0; i < eventCount; ++i) {
        libvlc_event_attach(manager, events[i], event_cb, this);
    }
}

MediaPlayer::~MediaPlayer()
{
#ifdef __GNUC__
#warning unloadmedia ... see MO
#endif
    libvlc_media_player_stop(m_player); // ensure that we are stopped.
    libvlc_media_player_release(m_player);
}

void MediaPlayer::setMedia(Media *media)
{
    m_media = media;
    libvlc_media_player_set_media(m_player, *m_media);
}

bool MediaPlayer::play()
{
    return libvlc_media_player_play(m_player) == 0;
}

void MediaPlayer::pause()
{
    libvlc_media_player_set_pause(m_player, 1);
}

void MediaPlayer::resume()
{
    libvlc_media_player_set_pause(m_player, 0);
}

void MediaPlayer::togglePause()
{
    libvlc_media_player_pause(m_player);
}

void MediaPlayer::stop()
{
    libvlc_media_player_stop(m_player);
}

qint64 MediaPlayer::time() const
{
    return libvlc_media_player_get_time(m_player);
}

void MediaPlayer::setTime(qint64 newTime)
{
    libvlc_media_player_set_time(m_player, newTime);
}

bool MediaPlayer::isSeekable() const
{
    return libvlc_media_player_is_seekable(m_player);
}

bool MediaPlayer::hasVideoOutput() const
{
    return libvlc_media_player_has_vout(m_player) > 0;
}

bool MediaPlayer::setSubtitle(int subtitle)
{
    return libvlc_video_set_spu(m_player, subtitle) == 0;
}

bool MediaPlayer::setSubtitle(const QString &file)
{
    return libvlc_video_set_subtitle_file(m_player, file.toAscii().data()) == 0;
}

void MediaPlayer::setTitle(int title)
{
    libvlc_media_player_set_title(m_player, title);
}

void MediaPlayer::setChapter(int chapter)
{
    libvlc_media_player_set_chapter(m_player, chapter);
}

bool MediaPlayer::setAudioTrack(int track)
{
    return libvlc_audio_set_track(m_player, track) == 0;
}

void MediaPlayer::event_cb(const libvlc_event_t *event, void *opaque)
{
    MediaPlayer *that = reinterpret_cast<MediaPlayer *>(opaque);
    Q_ASSERT(that);

    // Do not forget to register for the events you want to handle here!
    switch (event->type) {
    case libvlc_MediaPlayerTimeChanged:
        QMetaObject::invokeMethod(
                    that, "timeChanged",
                    Qt::QueuedConnection,
                    Q_ARG(qint64, event->u.media_player_time_changed.new_time));
        break;
    case libvlc_MediaPlayerSeekableChanged:
        QMetaObject::invokeMethod(
                    that, "seekableChanged",
                    Qt::QueuedConnection,
                    Q_ARG(bool, event->u.media_player_seekable_changed.new_seekable));
        break;
    case libvlc_MediaPlayerLengthChanged:
        QMetaObject::invokeMethod(
                    that, "lengthChanged",
                    Qt::QueuedConnection,
                    Q_ARG(qint64, event->u.media_player_length_changed.new_length));
        break;
    case libvlc_MediaPlayerNothingSpecial:
        P_EMIT_STATE(NoState);
        break;
    case libvlc_MediaPlayerOpening:
        P_EMIT_STATE(OpeningState);
        break;
    case libvlc_MediaPlayerBuffering:
        // We need to only process the buffering event in >= 1.2 as the fact
        // that no explicit switch to Playing is sent would lock us into
        // buffering with no chance of ever getting back to playing (well, unless
        // there is a playing event obviously).
#if (LIBVLC_VERSION_INT >= LIBVLC_VERSION(1, 2, 0, 0))
        // LibVLC <= 1.2 (possibly greater) does not explicitly switch to playing
        // once 100 % cache was reached. So we need to work around this by fake
        // emitting a playingstate event whereas really it was buffering :S
        // http://trac.videolan.org/vlc/ticket/5277
        that->m_bufferCache = event->u.media_player_buffering.new_cache;
        if (that->m_bufferCache < 100)
            P_EMIT_STATE(BufferingState);
        else
            P_EMIT_STATE(PlayingState);
#endif // VLC >= 1.2
        break;
    case libvlc_MediaPlayerPlaying:
        P_EMIT_STATE(PlayingState);
        break;
    case libvlc_MediaPlayerPaused:
        P_EMIT_STATE(PausedState);
        break;
    case libvlc_MediaPlayerStopped:
        P_EMIT_STATE(StoppedState);
        break;
    case libvlc_MediaPlayerEndReached:
        P_EMIT_STATE(EndedState);
        break;
    case libvlc_MediaPlayerEncounteredError:
        P_EMIT_STATE(ErrorState);
        break;
#ifdef __GNUC__
#warning bump dep to 1.2 once released
#endif
#if (LIBVLC_VERSION_INT >= LIBVLC_VERSION(1, 2, 0, 0))
    case libvlc_MediaPlayerVout:
        if (event->u.media_player_vout.new_count > 0)
            P_EMIT_HAS_VIDEO(true);
        else
            P_EMIT_HAS_VIDEO(false);
        break;
#endif // VLC >= 1.2
    case libvlc_MediaPlayerMediaChanged:
        break;
    case libvlc_MediaPlayerForward:
    case libvlc_MediaPlayerBackward:
    case libvlc_MediaPlayerPositionChanged:
    case libvlc_MediaPlayerPausableChanged:
    case libvlc_MediaPlayerTitleChanged:
    case libvlc_MediaPlayerSnapshotTaken:
    default:
        break;
        QString msg = QString("Unknown event: ") + QString(libvlc_event_type_name(event->type));
        Q_ASSERT_X(false, "event_cb", qPrintable(msg));
        break;
    }
}

QDebug operator<<(QDebug dbg, const MediaPlayer::State &s)
{
    QString name;
    switch (s) {
    case MediaPlayer::NoState:
        name = QLatin1String("MediaPlayer::NoState");
        break;
    case MediaPlayer::OpeningState:
        name = QLatin1String("MediaPlayer::OpeningState");
        break;
    case MediaPlayer::BufferingState:
        name = QLatin1String("MediaPlayer::BufferingState");
        break;
    case MediaPlayer::PlayingState:
        name = QLatin1String("MediaPlayer::PlayingState");
        break;
    case MediaPlayer::PausedState:
        name = QLatin1String("MediaPlayer::PausedState");
        break;
    case MediaPlayer::StoppedState:
        name = QLatin1String("MediaPlayer::StoppedState");
        break;
    case MediaPlayer::EndedState:
        name = QLatin1String("MediaPlayer::EndedState");
        break;
    case MediaPlayer::ErrorState:
        name = QLatin1String("MediaPlayer::ErrorState");
        break;
    }
    dbg.nospace() << "State(" << qPrintable(name) << ")";
    return dbg.space();
}

} // namespace VLC
} // namespace Phonon