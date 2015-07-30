/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd. All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009, 2010 Igalia S.L
 * Copyright (C) 2014 Cable Television Laboratories, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef MediaPlayerPrivateGStreamer_h
#define MediaPlayerPrivateGStreamer_h
#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GRefPtrGStreamer.h"
#include "MediaPlayerPrivateGStreamerBase.h"
#include "Timer.h"
#include "KURL.h"

#include <glib.h>
#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>
#include <wtf/Forward.h>
#include <wtf/glib/GSourceWrap.h>

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
#include <wtf/text/AtomicStringHash.h>
#endif

#if ENABLE(MEDIA_SOURCE)
#include "MediaSourceGStreamer.h"
#endif

//#if ENABLE(ENCRYPTED_MEDIA)
#include <wtf/threads/BinarySemaphore.h>
//#endif

#include "MediaPlayer.h"

typedef struct _GstBuffer GstBuffer;
typedef struct _GstMessage GstMessage;
typedef struct _GstElement GstElement;
typedef struct _GstMpegtsSection GstMpegtsSection;

namespace WebCore {

#if ENABLE(WEB_AUDIO)
class AudioSourceProvider;
class AudioSourceProviderGStreamer;
#endif

class AudioTrackPrivateGStreamer;
class InbandMetadataTextTrackPrivateGStreamer;
class InbandTextTrackPrivateGStreamer;
class VideoTrackPrivateGStreamer;

enum SupportsType { IsNotSupported, IsSupported, MayBeSupported };

class MediaPlayerPrivateGStreamer : public MediaPlayerPrivateGStreamerBase {
public:
    explicit MediaPlayerPrivateGStreamer(MediaPlayer*);
    ~MediaPlayerPrivateGStreamer();

    static void registerMediaEngine(MediaEngineRegistrar);
    void handleSyncMessage(GstMessage*);
    gboolean handleMessage(GstMessage*);
    void handlePluginInstallerResult(GstInstallPluginsReturn);

    // TODO: implement
    virtual void paint(GraphicsContext*, const IntRect&);


    bool hasVideo() const override { return m_hasVideo; }
    bool hasAudio() const override { return m_hasAudio; }

    void load(const String &url) override;
#if ENABLE(MEDIA_SOURCE)
    void load(const String& url, MediaSourcePrivateClient*) override;
#endif
#if ENABLE(MEDIA_STREAM)
    void load(MediaStreamPrivate&) override;
#endif
    void commitLoad();
    void cancelLoad() override;

    void prepareToPlay() override;
    void play() override;
    void pause() override;

    bool paused() const override;
    bool seeking() const override;

    float duration() const override;
    float currentTime() const override;
    void seek(float) override;

    void setReadyState(MediaPlayer::ReadyState state);

    void setRate(float) override;
    //double rate() const override;
    double rate() const;
    void setPreservesPitch(bool) override;

    void setPreload(MediaPlayer::Preload) override;
    void fillTimerFired();

    //std::unique_ptr<PlatformTimeRanges> buffered() const override;
    PassOwnPtr<PlatformTimeRanges> buffered() const override;
    float maxTimeSeekable() const override;
    bool didLoadingProgress() const override;
    unsigned long long totalBytes() const override;
    float maxTimeLoaded() const override;

    void loadStateChanged();
    void timeChanged();
    void didEnd();
    void notifyDurationChanged();
    void durationChanged();
    void loadingFailed(MediaPlayer::NetworkState);

    void videoChanged();
    void videoCapsChanged();
    void audioChanged();
    void notifyPlayerOfVideo();
    void notifyPlayerOfVideoCaps();
    void notifyPlayerOfAudio();

#if ENABLE(VIDEO_TRACK)
    void textChanged();
    void notifyPlayerOfText();

    void newTextSample();
    void notifyPlayerOfNewTextSample();
#endif

    void sourceChanged();
    GstElement* audioSink() const override;
    void configurePlaySink();

    void setAudioStreamProperties(GObject*);

    void simulateAudioInterruption() override;

    bool changePipelineState(GstState);

#if ENABLE(WEB_AUDIO)
    AudioSourceProvider* audioSourceProvider() override { return reinterpret_cast<AudioSourceProvider*>(m_audioSourceProvider.get()); }
#endif

//#if ENABLE(ENCRYPTED_MEDIA)
    MediaPlayer::MediaKeyException addKey(const String&, const unsigned char*, unsigned, const unsigned char*, unsigned, const String&);
    MediaPlayer::MediaKeyException generateKeyRequest(const String&, const unsigned char*, unsigned);
    MediaPlayer::MediaKeyException cancelKeyRequest(const String&, const String&);
    void needKey(const String&, const String&, const unsigned char*, unsigned);

    void signalDRM();
//#endif

    bool isLiveStream() const override { return m_isStreaming; }
#if ENABLE(MEDIA_SOURCE)
    void notifyAppendComplete();
#endif

#if ENABLE(ENCRYPTED_MEDIA_V2)
    void needKey(RefPtr<Uint8Array> initData);
    void setCDMSession(CDMSession*);
    void keyAdded();
#endif

private:
    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const MediaEngineSupportParameters&);

    static bool isAvailable();
    static bool supportsKeySystem(const String& keySystem, const String& mimeType);

    GstElement* createAudioSink() override;

#if ENABLE(ENCRYPTED_MEDIA_V2)
    static MediaPlayer::SupportsType extendedSupportsType(const MediaEngineSupportParameters&);
    PassOwnPtr<CDMSession> createSession(const String&);
    CDMSession* m_cdmSession;
#endif

    float playbackPosition() const;

    void cacheDuration();
    void updateStates();
    void asyncStateChangeDone();

    void createGSTPlayBin();

    bool loadNextLocation();
    void mediaLocationChanged(GstMessage*);

    void setDownloadBuffering();
    void processBufferingStats(GstMessage*);
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
    void processMpegTsSection(GstMpegtsSection*);
#endif
#if ENABLE(VIDEO_TRACK)
    void processTableOfContents(GstMessage*);
    void processTableOfContentsEntry(GstTocEntry*, GstTocEntry* parent);
#endif
    bool doSeek(gint64 position, float rate, GstSeekFlags seekType);
    void updatePlaybackRate();


    String engineDescription() const override { return "GStreamer"; }
    bool didPassCORSAccessCheck() const override;
    //bool canSaveMediaData() const override;
    bool canSaveMediaData() const;

#if ENABLE(MEDIA_SOURCE)
    // TODO: Implement
    //unsigned long totalVideoFrames() override { return 0; }
    unsigned long totalVideoFrames() { return 0; }
    //unsigned long droppedVideoFrames() override { return 0; }
    unsigned long droppedVideoFrames() { return 0; }
    //unsigned long corruptedVideoFrames() override { return 0; }
    unsigned long corruptedVideoFrames() { return 0; }
    //MediaTime totalFrameDelay() override { return MediaTime::zeroTime(); }
    MediaTime totalFrameDelay() { return MediaTime::zeroTime(); }
    GRefPtr<GstCaps> currentDemuxerCaps() const override;
#endif

private:
    static void callNeedKey(MediaPlayerPrivateGStreamer* pInstance, const char* keySystemId, const unsigned char * data, unsigned size);

    GRefPtr<GstElement> m_source;
#if ENABLE(VIDEO_TRACK)
    GRefPtr<GstElement> m_textAppSink;
    GRefPtr<GstPad> m_textAppSinkPad;
#endif
    float m_seekTime;
    bool m_changingRate;
    float m_endTime;
    mutable bool m_isStreaming;
    GstStructure* m_mediaLocations;
    int m_mediaLocationCurrentIndex;
    bool m_resetPipeline;
    bool m_paused;
    bool m_playbackRatePause;
    bool m_seeking;
    bool m_seekIsPending;
    float m_timeOfOverlappingSeek;
    bool m_canFallBackToLastFinishedSeekPosition;
    bool m_buffering;
    float m_playbackRate;
    float m_lastPlaybackRate;
    bool m_errorOccured;
    mutable gfloat m_mediaDuration;
    bool m_downloadFinished;
    TimerNew m_fillTimer;
    float m_maxTimeLoaded;
    int m_bufferingPercentage;
    MediaPlayer::Preload m_preload;
    bool m_delayingLoad;
    bool m_mediaDurationKnown;
    mutable float m_maxTimeLoadedAtLastDidLoadingProgress;
    bool m_volumeAndMuteInitialized;
    bool m_hasVideo;
    bool m_hasAudio;
    GSourceWrap::Static m_audioTimerHandler;
    GSourceWrap::Static m_textTimerHandler;
    GSourceWrap::Static m_videoTimerHandler;
    GSourceWrap::Static m_videoCapsTimerHandler;
    GSourceWrap::Static m_readyTimerHandler;
    mutable unsigned long long m_totalBytes;
    KURL m_url;
    bool m_preservesPitch;
    mutable float m_cachedPosition;
    mutable double m_lastQuery;
#if ENABLE(WEB_AUDIO)
    std::unique_ptr<AudioSourceProviderGStreamer> m_audioSourceProvider;
#endif
    GstState m_requestedState;
    GRefPtr<GstElement> m_autoAudioSink;
    bool m_missingPlugins;
#if ENABLE(VIDEO_TRACK)
    Vector<RefPtr<AudioTrackPrivateGStreamer>> m_audioTracks;
    Vector<RefPtr<InbandTextTrackPrivateGStreamer>> m_textTracks;
    Vector<RefPtr<VideoTrackPrivateGStreamer>> m_videoTracks;
    RefPtr<InbandMetadataTextTrackPrivateGStreamer> m_chaptersTrack;
#endif
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
    HashMap<AtomicString, RefPtr<InbandMetadataTextTrackPrivateGStreamer>> m_metadataTracks;
#endif
#if ENABLE(MEDIA_SOURCE)
    RefPtr<MediaSourcePrivateClient> m_mediaSource;
    bool isMediaSource() const { return m_mediaSource; }
#else
    bool isMediaSource() const { return false; }
#endif
#if USE(GSTREAMER_GL)
    GstGLContext* m_glContext;
    GstGLDisplay* m_glDisplay;
#endif
//#if ENABLE(ENCRYPTED_MEDIA)
    BinarySemaphore m_drmKeySemaphore;
//#endif
    Mutex m_pendingAsyncOperationsLock;
    GList* m_pendingAsyncOperations;
};
}

#endif // USE(GSTREAMER)
#endif
