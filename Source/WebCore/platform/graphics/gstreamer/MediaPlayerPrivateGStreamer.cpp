/*
 * Copyright (C) 2007, 2009 Apple Inc.  All rights reserved.
 * Copyright (C) 2007 Collabora Ltd.  All rights reserved.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Gustavo Noronha Silva <gns@gnome.org>
 * Copyright (C) 2009, 2010, 2011, 2012, 2013 Igalia S.L
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

#include "config.h"
#include "MediaPlayerPrivateGStreamer.h"

#if ENABLE(VIDEO) && USE(GSTREAMER)

#include "GStreamerUtilities.h"
//#include "URL.h"
#include "MIMETypeRegistry.h"
#include "MediaPlayer.h"
#include "NotImplemented.h"
#include "SecurityOrigin.h"
#include "TimeRanges.h"
#include "UUID.h"
#include "WebKitWebSourceGStreamer.h"
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <limits>
#include <wtf/CurrentTime.h>
#include <wtf/HexNumber.h>
#include <wtf/MediaTime.h>
//#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>

#if ENABLE(VIDEO_TRACK)
#include "AudioTrackPrivateGStreamer.h"
#include "InbandMetadataTextTrackPrivateGStreamer.h"
#include "InbandTextTrackPrivateGStreamer.h"
#include "TextCombinerGStreamer.h"
#include "TextSinkGStreamer.h"
#include "VideoTrackPrivateGStreamer.h"
#endif

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
#define GST_USE_UNSTABLE_API
#include <gst/mpegts/mpegts.h>
#undef GST_USE_UNSTABLE_API
#endif
#include <gst/audio/streamvolume.h>

#if ENABLE(ENCRYPTED_MEDIA)
#include "WebKitCommonEncryptionDecryptorGStreamer.h"
#endif

#if ENABLE(MEDIA_SOURCE)
#include "MediaSource.h"
#include "WebKitMediaSourceGStreamer.h"
#endif

#if ENABLE(WEB_AUDIO)
//#include "AudioSourceProviderGStreamer.h"
#endif

#if ENABLE(ENCRYPTED_MEDIA_V2) && USE(DXDRM)
#include "CDMPRSessionGStreamer.h"
#include "WebKitPlayReadyDecryptorGStreamer.h"
#endif

#if USE(GSTREAMER_GL)
#include "GLContext.h"

#define GST_USE_UNSTABLE_API
#include <gst/gl/gl.h>
#undef GST_USE_UNSTABLE_API

#if USE(GLX)
#include "GLContextGLX.h"
#include <gst/gl/x11/gstgldisplay_x11.h>
#elif USE(EGL)
#include "GLContextEGL.h"
#include <gst/gl/egl/gstgldisplay_egl.h>
#endif

// gstglapi.h may include eglplatform.h and it includes X.h, which
// defines None, breaking MediaPlayer::None enum
#if PLATFORM(X11) && GST_GL_HAVE_PLATFORM_EGL
#undef None
#endif
#endif // USE(GSTREAMER_GL)

#include <condition_variable>
#include <mutex>
#include <wtf/MainThread.h>

static void callOnMainThreadAndWait(std::function<void ()> function)
{
    if (isMainThread()) {
        function();
        return;
    }

    std::mutex mutex;
    std::condition_variable conditionVariable;

    bool isFinished = false;

    callOnMainThread([&] {
        function();

        std::lock_guard<std::mutex> lock(mutex);
        isFinished = true;
        conditionVariable.notify_one();
    });

    std::unique_lock<std::mutex> lock(mutex);
    conditionVariable.wait(lock, [&] { return isFinished; });
}

// Max interval in seconds to stay in the READY state on manual
// state change requests.
static const unsigned gReadyStateTimerInterval = 60;

GST_DEBUG_CATEGORY_EXTERN(webkit_media_player_debug);
#define GST_CAT_DEFAULT webkit_media_player_debug

using namespace std;

namespace WebCore {

static gboolean mediaPlayerPrivateMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamer* player)
{
    return player->handleMessage(message);
}

static void mediaPlayerPrivateSyncMessageCallback(GstBus*, GstMessage* message, MediaPlayerPrivateGStreamer* player)
{
    player->handleSyncMessage(message);
}

static void mediaPlayerPrivateSourceChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamer* player)
{
    player->sourceChanged();
}

static void mediaPlayerPrivateVideoSinkCapsChangedCallback(GObject*, GParamSpec*, MediaPlayerPrivateGStreamer* player)
{
    player->videoCapsChanged();
}

static void mediaPlayerPrivateVideoChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->videoChanged();
}

static void mediaPlayerPrivateAudioChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->audioChanged();
}

static void setAudioStreamPropertiesCallback(GstChildProxy*, GObject* object, gchar*,
    MediaPlayerPrivateGStreamer* player)
{
    player->setAudioStreamProperties(object);
}

#if ENABLE(VIDEO_TRACK)
static void mediaPlayerPrivateTextChangedCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->textChanged();
}

static GstFlowReturn mediaPlayerPrivateNewTextSampleCallback(GObject*, MediaPlayerPrivateGStreamer* player)
{
    player->newTextSample();
    return GST_FLOW_OK;
}
#endif

static gboolean mediaPlayerPrivateNotifyDurationChanged(MediaPlayerPrivateGStreamer* player)
{
    player->notifyDurationChanged();
    return G_SOURCE_REMOVE;
}

static void mediaPlayerPrivatePluginInstallerResultFunction(GstInstallPluginsReturn result, gpointer userData)
{
    MediaPlayerPrivateGStreamer* player = reinterpret_cast<MediaPlayerPrivateGStreamer*>(userData);
    player->handlePluginInstallerResult(result);
}

void MediaPlayerPrivateGStreamer::setAudioStreamProperties(GObject* object)
{
    if (g_strcmp0(G_OBJECT_TYPE_NAME(object), "GstPulseSink"))
        return;

    const char* role = m_player->client().mediaPlayerIsVideo() ? "video" : "music";
    GstStructure* structure = gst_structure_new("stream-properties", "media.role", G_TYPE_STRING, role, NULL);
    g_object_set(object, "stream-properties", structure, NULL);
    gst_structure_free(structure);
    GUniquePtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(object)));
    LOG_MEDIA_MESSAGE("Set media.role as %s at %s", role, elementName.get());
}

void MediaPlayerPrivateGStreamer::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
#if ENABLE(ENCRYPTED_MEDIA_V2)
        registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateGStreamer>(player); },
            getSupportedTypes, extendedSupportsType, 0, 0, 0, supportsKeySystem);
#else
         registrar([](MediaPlayer* player) { return std::make_unique<MediaPlayerPrivateGStreamer>(player); },
            getSupportedTypes, supportsType, 0, 0, 0, supportsKeySystem);
#endif
}

bool initializeGStreamerAndRegisterWebKitElements()
{
    if (!initializeGStreamer())
        return false;

    GRefPtr<GstElementFactory> srcFactory = gst_element_factory_find("webkitwebsrc");
    if (!srcFactory) {
        GST_DEBUG_CATEGORY_INIT(webkit_media_player_debug, "webkitmediaplayer", 0, "WebKit media player");
        gst_element_register(0, "webkitwebsrc", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_WEB_SRC);
    }

#if ENABLE(ENCRYPTED_MEDIA)
    GRefPtr<GstElementFactory> cencDecryptorFactory = gst_element_factory_find("webkitcencdec");
    if (!cencDecryptorFactory)
        gst_element_register(0, "webkitcencdec", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_CENC_DECRYPT);
#endif

#if USE(DXDRM)
    GRefPtr<GstElementFactory> playReadyDecryptorFactory = gst_element_factory_find("webkitplayreadydec");
    if (!playReadyDecryptorFactory)
        gst_element_register(0, "webkitplayreadydec", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_PLAYREADY_DECRYPT);
#endif

#if ENABLE(MEDIA_SOURCE)
    GRefPtr<GstElementFactory> WebKitMediaSrcFactory = gst_element_factory_find("webkitmediasrc");
    if (!WebKitMediaSrcFactory)
        gst_element_register(0, "webkitmediasrc", GST_RANK_PRIMARY + 100, WEBKIT_TYPE_MEDIA_SRC);
#endif
    return true;
}

bool MediaPlayerPrivateGStreamer::isAvailable()
{
    if (!initializeGStreamerAndRegisterWebKitElements())
        return false;

    GRefPtr<GstElementFactory> factory = gst_element_factory_find("playbin");
    return factory;
}

MediaPlayerPrivateGStreamer::MediaPlayerPrivateGStreamer(MediaPlayer* player)
    : MediaPlayerPrivateGStreamerBase(player)
    , m_source(0)
    , m_seekTime(0)
    , m_changingRate(false)
    , m_endTime(numeric_limits<float>::infinity())
    , m_isStreaming(false)
    , m_mediaLocations(0)
    , m_mediaLocationCurrentIndex(0)
    , m_resetPipeline(false)
    , m_paused(true)
    , m_playbackRatePause(false)
    , m_seeking(false)
    , m_seekIsPending(false)
    , m_timeOfOverlappingSeek(-1)
    , m_canFallBackToLastFinishedSeekPosition(false)
    , m_buffering(false)
    , m_playbackRate(1)
    , m_lastPlaybackRate(1)
    , m_errorOccured(false)
    , m_mediaDuration(0)
    , m_downloadFinished(false)
    , m_fillTimer(*this, &MediaPlayerPrivateGStreamer::fillTimerFired)
    , m_maxTimeLoaded(0)
    , m_bufferingPercentage(0)
    , m_preload(player->preload())
    , m_delayingLoad(false)
    , m_mediaDurationKnown(true)
    , m_maxTimeLoadedAtLastDidLoadingProgress(0)
    , m_volumeAndMuteInitialized(false)
    , m_hasVideo(false)
    , m_hasAudio(false)
    , m_audioTimerHandler("[WebKit] MediaPlayerPrivateGStreamer::audioChanged", std::bind(&MediaPlayerPrivateGStreamer::notifyPlayerOfAudio, this))
    , m_textTimerHandler("[WebKit] MediaPlayerPrivateGStreamer::textChanged", std::bind(&MediaPlayerPrivateGStreamer::notifyPlayerOfText, this))
    , m_videoTimerHandler("[WebKit] MediaPlayerPrivateGStreamer::videoChanged", std::bind(&MediaPlayerPrivateGStreamer::notifyPlayerOfVideo, this))
    , m_videoCapsTimerHandler("[WebKit] MediaPlayerPrivateGStreamer::videoCapsChanged", std::bind(&MediaPlayerPrivateGStreamer::notifyPlayerOfVideoCaps, this))
    , m_readyTimerHandler("[WebKit] mediaPlayerPrivateReadyStateTimeoutCallback", [this] { changePipelineState(GST_STATE_NULL); })
    , m_totalBytes(0)
    , m_preservesPitch(false)
    , m_cachedPosition(-1)
    , m_lastQuery(-1)
#if ENABLE(WEB_AUDIO)
    , m_audioSourceProvider(std::make_unique<AudioSourceProviderGStreamer>())
#endif
    , m_requestedState(GST_STATE_VOID_PENDING)
    , m_missingPlugins(false)
    , m_pendingAsyncOperations(nullptr)
{
}

MediaPlayerPrivateGStreamer::~MediaPlayerPrivateGStreamer()
{
#if ENABLE(ENCRYPTED_MEDIA)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal();
#endif

#if ENABLE(VIDEO_TRACK)
    for (size_t i = 0; i < m_audioTracks.size(); ++i)
        m_audioTracks[i]->disconnect();

    for (size_t i = 0; i < m_textTracks.size(); ++i)
        m_textTracks[i]->disconnect();

    for (size_t i = 0; i < m_videoTracks.size(); ++i)
        m_videoTracks[i]->disconnect();
#endif
    if (m_fillTimer.isActive())
        m_fillTimer.stop();

    if (m_mediaLocations) {
        gst_structure_free(m_mediaLocations);
        m_mediaLocations = 0;
    }

    if (m_autoAudioSink)
        g_signal_handlers_disconnect_by_func(G_OBJECT(m_autoAudioSink.get()),
            reinterpret_cast<gpointer>(setAudioStreamPropertiesCallback), this);

    m_readyTimerHandler.cancel();

#if ENABLE(MEDIA_SOURCE)
    if (m_source && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_source.get()), 0);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_source.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);
    }
#endif

    if (m_pipeline) {
        GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
        ASSERT(bus);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateMessageCallback), this);
        g_signal_handlers_disconnect_by_func(bus.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateSyncMessageCallback), this);
        gst_bus_remove_signal_watch(bus.get());

        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateSourceChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateAudioChangedCallback), this);
#if ENABLE(VIDEO_TRACK)
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateNewTextSampleCallback), this);
        g_signal_handlers_disconnect_by_func(m_pipeline.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateTextChangedCallback), this);
#endif

        gst_element_set_state(m_pipeline.get(), GST_STATE_NULL);
    }

    if (m_videoSink) {
        GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_videoSink.get(), "sink"));
        g_signal_handlers_disconnect_by_func(videoSinkPad.get(), reinterpret_cast<gpointer>(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);
    }

    // Cancel pending mediaPlayerPrivateNotifyDurationChanged() delayed calls
    m_pendingAsyncOperationsLock.lock();
    while (m_pendingAsyncOperations) {
        g_source_remove(GPOINTER_TO_UINT(m_pendingAsyncOperations->data));
        m_pendingAsyncOperations = g_list_remove(m_pendingAsyncOperations, m_pendingAsyncOperations->data);
    }
    m_pendingAsyncOperationsLock.unlock();
}

void MediaPlayerPrivateGStreamer::load(const String& urlString)
{
    if (!initializeGStreamerAndRegisterWebKitElements())
        return;

    URL url(URL(), urlString);
    if (url.isBlankURL())
        return;

    // Clean out everything after file:// url path.
    String cleanURL(urlString);
    if (url.isLocalFile())
        cleanURL = cleanURL.substring(0, url.pathEnd());

    if (!m_pipeline)
        createGSTPlayBin();

    ASSERT(m_pipeline);

#if ENABLE(ENCRYPTED_MEDIA)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal();
#endif

    m_url = URL(URL(), cleanURL);
    g_object_set(m_pipeline.get(), "uri", cleanURL.utf8().data(), nullptr);

    INFO_MEDIA_MESSAGE("Load %s", cleanURL.utf8().data());

    if (m_preload == MediaPlayer::None) {
        LOG_MEDIA_MESSAGE("Delaying load.");
        m_delayingLoad = true;
    }

    // Reset network and ready states. Those will be set properly once
    // the pipeline pre-rolled.
    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();
    m_readyState = MediaPlayer::HaveNothing;
    m_player->readyStateChanged();
    m_volumeAndMuteInitialized = false;

    if (!m_delayingLoad)
        commitLoad();
}

#if ENABLE(MEDIA_SOURCE)
void MediaPlayerPrivateGStreamer::load(const String& url, MediaSourcePrivateClient* mediaSource)
{
    String mediasourceUri = String::format("mediasource%s", url.utf8().data());
    m_mediaSource = mediaSource;
    load(mediasourceUri);
}
#endif

#if ENABLE(MEDIA_STREAM)
void MediaPlayerPrivateGStreamer::load(MediaStreamPrivate&)
{
    notImplemented();
}
#endif

void MediaPlayerPrivateGStreamer::commitLoad()
{
    ASSERT(!m_delayingLoad);
    LOG_MEDIA_MESSAGE("Committing load.");

    // GStreamer needs to have the pipeline set to a paused state to
    // start providing anything useful.
    changePipelineState(GST_STATE_PAUSED);

    setDownloadBuffering();
    updateStates();
}

float MediaPlayerPrivateGStreamer::playbackPosition() const
{
    if (m_isEndReached) {
        // Position queries on a null pipeline return 0. If we're at
        // the end of the stream the pipeline is null but we want to
        // report either the seek time or the duration because this is
        // what the Media element spec expects us to do.
        if (m_seeking)
            return m_seekTime;
        if (m_mediaDuration)
            return m_mediaDuration;
        return 0;
    }

    double now = WTF::currentTime();
    if (m_lastQuery > -1 && ((now - m_lastQuery) < 0.25) && (m_cachedPosition > -1))
        return m_cachedPosition;

    m_lastQuery = now;

    // Position is only available if no async state change is going on and the state is either paused or playing.
    gint64 position = GST_CLOCK_TIME_NONE;
    GstQuery* query= gst_query_new_position(GST_FORMAT_TIME);
    if (gst_element_query(m_pipeline.get(), query))
        gst_query_parse_position(query, 0, &position);

    float result = 0.0f;
    if (static_cast<GstClockTime>(position) != GST_CLOCK_TIME_NONE)
        result = static_cast<double>(position) / GST_SECOND;
    else if (m_canFallBackToLastFinishedSeekPosition)
        result = m_seekTime;

    LOG_MEDIA_MESSAGE("Position %" GST_TIME_FORMAT, GST_TIME_ARGS(position));

    gst_query_unref(query);
    m_cachedPosition = result;
    return result;
}

bool MediaPlayerPrivateGStreamer::changePipelineState(GstState newState)
{
    ASSERT(m_pipeline);

    GstState currentState;
    GstState pending;

    gst_element_get_state(m_pipeline.get(), &currentState, &pending, 0);
    if (currentState == newState || pending == newState) {
        LOG_MEDIA_MESSAGE("Rejected state change to %s from %s with %s pending", gst_element_state_get_name(newState),
            gst_element_state_get_name(currentState), gst_element_state_get_name(pending));
        return true;
    }

    LOG_MEDIA_MESSAGE("Changing state change to %s from %s with %s pending", gst_element_state_get_name(newState),
        gst_element_state_get_name(currentState), gst_element_state_get_name(pending));

    GstStateChangeReturn setStateResult = gst_element_set_state(m_pipeline.get(), newState);
    GstState pausedOrPlaying = newState == GST_STATE_PLAYING ? GST_STATE_PAUSED : GST_STATE_PLAYING;
    if (currentState != pausedOrPlaying && setStateResult == GST_STATE_CHANGE_FAILURE) {
        return false;
    }

    // Create a timer when entering the READY state so that we can free resources
    // if we stay for too long on READY.
    // Also lets remove the timer if we request a state change for any state other than READY.
    // See also https://bugs.webkit.org/show_bug.cgi?id=117354
    if (newState == GST_STATE_READY && !m_readyTimerHandler.isScheduled()) {
        m_readyTimerHandler.schedule(std::chrono::seconds(gReadyStateTimerInterval));
    } else if (newState != GST_STATE_READY && m_readyTimerHandler.isScheduled()) {
        m_readyTimerHandler.cancel();
    }

    return true;
}

void MediaPlayerPrivateGStreamer::prepareToPlay()
{
    m_preload = MediaPlayer::Auto;
    if (m_delayingLoad) {
        m_delayingLoad = false;
        commitLoad();
    }
}

void MediaPlayerPrivateGStreamer::play()
{
    if (!m_playbackRate) {
        m_playbackRatePause = true;
        return;
    }

    if (changePipelineState(GST_STATE_PLAYING)) {
        m_isEndReached = false;
        m_delayingLoad = false;
        m_preload = MediaPlayer::Auto;
        setDownloadBuffering();
        LOG_MEDIA_MESSAGE("Play");
    } else {
        loadingFailed(MediaPlayer::Empty);
    }
}

void MediaPlayerPrivateGStreamer::pause()
{
    m_playbackRatePause = false;
    GstState currentState, pendingState;
    gst_element_get_state(m_pipeline.get(), &currentState, &pendingState, 0);
    if (currentState < GST_STATE_PAUSED && pendingState <= GST_STATE_PAUSED)
        return;

    if (changePipelineState(GST_STATE_PAUSED))
        INFO_MEDIA_MESSAGE("Pause");
    else
        loadingFailed(MediaPlayer::Empty);
}

float MediaPlayerPrivateGStreamer::duration() const
{
    if (!m_pipeline)
        return 0.0f;

    if (m_errorOccured)
        return 0.0f;

    // Media duration query failed already, don't attempt new useless queries.
    if (!m_mediaDurationKnown)
        return numeric_limits<float>::infinity();

    if (m_mediaDuration)
        return m_mediaDuration;

    GstFormat timeFormat = GST_FORMAT_TIME;
    gint64 timeLength = 0;

    bool failure = !gst_element_query_duration(m_pipeline.get(), timeFormat, &timeLength) || static_cast<guint64>(timeLength) == GST_CLOCK_TIME_NONE;

    if (failure && m_source)
        failure = !gst_element_query_duration(m_source.get(), timeFormat, &timeLength) || static_cast<guint64>(timeLength) == GST_CLOCK_TIME_NONE;

#if ENABLE(MEDIA_SOURCE)
    if (failure && m_mediaSource)
        return m_mediaSource->duration().toFloat();
#endif

    if (failure) {
        LOG_MEDIA_MESSAGE("Time duration query failed for %s", m_url.string().utf8().data());
        return numeric_limits<float>::infinity();
    }

    LOG_MEDIA_MESSAGE("Duration: %" GST_TIME_FORMAT, GST_TIME_ARGS(timeLength));

    m_mediaDuration = static_cast<double>(timeLength) / GST_SECOND;
    return m_mediaDuration;
    // FIXME: handle 3.14.9.5 properly
}

float MediaPlayerPrivateGStreamer::currentTime() const
{
    if (!m_pipeline)
        return 0.0f;

    if (m_errorOccured)
        return 0.0f;

    if (m_seeking)
        return m_seekTime;

    // Workaround for
    // https://bugzilla.gnome.org/show_bug.cgi?id=639941 In GStreamer
    // 0.10.35 basesink reports wrong duration in case of EOS and
    // negative playback rate. There's no upstream accepted patch for
    // this bug yet, hence this temporary workaround.
    if (m_isEndReached && m_playbackRate < 0)
        return 0.0f;

    return playbackPosition();
}

void MediaPlayerPrivateGStreamer::seek(float time)
{
    if (!m_pipeline)
        return;

    if (m_errorOccured)
        return;

    INFO_MEDIA_MESSAGE("[Seek] seek attempt to %f secs", time);

    // Avoid useless seeking.
    if (time == currentTime())
        return;

    if (isLiveStream())
        return;

    GstClockTime clockTime = toGstClockTime(time);
    INFO_MEDIA_MESSAGE("[Seek] seeking to %" GST_TIME_FORMAT " (%f)", GST_TIME_ARGS(clockTime), time);

    if (m_seeking) {
        m_timeOfOverlappingSeek = time;
        if (m_seekIsPending) {
            m_seekTime = time;
            return;
        }
    }

    GstState state;
    GstStateChangeReturn getStateResult = gst_element_get_state(m_pipeline.get(), &state, nullptr, 0);
    if (getStateResult == GST_STATE_CHANGE_FAILURE || getStateResult == GST_STATE_CHANGE_NO_PREROLL) {
        LOG_MEDIA_MESSAGE("[Seek] cannot seek, current state change is %s", gst_element_state_change_return_get_name(getStateResult));
        return;
    }
    if (getStateResult == GST_STATE_CHANGE_ASYNC || state < GST_STATE_PAUSED
#if ENABLE(MEDIA_SOURCE)
            || (isMediaSource() && webkit_media_src_is_appending(WEBKIT_MEDIA_SRC(m_source.get())))
#endif
            || m_isEndReached) {
        m_seekIsPending = true;
        if (m_isEndReached) {
            LOG_MEDIA_MESSAGE("[Seek] reset pipeline");
            m_resetPipeline = true;
            if (!changePipelineState(GST_STATE_PAUSED))
                loadingFailed(MediaPlayer::Empty);
        }
    } else {
        // We can seek now.
        if (!doSeek(clockTime, m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE))) {
            LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", time);
            return;
        }
    }

    m_seeking = true;
    m_seekTime = time;
    m_isEndReached = false;
}

bool MediaPlayerPrivateGStreamer::doSeek(gint64 position, float rate, GstSeekFlags seekType)
{
    gint64 startTime, endTime;

    if (rate > 0) {
        startTime = position;
        endTime = GST_CLOCK_TIME_NONE;
    } else {
        startTime = 0;
        // If we are at beginning of media, start from the end to
        // avoid immediate EOS.
        if (position < 0)
            endTime = static_cast<gint64>(duration() * GST_SECOND);
        else
            endTime = position;
    }

    if (!rate)
        rate = 1.0;

#if ENABLE(MEDIA_SOURCE)
    MediaTime time(MediaTime::createWithDouble(double(static_cast<double>(position) / GST_SECOND)));

    if (isMediaSource())
        webkit_media_src_set_seek_time(WEBKIT_MEDIA_SRC(m_source.get()), time);
#endif

    if (!gst_element_seek(m_pipeline.get(), rate, GST_FORMAT_TIME, seekType,
        GST_SEEK_TYPE_SET, startTime, GST_SEEK_TYPE_SET, endTime))
        return false;

#if ENABLE(MEDIA_SOURCE)
    if (isMediaSource())
        m_mediaSource->seekToTime(time);
#endif

    return true;
}

void MediaPlayerPrivateGStreamer::updatePlaybackRate()
{
    if (!m_changingRate)
        return;

    float currentPosition = static_cast<float>(playbackPosition() * GST_SECOND);
    bool mute = false;

    INFO_MEDIA_MESSAGE("Set Rate to %f", m_playbackRate);

    if (m_playbackRate > 0) {
        // Mute the sound if the playback rate is too extreme and
        // audio pitch is not adjusted.
        mute = (!m_preservesPitch && (m_playbackRate < 0.8 || m_playbackRate > 2));
    } else {
        if (currentPosition == 0.0f)
            currentPosition = -1.0f;
        mute = true;
    }

    INFO_MEDIA_MESSAGE("Need to mute audio?: %d", (int) mute);
    if (doSeek(currentPosition, m_playbackRate, static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH))) {
        g_object_set(m_pipeline.get(), "mute", mute, nullptr);
        m_lastPlaybackRate = m_playbackRate;
    } else {
        m_playbackRate = m_lastPlaybackRate;
        ERROR_MEDIA_MESSAGE("Set rate to %f failed", m_playbackRate);
    }

    if (m_playbackRatePause) {
        GstState state;
        GstState pending;

        gst_element_get_state(m_pipeline.get(), &state, &pending, 0);
        if (state != GST_STATE_PLAYING && pending != GST_STATE_PLAYING)
            changePipelineState(GST_STATE_PLAYING);
        m_playbackRatePause = false;
    }

    m_changingRate = false;
    m_player->rateChanged();
}

bool MediaPlayerPrivateGStreamer::paused() const
{
    if (m_isEndReached) {
        LOG_MEDIA_MESSAGE("Ignoring pause at EOS");
        return true;
    }

    if (m_playbackRatePause)
        return false;

    GstState state;
    gst_element_get_state(m_pipeline.get(), &state, nullptr, 0);
    return state <= GST_STATE_PAUSED;
}

bool MediaPlayerPrivateGStreamer::seeking() const
{
    return m_seeking;
}

void MediaPlayerPrivateGStreamer::videoChanged()
{
    m_videoTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamer::videoCapsChanged()
{
    m_videoCapsTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfVideo()
{
    gint numTracks = 0;
#if ENABLE(MEDIA_SOURCE)
    bool useMediaSource = false;
#endif
    if (m_pipeline) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-video", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
        g_object_get(m_pipeline.get(), "n-video", &numTracks, nullptr);
    }

    m_hasVideo = numTracks > 0;

#if ENABLE(VIDEO_TRACK)
    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_video_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
        g_signal_emit_by_name(m_pipeline.get(), "get-video-pad", i, &pad.outPtr(), nullptr);
        ASSERT(pad);

        if (i < static_cast<gint>(m_videoTracks.size())) {
            RefPtr<VideoTrackPrivateGStreamer> existingTrack = m_videoTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<VideoTrackPrivateGStreamer> track = VideoTrackPrivateGStreamer::create(m_pipeline, i, pad);
        m_videoTracks.append(track);
#if ENABLE(MEDIA_SOURCE)
        if (isMediaSource()) {
            // TODO: This will leak if the event gets freed without arriving in
            // the source, e.g. when we go flushing here
            RefPtr<VideoTrackPrivateGStreamer>* trackCopy = new RefPtr<VideoTrackPrivateGStreamer>(track);
            GstStructure* videoEventStructure = gst_structure_new("webKitVideoTrack", "track", G_TYPE_POINTER, trackCopy, nullptr);
            if (useMediaSource) {
                // Using the stream->demuxersrcpad. The GstUriDecodeBin pad
                // may not exist at this point. Using an alternate way to
                // notify the upper layer.
                webkit_media_src_track_added(WEBKIT_MEDIA_SRC(m_source.get()), pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, videoEventStructure));
            } else {
                // Using the GstUriDecodeBin source pad
                gst_pad_push_event(pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, videoEventStructure));
            }
        }
#endif
        m_player->addVideoTrack(track.release());
    }

    while (static_cast<gint>(m_videoTracks.size()) > numTracks) {
        RefPtr<VideoTrackPrivateGStreamer> track = m_videoTracks.last();
        track->disconnect();
        m_videoTracks.removeLast();
        m_player->removeVideoTrack(track.release());
    }
#endif

    m_player->client().mediaPlayerEngineUpdated(m_player);
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfVideoCaps()
{
    m_videoSize = IntSize();
    m_player->client().mediaPlayerEngineUpdated(m_player);
}

void MediaPlayerPrivateGStreamer::audioChanged()
{
    m_audioTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfAudio()
{
    gint numTracks = 0;
#if ENABLE(MEDIA_SOURCE)
    bool useMediaSource = false;
#endif
    if (m_pipeline) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-audio", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
            g_object_get(m_pipeline.get(), "n-audio", &numTracks, NULL);
    }

    m_hasAudio = numTracks > 0;

#if ENABLE(VIDEO_TRACK)
    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_audio_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
        g_signal_emit_by_name(m_pipeline.get(), "get-audio-pad", i, &pad.outPtr(), nullptr);
        ASSERT(pad);

        if (i < static_cast<gint>(m_audioTracks.size())) {
            RefPtr<AudioTrackPrivateGStreamer> existingTrack = m_audioTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<AudioTrackPrivateGStreamer> track = AudioTrackPrivateGStreamer::create(m_pipeline, i, pad);
        m_audioTracks.insert(i, track);
#if ENABLE(MEDIA_SOURCE)
        if (isMediaSource()) {
            // TODO: This will leak if the event gets freed without arriving in
            // the source, e.g. when we go flushing here
            RefPtr<AudioTrackPrivateGStreamer>* trackCopy = new RefPtr<AudioTrackPrivateGStreamer>(track);
            GstStructure* audioEventStructure = gst_structure_new("webKitAudioTrack", "track", G_TYPE_POINTER, trackCopy, nullptr);
             if (useMediaSource) {
                 // Using the stream->demuxersrcpad. The GstUriDecodeBin pad
                 // may not exist at this point. Using an alternate way to
                 // notify the upper layer.
                 webkit_media_src_track_added(WEBKIT_MEDIA_SRC(m_source.get()), pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, audioEventStructure));
             } else {
                 // Using the GstUriDecodeBin source pad
                 gst_pad_push_event(pad.get(), gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, audioEventStructure));
             }
        }
#endif
        m_player->addAudioTrack(track.release());
    }

    while (static_cast<gint>(m_audioTracks.size()) > numTracks) {
        RefPtr<AudioTrackPrivateGStreamer> track = m_audioTracks.last();
        track->disconnect();
        m_audioTracks.removeLast();
        m_player->removeAudioTrack(track.release());
    }
#endif

    m_player->client().mediaPlayerEngineUpdated(m_player);
}

#if ENABLE(VIDEO_TRACK)
void MediaPlayerPrivateGStreamer::textChanged()
{
    m_textTimerHandler.schedule();
}

void MediaPlayerPrivateGStreamer::notifyPlayerOfText()
{
    gint numTracks = 0;
#if ENABLE(MEDIA_SOURCE)
    bool useMediaSource = false;
#endif
    if (m_pipeline) {
#if ENABLE(MEDIA_SOURCE)
        if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            g_object_get(m_source.get(), "n-text", &numTracks, NULL);
            useMediaSource = true;
        } else
#endif
        g_object_get(m_pipeline.get(), "n-text", &numTracks, nullptr);
    }

    for (gint i = 0; i < numTracks; ++i) {
        GRefPtr<GstPad> pad;
#if ENABLE(MEDIA_SOURCE)
        if (useMediaSource)
            pad = webkit_media_src_get_text_pad(WEBKIT_MEDIA_SRC(m_source.get()), i);
        else
#endif
        g_signal_emit_by_name(m_pipeline.get(), "get-text-pad", i, &pad.outPtr(), nullptr);
        ASSERT(pad);

        if (i < static_cast<gint>(m_textTracks.size())) {
            RefPtr<InbandTextTrackPrivateGStreamer> existingTrack = m_textTracks[i];
            existingTrack->setIndex(i);
            if (existingTrack->pad() == pad)
                continue;
        }

        RefPtr<InbandTextTrackPrivateGStreamer> track = InbandTextTrackPrivateGStreamer::create(i, pad);
        m_textTracks.insert(i, track);
        m_player->addTextTrack(track.release());
    }

    while (static_cast<gint>(m_textTracks.size()) > numTracks) {
        RefPtr<InbandTextTrackPrivateGStreamer> track = m_textTracks.last();
        track->disconnect();
        m_textTracks.removeLast();
        m_player->removeTextTrack(track.release());
    }
}

void MediaPlayerPrivateGStreamer::newTextSample()
{
    if (!m_textAppSink)
        return;

    GRefPtr<GstEvent> streamStartEvent = adoptGRef(
        gst_pad_get_sticky_event(m_textAppSinkPad.get(), GST_EVENT_STREAM_START, 0));

    GRefPtr<GstSample> sample;
    g_signal_emit_by_name(m_textAppSink.get(), "pull-sample", &sample.outPtr(), NULL);
    ASSERT(sample);

    if (streamStartEvent) {
        bool found = FALSE;
        const gchar* id;
        gst_event_parse_stream_start(streamStartEvent.get(), &id);
        for (size_t i = 0; i < m_textTracks.size(); ++i) {
            RefPtr<InbandTextTrackPrivateGStreamer> track = m_textTracks[i];
            if (track->streamId() == id) {
                track->handleSample(sample);
                found = true;
                break;
            }
        }
        if (!found)
            WARN_MEDIA_MESSAGE("Got sample with unknown stream ID.");
    } else
        WARN_MEDIA_MESSAGE("Unable to handle sample with no stream start event.");
}
#endif

// METRO FIXME: GStreamer mediaplayer manages the readystate on its own. We shouldn't change it manually.
void MediaPlayerPrivateGStreamer::setReadyState(MediaPlayer::ReadyState state)
{
    if (state != m_readyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed manually from %u to %u", m_readyState, state);
        m_readyState = state;
        m_player->readyStateChanged();
    }
}

void MediaPlayerPrivateGStreamer::setRate(float rate)
{
    // Higher rate causes crash.
    rate = clampTo(rate, -20.0, 20.0);

    // Avoid useless playback rate update.
    if (m_playbackRate == rate) {
        // and make sure that upper layers were notified if rate was set

        if (!m_changingRate && m_player->rate() != m_playbackRate)
            m_player->rateChanged();
        return;
    }

    if (isLiveStream()) {
        // notify upper layers that we cannot handle passed rate.
        m_changingRate = false;
        m_player->rateChanged();
        return;
    }

    GstState state;
    GstState pending;

    m_playbackRate = rate;
    m_changingRate = true;

    gst_element_get_state(m_pipeline.get(), &state, &pending, 0);

    if (!rate) {
        m_changingRate = false;
        m_playbackRatePause = true;
        if (state != GST_STATE_PAUSED && pending != GST_STATE_PAUSED)
            changePipelineState(GST_STATE_PAUSED);
        return;
    }

    if ((state != GST_STATE_PLAYING && state != GST_STATE_PAUSED)
        || (pending == GST_STATE_PAUSED))
        return;

    updatePlaybackRate();
}

double MediaPlayerPrivateGStreamer::rate() const
{
    return m_playbackRate;
}

void MediaPlayerPrivateGStreamer::setPreservesPitch(bool preservesPitch)
{
    m_preservesPitch = preservesPitch;
}

std::unique_ptr<PlatformTimeRanges> MediaPlayerPrivateGStreamer::buffered() const
{
#if ENABLE(MEDIA_SOURCE)
    if (isMediaSource())
        return m_mediaSource->buffered();
#endif

    auto timeRanges = std::make_unique<PlatformTimeRanges>();
    if (m_errorOccured || isLiveStream())
        return timeRanges;

    float mediaDuration(duration());
    if (!mediaDuration || std::isinf(mediaDuration))
        return timeRanges;

    GstQuery* query = gst_query_new_buffering(GST_FORMAT_PERCENT);

    if (!gst_element_query(m_pipeline.get(), query)) {
        gst_query_unref(query);
        return timeRanges;
    }

    guint numBufferingRanges = gst_query_get_n_buffering_ranges(query);
    for (guint index = 0; index < numBufferingRanges; index++) {
        gint64 rangeStart = 0, rangeStop = 0;
        if (gst_query_parse_nth_buffering_range(query, index, &rangeStart, &rangeStop))
            timeRanges->add(MediaTime::createWithDouble((rangeStart * mediaDuration) / GST_FORMAT_PERCENT_MAX),
                MediaTime::createWithDouble((rangeStop * mediaDuration) / GST_FORMAT_PERCENT_MAX));
    }

    // Fallback to the more general maxTimeLoaded() if no range has
    // been found.
    if (!timeRanges->length())
        if (float loaded = maxTimeLoaded())
            timeRanges->add(MediaTime::zeroTime(), MediaTime::createWithDouble(loaded));

    gst_query_unref(query);

    return timeRanges;
}

#if ENABLE(MEDIA_SOURCE)
static StreamType getStreamType(GstElement* element)
{
    g_return_val_if_fail(GST_IS_ELEMENT(element), STREAM_TYPE_UNKNOWN);

    GstIterator* it;
    GValue item = G_VALUE_INIT;
    StreamType result = STREAM_TYPE_UNKNOWN;

    it = gst_element_iterate_sink_pads(element);

    if (it && (gst_iterator_next(it, &item)) == GST_ITERATOR_OK) {
        GstPad* pad = GST_PAD(g_value_get_object(&item));
        if (pad) {
            GstCaps* caps = gst_pad_get_current_caps(pad);
            if (caps && GST_IS_CAPS(caps)) {
                const GstStructure* structure = gst_caps_get_structure(caps, 0);
                if (structure) {
                    const gchar* mediatype = gst_structure_get_name(structure);
                    if (mediatype) {
                        // Look for "audio", "video", "text"
                        switch (mediatype[0]) {
                        case 'a':
                            result = STREAM_TYPE_AUDIO;
                            break;
                        case 'v':
                            result = STREAM_TYPE_VIDEO;
                            break;
                        case 't':
                            result = STREAM_TYPE_TEXT;
                            break;
                        default:
                            break;
                        }
                    }
                }
                gst_caps_unref(caps);
            }
        }
    }

    g_value_unset(&item);

    if (it)
        gst_iterator_free(it);

    return result;
}
#endif

void MediaPlayerPrivateGStreamer::handleSyncMessage(GstMessage* message)
{
    // FIXME: Use proper formatting across USE(GSTREAMER_GL) and ENABLE(ENCRYPTED_MEDIA_V2).
    switch (GST_MESSAGE_TYPE(message)) {
#if USE(GSTREAMER_GL)
    case GST_MESSAGE_NEED_CONTEXT: {
        const gchar* contextType;
        gst_message_parse_context_type(message, &contextType);

        if (!m_glDisplay) {
#if PLATFORM(X11)
            Display* display = GLContext::sharedX11Display();
            GstGLDisplayX11* gstGLDisplay = gst_gl_display_x11_new_with_display(display);
#elif PLATFORM(WAYLAND)
            EGLDisplay display = WaylandDisplay::instance()->eglDisplay();
            GstGLDisplayEGL* gstGLDisplay = gst_gl_display_egl_new_with_egl_display(display);
#else
            return;
#endif

            m_glDisplay = reinterpret_cast<GstGLDisplay*>(gstGLDisplay);
            GLContext* webkitContext = GLContext::sharingContext();
#if USE(GLX)
            GLXContext* glxSharingContext = reinterpret_cast<GLXContext*>(webkitContext->platformContext());
            if (glxSharingContext && !m_glContext)
                m_glContext = gst_gl_context_new_wrapped(GST_GL_DISPLAY(gstGLDisplay), reinterpret_cast<guintptr>(glxSharingContext), GST_GL_PLATFORM_GLX, GST_GL_API_OPENGL);
#elif USE(EGL)
            EGLContext* eglSharingContext = reinterpret_cast<EGLContext*>(webkitContext->platformContext());
            if (eglSharingContext && !m_glContext)
                m_glContext = gst_gl_context_new_wrapped(GST_GL_DISPLAY(gstGLDisplay), reinterpret_cast<guintptr>(eglSharingContext), GST_GL_PLATFORM_EGL, GST_GL_API_GLES2);
#endif
        }

        if (!g_strcmp0(contextType, GST_GL_DISPLAY_CONTEXT_TYPE)) {
            GstContext* displayContext = gst_context_new(GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
            gst_context_set_gl_display(displayContext, m_glDisplay);
            gst_element_set_context(GST_ELEMENT(message->src), displayContext);
            return;
        }
        if (!g_strcmp0(contextType, "gst.gl.app_context")) {
            GstContext* appContext = gst_context_new("gst.gl.app_context", TRUE);
            GstStructure* structure = gst_context_writable_structure(appContext);
            gst_structure_set(structure, "context", GST_GL_TYPE_CONTEXT, m_glContext, nullptr);
            gst_element_set_context(GST_ELEMENT(message->src), appContext);
            return;
        }
        break;
    }
#endif // USE(GSTREAMER_GL)
        case GST_MESSAGE_ELEMENT:
        {
#if ENABLE(ENCRYPTED_MEDIA)
            const GstStructure* structure = gst_message_get_structure(message);
            // Here we receive the DRM init data from the pipeline: we will emit
            // the needkey event with that data and the browser might create a
            // CDMSession from this event handler. If such a session was created
            // We will emit the message event from the session to provide the
            // DRM challenge to the browser and wait for an update. If on the
            // contrary no session was created we won't wait and let the pipeline
            // error out by itself.
            if (gst_structure_has_name(structure, "drm-key-needed")) {
                GstBuffer* data;
                const char* keySystemId;
                gboolean valid = gst_structure_get(structure, "data", GST_TYPE_BUFFER, &data,
                    "key-system-id", G_TYPE_STRING, &keySystemId, nullptr);
                GstMapInfo mapInfo;
                if (!valid || !gst_buffer_map(data, &mapInfo, GST_MAP_READ))
                    break;

                GST_DEBUG("queueing keyNeeded event");
                // We need to reset the semaphore first. signal, wait
                m_drmKeySemaphore.signal();
                m_drmKeySemaphore.wait();
                // Fire the need key event from main thread
                callOnMainThreadAndWait([&] {
                    // FIXME: Provide a somehow valid sessionId.
                    needKey(keySystemId, "sessionId", reinterpret_cast<const unsigned char *>(mapInfo.data), mapInfo.size);
                });
                // Wait for a potential license
                GST_DEBUG("waiting for a license");
                m_drmKeySemaphore.wait();
                GST_DEBUG("finished waiting");
                gst_buffer_unmap(data, &mapInfo);
            }
#endif
            break;
        }
        case GST_MESSAGE_DURATION_CHANGED:
        {
            m_pendingAsyncOperationsLock.lock();
            guint asyncOperationId = g_timeout_add(0, (GSourceFunc)mediaPlayerPrivateNotifyDurationChanged, this);
            m_pendingAsyncOperations = g_list_append(m_pendingAsyncOperations, GUINT_TO_POINTER(asyncOperationId));
            m_pendingAsyncOperationsLock.unlock();
            break;
        }
        default:
            break;
    }
}

gboolean MediaPlayerPrivateGStreamer::handleMessage(GstMessage* message)
{
    GUniqueOutPtr<GError> err;
    GUniqueOutPtr<gchar> debug;
    MediaPlayer::NetworkState error;
    bool issueError = true;
    bool attemptNextLocation = false;
    const GstStructure* structure = gst_message_get_structure(message);
    GstState requestedState, currentState;

    m_canFallBackToLastFinishedSeekPosition = false;

    if (structure) {
        const gchar* messageTypeName = gst_structure_get_name(structure);

        // Redirect messages are sent from elements, like qtdemux, to
        // notify of the new location(s) of the media.
        if (!g_strcmp0(messageTypeName, "redirect")) {
            mediaLocationChanged(message);
            return TRUE;
        }
    }

    // We ignore state changes from internal elements. They are forwarded to playbin2 anyway.
    bool messageSourceIsPlaybin = GST_MESSAGE_SRC(message) == reinterpret_cast<GstObject*>(m_pipeline.get());

    LOG_MEDIA_MESSAGE("Message %s received from element %s", GST_MESSAGE_TYPE_NAME(message), GST_MESSAGE_SRC_NAME(message));
    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:
        if (m_resetPipeline)
            break;
        if (m_missingPlugins)
            break;
        gst_message_parse_error(message, &err.outPtr(), &debug.outPtr());
        ERROR_MEDIA_MESSAGE("Error %d: %s (url=%s)", err->code, err->message, m_url.string().utf8().data());

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, "webkit-video.error");

        error = MediaPlayer::Empty;
        if (err->code == GST_STREAM_ERROR_CODEC_NOT_FOUND
            || err->code == GST_STREAM_ERROR_WRONG_TYPE
            || err->code == GST_STREAM_ERROR_FAILED
            || err->code == GST_CORE_ERROR_MISSING_PLUGIN
            || err->code == GST_RESOURCE_ERROR_NOT_FOUND)
            error = MediaPlayer::FormatError;
        else if (err->domain == GST_STREAM_ERROR) {
            // Let the mediaPlayerClient handle the stream error, in
            // this case the HTMLMediaElement will emit a stalled
            // event.
            if (err->code == GST_STREAM_ERROR_TYPE_NOT_FOUND) {
                ERROR_MEDIA_MESSAGE("Decode error, let the Media element emit a stalled event.");
                break;
            }
            error = MediaPlayer::DecodeError;
            attemptNextLocation = true;
        } else if (err->domain == GST_RESOURCE_ERROR)
            error = MediaPlayer::NetworkError;

        if (attemptNextLocation)
            issueError = !loadNextLocation();
        if (issueError)
            loadingFailed(error);
        break;
    case GST_MESSAGE_EOS:
        didEnd();
        break;
    case GST_MESSAGE_ASYNC_DONE:
        if (!messageSourceIsPlaybin || m_delayingLoad)
            break;
        asyncStateChangeDone();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        if (!messageSourceIsPlaybin || m_delayingLoad)
            break;
        updateStates();

        // Construct a filename for the graphviz dot file output.
        GstState newState;
        gst_message_parse_state_changed(message, &currentState, &newState, 0);
        CString dotFileName = String::format("webkit-video.%s_%s", gst_element_state_get_name(currentState), gst_element_state_get_name(newState)).utf8();
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(m_pipeline.get()), GST_DEBUG_GRAPH_SHOW_ALL, dotFileName.data());

        break;
    }
    case GST_MESSAGE_BUFFERING:
        processBufferingStats(message);
        break;
    case GST_MESSAGE_DURATION_CHANGED:
        durationChanged();
        break;
    case GST_MESSAGE_REQUEST_STATE:
        gst_message_parse_request_state(message, &requestedState);
        gst_element_get_state(m_pipeline.get(), &currentState, nullptr, 250 * GST_NSECOND);
        if (requestedState < currentState) {
            GUniquePtr<gchar> elementName(gst_element_get_name(GST_ELEMENT(message)));
            INFO_MEDIA_MESSAGE("Element %s requested state change to %s", elementName.get(),
                gst_element_state_get_name(requestedState));
            m_requestedState = requestedState;
            if (!changePipelineState(requestedState))
                loadingFailed(MediaPlayer::Empty);
        }
        break;
    case GST_MESSAGE_CLOCK_LOST:
        // This can only happen in PLAYING state and we should just
        // get a new clock by moving back to PAUSED and then to
        // PLAYING again.
        // This can happen if the stream that ends in a sink that
        // provides the current clock disappears, for example if
        // the audio sink provides the clock and the audio stream
        // is disabled. It also happens relatively often with
        // HTTP adaptive streams when switching between different
        // variants of a stream.
        gst_element_set_state(m_pipeline.get(), GST_STATE_PAUSED);
        gst_element_set_state(m_pipeline.get(), GST_STATE_PLAYING);
        break;
    case GST_MESSAGE_LATENCY:
        // Recalculate the latency, we don't need any special handling
        // here other than the GStreamer default.
        // This can happen if the latency of live elements changes, or
        // for one reason or another a new live element is added or
        // removed from the pipeline.
        gst_bin_recalculate_latency(GST_BIN(m_pipeline.get()));
        break;
    case GST_MESSAGE_ELEMENT:
        if (gst_is_missing_plugin_message(message)) {
            gchar* detail = gst_missing_plugin_message_get_installer_detail(message);
            gchar* detailArray[2] = {detail, 0};
            GstInstallPluginsReturn result = gst_install_plugins_async(detailArray, 0, mediaPlayerPrivatePluginInstallerResultFunction, this);
            m_missingPlugins = result == GST_INSTALL_PLUGINS_STARTED_OK;
            g_free(detail);
        }
#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
        else {
            GstMpegtsSection* section = gst_message_parse_mpegts_section(message);
            if (section) {
                processMpegTsSection(section);
                gst_mpegts_section_unref(section);
            }
        }
#endif
        break;
#if ENABLE(VIDEO_TRACK)
    case GST_MESSAGE_TOC:
        processTableOfContents(message);
        break;
#endif
#if ENABLE(MEDIA_SOURCE)
    case GST_MESSAGE_RESET_TIME:
        if (m_source && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
            StreamType streamType = getStreamType(GST_ELEMENT(GST_MESSAGE_SRC(message)));
            if (streamType == STREAM_TYPE_AUDIO || streamType == STREAM_TYPE_VIDEO)
                webkit_media_src_segment_needed(WEBKIT_MEDIA_SRC(m_source.get()), streamType);
        }
        break;
#endif
    default:
        LOG_MEDIA_MESSAGE("Unhandled GStreamer message type: %s",
                    GST_MESSAGE_TYPE_NAME(message));
        break;
    }
    return TRUE;
}

void MediaPlayerPrivateGStreamer::handlePluginInstallerResult(GstInstallPluginsReturn result)
{
    m_missingPlugins = false;
    if (result == GST_INSTALL_PLUGINS_SUCCESS) {
        changePipelineState(GST_STATE_READY);
        changePipelineState(GST_STATE_PAUSED);
    }
}

void MediaPlayerPrivateGStreamer::processBufferingStats(GstMessage* message)
{
    m_buffering = true;
    gst_message_parse_buffering(message, &m_bufferingPercentage);

    LOG_MEDIA_MESSAGE("[Buffering] Buffering: %d%%.", m_bufferingPercentage);

    updateStates();
}

#if ENABLE(VIDEO_TRACK) && USE(GSTREAMER_MPEGTS)
void MediaPlayerPrivateGStreamer::processMpegTsSection(GstMpegtsSection* section)
{
    ASSERT(section);

    if (section->section_type == GST_MPEGTS_SECTION_PMT) {
        const GstMpegtsPMT* pmt = gst_mpegts_section_get_pmt(section);
        m_metadataTracks.clear();
        for (guint i = 0; i < pmt->streams->len; ++i) {
            const GstMpegtsPMTStream* stream = static_cast<const GstMpegtsPMTStream*>(g_ptr_array_index(pmt->streams, i));
            if (stream->stream_type == 0x05 || stream->stream_type >= 0x80) {
                AtomicString pid = String::number(stream->pid);
                RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = InbandMetadataTextTrackPrivateGStreamer::create(
                    InbandTextTrackPrivate::Metadata, InbandTextTrackPrivate::Data, pid);

                // 4.7.10.12.2 Sourcing in-band text tracks
                // If the new text track's kind is metadata, then set the text track in-band metadata track dispatch
                // type as follows, based on the type of the media resource:
                // Let stream type be the value of the "stream_type" field describing the text track's type in the
                // file's program map section, interpreted as an 8-bit unsigned integer. Let length be the value of
                // the "ES_info_length" field for the track in the same part of the program map section, interpreted
                // as an integer as defined by the MPEG-2 specification. Let descriptor bytes be the length bytes
                // following the "ES_info_length" field. The text track in-band metadata track dispatch type must be
                // set to the concatenation of the stream type byte and the zero or more descriptor bytes bytes,
                // expressed in hexadecimal using uppercase ASCII hex digits.
                String inbandMetadataTrackDispatchType;
                appendUnsignedAsHexFixedSize(stream->stream_type, inbandMetadataTrackDispatchType, 2);
                for (guint j = 0; j < stream->descriptors->len; ++j) {
                    const GstMpegtsDescriptor* descriptor = static_cast<const GstMpegtsDescriptor*>(g_ptr_array_index(stream->descriptors, j));
                    for (guint k = 0; k < descriptor->length; ++k)
                        appendByteAsHex(descriptor->data[k], inbandMetadataTrackDispatchType);
                }
                track->setInBandMetadataTrackDispatchType(inbandMetadataTrackDispatchType);

                m_metadataTracks.add(pid, track);
                m_player->addTextTrack(track);
            }
        }
    } else {
        AtomicString pid = String::number(section->pid);
        RefPtr<InbandMetadataTextTrackPrivateGStreamer> track = m_metadataTracks.get(pid);
        if (!track)
            return;

        GRefPtr<GBytes> data = gst_mpegts_section_get_data(section);
        gsize size;
        const void* bytes = g_bytes_get_data(data.get(), &size);

        track->addDataCue(MediaTime::createWithDouble(currentTimeDouble()), MediaTime::createWithDouble(currentTimeDouble()), bytes, size);
    }
}
#endif

#if ENABLE(VIDEO_TRACK)
void MediaPlayerPrivateGStreamer::processTableOfContents(GstMessage* message)
{
    if (m_chaptersTrack)
        m_player->removeTextTrack(m_chaptersTrack);

    m_chaptersTrack = InbandMetadataTextTrackPrivateGStreamer::create(InbandTextTrackPrivate::Chapters, InbandTextTrackPrivate::Generic);
    m_player->addTextTrack(m_chaptersTrack);

    GRefPtr<GstToc> toc;
    gboolean updated;
    gst_message_parse_toc(message, &toc.outPtr(), &updated);
    ASSERT(toc);

    for (GList* i = gst_toc_get_entries(toc.get()); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), 0);
}

void MediaPlayerPrivateGStreamer::processTableOfContentsEntry(GstTocEntry* entry, GstTocEntry* parent)
{
    UNUSED_PARAM(parent);
    ASSERT(entry);

    RefPtr<GenericCueData> cue = GenericCueData::create();

    gint64 start = -1, stop = -1;
    gst_toc_entry_get_start_stop_times(entry, &start, &stop);
    if (start != -1)
        cue->setStartTime(MediaTime(start, GST_SECOND));
    if (stop != -1)
        cue->setEndTime(MediaTime(stop, GST_SECOND));

    GstTagList* tags = gst_toc_entry_get_tags(entry);
    if (tags) {
        gchar* title =  0;
        gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
        if (title) {
            cue->setContent(title);
            g_free(title);
        }
    }

    m_chaptersTrack->addGenericCue(cue.release());

    for (GList* i = gst_toc_entry_get_sub_entries(entry); i; i = i->next)
        processTableOfContentsEntry(static_cast<GstTocEntry*>(i->data), entry);
}
#endif

void MediaPlayerPrivateGStreamer::fillTimerFired()
{
    GstQuery* query = gst_query_new_buffering(GST_FORMAT_PERCENT);

    if (!gst_element_query(m_pipeline.get(), query)) {
        gst_query_unref(query);
        return;
    }

    gint64 start, stop;
    gdouble fillStatus = 100.0;

    gst_query_parse_buffering_range(query, 0, &start, &stop, 0);
    gst_query_unref(query);

    if (stop != -1)
        fillStatus = 100.0 * stop / GST_FORMAT_PERCENT_MAX;

    LOG_MEDIA_MESSAGE("[Buffering] Download buffer filled up to %f%%", fillStatus);

    if (!m_mediaDuration)
        durationChanged();

    // Update maxTimeLoaded only if the media duration is
    // available. Otherwise we can't compute it.
    if (m_mediaDuration) {
        if (fillStatus == 100.0)
            m_maxTimeLoaded = m_mediaDuration;
        else
            m_maxTimeLoaded = static_cast<float>((fillStatus * m_mediaDuration) / 100.0);
        LOG_MEDIA_MESSAGE("[Buffering] Updated maxTimeLoaded: %f", m_maxTimeLoaded);
    }

    m_downloadFinished = fillStatus == 100.0;
    if (!m_downloadFinished) {
        updateStates();
        return;
    }

    // Media is now fully loaded. It will play even if network
    // connection is cut. Buffering is done, remove the fill source
    // from the main loop.
    m_fillTimer.stop();
    updateStates();
}

float MediaPlayerPrivateGStreamer::maxTimeSeekable() const
{
    if (m_errorOccured)
        return 0.0f;

    LOG_MEDIA_MESSAGE("maxTimeSeekable");
    // infinite duration means live stream
    if (std::isinf(duration()))
        return 0.0f;

    return duration();
}

float MediaPlayerPrivateGStreamer::maxTimeLoaded() const
{
    if (m_errorOccured)
        return 0.0f;

    float loaded = m_maxTimeLoaded;
    if (m_isEndReached && m_mediaDuration)
        loaded = m_mediaDuration;
    LOG_MEDIA_MESSAGE("maxTimeLoaded: %f", loaded);
    return loaded;
}

bool MediaPlayerPrivateGStreamer::didLoadingProgress() const
{
    if (!m_pipeline ||
#if ENABLE(MEDIA_SOURCE)
            (!isMediaSource() && !totalBytes())
#else
            !totalBytes()
#endif
            || !m_mediaDuration)
        return false;
    float currentMaxTimeLoaded = maxTimeLoaded();
    bool didLoadingProgress = currentMaxTimeLoaded != m_maxTimeLoadedAtLastDidLoadingProgress;
    m_maxTimeLoadedAtLastDidLoadingProgress = currentMaxTimeLoaded;
    LOG_MEDIA_MESSAGE("didLoadingProgress: %d", didLoadingProgress);
    return didLoadingProgress;
}

unsigned long long MediaPlayerPrivateGStreamer::totalBytes() const
{
    if (m_errorOccured)
        return 0;

    if (m_totalBytes)
        return m_totalBytes;

    if (!m_source)
        return 0;

    GstFormat fmt = GST_FORMAT_BYTES;
    gint64 length = 0;
    if (gst_element_query_duration(m_source.get(), fmt, &length)) {
        INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
        m_totalBytes = static_cast<unsigned long long>(length);
        m_isStreaming = !length;
        return m_totalBytes;
    }

    // Fall back to querying the source pads manually.
    // See also https://bugzilla.gnome.org/show_bug.cgi?id=638749
    GstIterator* iter = gst_element_iterate_src_pads(m_source.get());
    bool done = false;
    while (!done) {
        GValue item = G_VALUE_INIT;
        switch (gst_iterator_next(iter, &item)) {
        case GST_ITERATOR_OK: {
            GstPad* pad = static_cast<GstPad*>(g_value_get_object(&item));
            gint64 padLength = 0;
            if (gst_pad_query_duration(pad, fmt, &padLength) && padLength > length)
                length = padLength;
            break;
        }
        case GST_ITERATOR_RESYNC:
            gst_iterator_resync(iter);
            break;
        case GST_ITERATOR_ERROR:
            FALLTHROUGH;
        case GST_ITERATOR_DONE:
            done = true;
            break;
        }

        g_value_unset(&item);
    }

    gst_iterator_free(iter);

    INFO_MEDIA_MESSAGE("totalBytes %" G_GINT64_FORMAT, length);
    m_totalBytes = static_cast<unsigned long long>(length);
    m_isStreaming = !length;
    return m_totalBytes;
}

void MediaPlayerPrivateGStreamer::sourceChanged()
{
    m_source.clear();
    g_object_get(m_pipeline.get(), "source", &m_source.outPtr(), nullptr);

    if (WEBKIT_IS_WEB_SRC(m_source.get()))
        webKitWebSrcSetMediaPlayer(WEBKIT_WEB_SRC(m_source.get()), m_player);
#if ENABLE(MEDIA_SOURCE)
    if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        MediaSourceGStreamer::open(m_mediaSource.get(), WEBKIT_MEDIA_SRC(m_source.get()), this);
        g_signal_connect(m_source.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
        g_signal_connect(m_source.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);
        g_signal_connect(m_source.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);
        webkit_media_src_set_mediaplayerprivate(WEBKIT_MEDIA_SRC(m_source.get()), this);
    }
#endif
}

void MediaPlayerPrivateGStreamer::cancelLoad()
{
#if ENABLE(ENCRYPTED_MEDIA)
    // Potentially unblock GStreamer thread for DRM license acquisition.
    m_drmKeySemaphore.signal();
#endif

    if (m_networkState < MediaPlayer::Loading || m_networkState == MediaPlayer::Loaded)
        return;

    if (m_pipeline)
        changePipelineState(GST_STATE_READY);
}

void MediaPlayerPrivateGStreamer::asyncStateChangeDone()
{
    if (!m_pipeline || m_errorOccured)
        return;

    if (m_seeking) {
        if (m_seekIsPending)
            updateStates();
        else {
            LOG_MEDIA_MESSAGE("[Seek] seeked to %f", m_seekTime);
            m_seeking = false;
            m_cachedPosition = -1;
            if (m_timeOfOverlappingSeek != m_seekTime && m_timeOfOverlappingSeek != -1) {
                seek(m_timeOfOverlappingSeek);
                m_timeOfOverlappingSeek = -1;
                return;
            }
            m_timeOfOverlappingSeek = -1;

            // The pipeline can still have a pending state. In this case a position query will fail.
            // Right now we can use m_seekTime as a fallback.
            m_canFallBackToLastFinishedSeekPosition = true;
            timeChanged();
        }
    } else
        updateStates();
}

void MediaPlayerPrivateGStreamer::updateStates()
{
    if (!m_pipeline)
        return;

    if (m_errorOccured)
        return;

    MediaPlayer::NetworkState oldNetworkState = m_networkState;
    MediaPlayer::ReadyState oldReadyState = m_readyState;
    GstState state;
    GstState pending;

    GstStateChangeReturn getStateResult = gst_element_get_state(m_pipeline.get(), &state, &pending, 250 * GST_NSECOND);

    bool shouldUpdatePlaybackState = false;
    switch (getStateResult) {
    case GST_STATE_CHANGE_SUCCESS: {
        LOG_MEDIA_MESSAGE("State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        // Do nothing if on EOS and state changed to READY to avoid recreating the player
        // on HTMLMediaElement and properly generate the video 'ended' event.
        if (m_isEndReached && state == GST_STATE_READY)
            break;

        if (state <= GST_STATE_READY) {
            m_resetPipeline = true;
            m_mediaDuration = 0;
        } else {
            m_resetPipeline = false;
            cacheDuration();
        }

        bool didBuffering = m_buffering;

        // Update ready and network states.
        switch (state) {
        case GST_STATE_NULL:
            m_readyState = MediaPlayer::HaveNothing;
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_READY:
            m_readyState = MediaPlayer::HaveMetadata;
            m_networkState = MediaPlayer::Empty;
            break;
        case GST_STATE_PAUSED:
        case GST_STATE_PLAYING:
            if (m_buffering) {
                if (m_bufferingPercentage == 100) {
                    LOG_MEDIA_MESSAGE("[Buffering] Complete.");
                    m_buffering = false;
                    m_readyState = MediaPlayer::HaveEnoughData;
                    m_networkState = m_downloadFinished ? MediaPlayer::Idle : MediaPlayer::Loading;
                } else {
                    m_readyState = MediaPlayer::HaveCurrentData;
                    m_networkState = MediaPlayer::Loading;
                }
            } else if (m_downloadFinished) {
                m_readyState = MediaPlayer::HaveEnoughData;
                m_networkState = MediaPlayer::Loaded;
            } else {
                m_readyState = MediaPlayer::HaveFutureData;
                m_networkState = MediaPlayer::Loading;
            }

            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }

        // Sync states where needed.
        if (state == GST_STATE_PAUSED) {
            if (!m_volumeAndMuteInitialized) {
                notifyPlayerOfVolumeChange();
                notifyPlayerOfMute();
                m_volumeAndMuteInitialized = true;
            }

            if (didBuffering && !m_buffering && !m_paused && m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Restarting playback.");
                changePipelineState(GST_STATE_PLAYING);
            }
        } else if (state == GST_STATE_PLAYING) {
            m_paused = false;

            if ((m_buffering && !isLiveStream()) || !m_playbackRate) {
                LOG_MEDIA_MESSAGE("[Buffering] Pausing stream for buffering.");
                changePipelineState(GST_STATE_PAUSED);
            }
        } else
            m_paused = true;

        if (m_requestedState == GST_STATE_PAUSED && state == GST_STATE_PAUSED) {
            shouldUpdatePlaybackState = true;
            LOG_MEDIA_MESSAGE("Requested state change to %s was completed", gst_element_state_get_name(state));
        }

        break;
    }
    case GST_STATE_CHANGE_ASYNC:
        LOG_MEDIA_MESSAGE("Async: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change in progress.
        break;
    case GST_STATE_CHANGE_FAILURE:
        LOG_MEDIA_MESSAGE("Failure: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));
        // Change failed
        return;
    case GST_STATE_CHANGE_NO_PREROLL:
        LOG_MEDIA_MESSAGE("No preroll: State: %s, pending: %s", gst_element_state_get_name(state), gst_element_state_get_name(pending));

        // Live pipelines go in PAUSED without prerolling.
        m_isStreaming = true;
        setDownloadBuffering();

        if (state == GST_STATE_READY)
            m_readyState = MediaPlayer::HaveNothing;
        else if (state == GST_STATE_PAUSED) {
            m_readyState = MediaPlayer::HaveEnoughData;
            m_paused = true;
        } else if (state == GST_STATE_PLAYING)
            m_paused = false;

        if (!m_paused && m_playbackRate)
            changePipelineState(GST_STATE_PLAYING);

        m_networkState = MediaPlayer::Loading;
        break;
    default:
        LOG_MEDIA_MESSAGE("Else : %d", getStateResult);
        break;
    }

    m_requestedState = GST_STATE_VOID_PENDING;

    if (shouldUpdatePlaybackState)
        m_player->playbackStateChanged();

    if (m_networkState != oldNetworkState) {
        LOG_MEDIA_MESSAGE("Network State Changed from %u to %u", oldNetworkState, m_networkState);
        m_player->networkStateChanged();
    }
    if (m_readyState != oldReadyState) {
        LOG_MEDIA_MESSAGE("Ready State Changed from %u to %u", oldReadyState, m_readyState);
        m_player->readyStateChanged();
    }

    if (getStateResult == GST_STATE_CHANGE_SUCCESS && state >= GST_STATE_PAUSED) {
        updatePlaybackRate();
        if (m_seekIsPending) {
            LOG_MEDIA_MESSAGE("[Seek] committing pending seek to %f", m_seekTime);
            m_seekIsPending = false;
            m_seeking = doSeek(toGstClockTime(m_seekTime), m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
            if (!m_seeking) {
                m_cachedPosition = -1;
                LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", m_seekTime);
            }
        }
    }
}

#if ENABLE(MEDIA_SOURCE)
GRefPtr<GstCaps> MediaPlayerPrivateGStreamer::currentDemuxerCaps() const
{
    GRefPtr<GstCaps> result;
    if (m_mediaSource && WEBKIT_IS_MEDIA_SRC(m_source.get())) {
        // METRO FIXME: Select the current demuxer pad (how to know?) instead of the first one
        GstPad* demuxersrcpad = webkit_media_src_get_video_pad(WEBKIT_MEDIA_SRC(m_source.get()), 0);

        if (demuxersrcpad) {
            result = adoptGRef(gst_pad_get_current_caps(demuxersrcpad));
        }
    }
    return result;
}
#endif

void MediaPlayerPrivateGStreamer::mediaLocationChanged(GstMessage* message)
{
    if (m_mediaLocations)
        gst_structure_free(m_mediaLocations);

    const GstStructure* structure = gst_message_get_structure(message);
    if (structure) {
        // This structure can contain:
        // - both a new-location string and embedded locations structure
        // - or only a new-location string.
        m_mediaLocations = gst_structure_copy(structure);
        const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");

        if (locations)
            m_mediaLocationCurrentIndex = static_cast<int>(gst_value_list_get_size(locations)) -1;

        loadNextLocation();
    }
}

bool MediaPlayerPrivateGStreamer::loadNextLocation()
{
    if (!m_mediaLocations)
        return false;

    const GValue* locations = gst_structure_get_value(m_mediaLocations, "locations");
    const gchar* newLocation = 0;

    if (!locations) {
        // Fallback on new-location string.
        newLocation = gst_structure_get_string(m_mediaLocations, "new-location");
        if (!newLocation)
            return false;
    }

    if (!newLocation) {
        if (m_mediaLocationCurrentIndex < 0) {
            m_mediaLocations = 0;
            return false;
        }

        const GValue* location = gst_value_list_get_value(locations,
                                                          m_mediaLocationCurrentIndex);
        const GstStructure* structure = gst_value_get_structure(location);

        if (!structure) {
            m_mediaLocationCurrentIndex--;
            return false;
        }

        newLocation = gst_structure_get_string(structure, "new-location");
    }

    if (newLocation) {
        // Found a candidate. new-location is not always an absolute url
        // though. We need to take the base of the current url and
        // append the value of new-location to it.
        URL baseUrl = gst_uri_is_valid(newLocation) ? URL() : m_url;
        URL newUrl = URL(baseUrl, newLocation);

        RefPtr<SecurityOrigin> securityOrigin = SecurityOrigin::create(m_url);
        if (securityOrigin->canRequest(newUrl)) {
            INFO_MEDIA_MESSAGE("New media url: %s", newUrl.string().utf8().data());

#if ENABLE(ENCRYPTED_MEDIA)
            // Potentially unblock GStreamer thread for DRM license acquisition.
            m_drmKeySemaphore.signal();
#endif

            // Reset player states.
            m_networkState = MediaPlayer::Loading;
            m_player->networkStateChanged();
            m_readyState = MediaPlayer::HaveNothing;
            m_player->readyStateChanged();

            // Reset pipeline state.
            m_resetPipeline = true;
            changePipelineState(GST_STATE_READY);

            GstState state;
            gst_element_get_state(m_pipeline.get(), &state, nullptr, 0);
            if (state <= GST_STATE_READY) {
                // Set the new uri and start playing.
                g_object_set(m_pipeline.get(), "uri", newUrl.string().utf8().data(), nullptr);
                m_url = newUrl;
                changePipelineState(GST_STATE_PLAYING);
                return true;
            }
        } else
            INFO_MEDIA_MESSAGE("Not allowed to load new media location: %s", newUrl.string().utf8().data());
    }
    m_mediaLocationCurrentIndex--;
    return false;
}

void MediaPlayerPrivateGStreamer::loadStateChanged()
{
    updateStates();
}

void MediaPlayerPrivateGStreamer::timeChanged()
{
    updateStates();
    m_player->timeChanged();
}

void MediaPlayerPrivateGStreamer::didEnd()
{
    // Synchronize position and duration values to not confuse the
    // HTMLMediaElement. In some cases like reverse playback the
    // position is not always reported as 0 for instance.
    float now = currentTime();
    if (now > 0 && now <= duration() && m_mediaDuration != now) {
        m_mediaDurationKnown = true;
        m_mediaDuration = now;
        m_player->durationChanged();
    }

    m_isEndReached = true;
    timeChanged();

    if (!m_player->client().mediaPlayerIsLooping()) {
        m_paused = true;
        changePipelineState(GST_STATE_READY);
        m_downloadFinished = false;
    }
}

#if ENABLE(MEDIA_SOURCE)
void MediaPlayerPrivateGStreamer::notifyAppendComplete()
{
    if (m_seekIsPending) {
        updatePlaybackRate();
        LOG_MEDIA_MESSAGE("[Seek] committing pending seek to %f after append completed", m_seekTime);
        m_seekIsPending = false;
        m_seeking = doSeek(toGstClockTime(m_seekTime), m_player->rate(), static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE));
        if (!m_seeking) {
            m_cachedPosition = -1;
            LOG_MEDIA_MESSAGE("[Seek] seeking to %f failed", m_seekTime);
        }
    }
}
#endif

void MediaPlayerPrivateGStreamer::cacheDuration()
{
    if (m_mediaDuration || !m_mediaDurationKnown)
        return;

    float newDuration = duration();
    if (std::isinf(newDuration)) {
        // Only pretend that duration is not available if the the query failed in a stable pipeline state.
        GstState state;
        if (gst_element_get_state(m_pipeline.get(), &state, nullptr, 0) == GST_STATE_CHANGE_SUCCESS && state > GST_STATE_READY)
            m_mediaDurationKnown = false;
        return;
    }

    m_mediaDuration = newDuration;
}

void MediaPlayerPrivateGStreamer::notifyDurationChanged()
{
    m_pendingAsyncOperationsLock.lock();
    ASSERT(m_pendingAsyncOperations);
    m_pendingAsyncOperations = g_list_remove(m_pendingAsyncOperations, m_pendingAsyncOperations->data);
    m_pendingAsyncOperationsLock.unlock();

    durationChanged();
}

void MediaPlayerPrivateGStreamer::durationChanged()
{
    float previousDuration = m_mediaDuration;

#if ENABLE(MEDIA_SOURCE)
    // Force duration refresh.
    if (isMediaSource())
        m_mediaDuration = 0;
#endif

    cacheDuration();
    // Avoid emiting durationchanged in the case where the previous
    // duration was 0 because that case is already handled by the
    // HTMLMediaElement.
    if (previousDuration && m_mediaDuration != previousDuration)
        m_player->durationChanged();
}

void MediaPlayerPrivateGStreamer::loadingFailed(MediaPlayer::NetworkState error)
{
    m_errorOccured = true;
    if (m_networkState != error) {
        m_networkState = error;
        m_player->networkStateChanged();
    }
    if (m_readyState != MediaPlayer::HaveNothing) {
        m_readyState = MediaPlayer::HaveNothing;
        m_player->readyStateChanged();
    }

    // Loading failed, remove ready timer.
    m_readyTimerHandler.cancel();
}

static HashSet<String> mimeTypeCache()
{
    initializeGStreamerAndRegisterWebKitElements();

    DEPRECATED_DEFINE_STATIC_LOCAL(HashSet<String>, cache, ());
    static bool typeListInitialized = false;

    if (typeListInitialized)
        return cache;

    const char* mimeTypes[] = {
        "application/ogg",
        "application/vnd.apple.mpegurl",
        "application/vnd.rn-realmedia",
        "application/x-3gp",
        "application/x-pn-realaudio",
        "application/x-mpegurl",
        "audio/3gpp",
        "audio/aac",
        "audio/flac",
        "audio/iLBC-sh",
        "audio/midi",
        "audio/mobile-xmf",
        "audio/mp1",
        "audio/mp2",
        "audio/mp3",
        "audio/mp4",
        "audio/mpeg",
        "audio/ogg",
        "audio/opus",
        "audio/qcelp",
        "audio/riff-midi",
        "audio/speex",
        "audio/wav",
        "audio/webm",
        "audio/x-ac3",
        "audio/x-aiff",
        "audio/x-amr-nb-sh",
        "audio/x-amr-wb-sh",
        "audio/x-au",
        "audio/x-ay",
        "audio/x-celt",
        "audio/x-dts",
        "audio/x-flac",
        "audio/x-gbs",
        "audio/x-gsm",
        "audio/x-gym",
        "audio/x-imelody",
        "audio/x-ircam",
        "audio/x-kss",
        "audio/x-m4a",
        "audio/x-mod",
        "audio/x-mp3",
        "audio/x-mpeg",
        "audio/x-musepack",
        "audio/x-nist",
        "audio/x-nsf",
        "audio/x-paris",
        "audio/x-sap",
        "audio/x-sbc",
        "audio/x-sds",
        "audio/x-shorten",
        "audio/x-sid",
        "audio/x-spc",
        "audio/x-speex",
        "audio/x-svx",
        "audio/x-ttafile",
        "audio/x-vgm",
        "audio/x-voc",
        "audio/x-vorbis+ogg",
        "audio/x-w64",
        "audio/x-wav",
        "audio/x-wavpack",
        "audio/x-wavpack-correction",
        "video/3gpp",
        "video/flv",
        "video/mj2",
        "video/mp2t",
        "video/mp4",
        "video/mpeg",
        "video/mpegts",
        "video/ogg",
        "video/quicktime",
        "video/vivo",
        "video/webm",
        "video/x-cdxa",
        "video/x-dirac",
        "video/x-dv",
        "video/x-fli",
        "video/x-flv",
        "video/x-h263",
        "video/x-ivf",
        "video/x-m4v",
        "video/x-matroska",
        "video/x-mng",
        "video/x-ms-asf",
        "video/x-msvideo",
        "video/x-mve",
        "video/x-nuv",
        "video/x-vcd"
    };

    for (unsigned i = 0; i < (sizeof(mimeTypes) / sizeof(*mimeTypes)); ++i)
        cache.add(String(mimeTypes[i]));

    typeListInitialized = true;
    return cache;
}

void MediaPlayerPrivateGStreamer::getSupportedTypes(HashSet<String>& types)
{
    types = mimeTypeCache();
}

bool MediaPlayerPrivateGStreamer::supportsKeySystem(const String& keySystem, const String& mimeType)
{
    GST_DEBUG("Checking for KeySystem support with %s and type %s", keySystem.utf8().data(), mimeType.utf8().data());

#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready") ||
        equalIgnoringCase(keySystem, "com.youtube.playready"))
        return true;
#endif

    if (equalIgnoringCase(keySystem, "org.w3.clearkey"))
        return true;

    return false;
}

#if ENABLE(ENCRYPTED_MEDIA)
MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamer::addKey(const String& keySystem, const unsigned char* keyData, unsigned keyLength, const unsigned char* /* initData */, unsigned /* initDataLength */ , const String& sessionID)
{
    LOG_MEDIA_MESSAGE("addKey system: %s, length: %u, session: %s", keySystem.utf8().data(), keyLength, sessionID.utf8().data());
    GstBuffer* buffer = gst_buffer_new_wrapped(g_memdup(keyData, keyLength), keyLength);
    gst_element_send_event(m_pipeline.get(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
        gst_structure_new("drm-cipher", "key", GST_TYPE_BUFFER, buffer, nullptr)));
    gst_buffer_unref(buffer);
    m_drmKeySemaphore.signal();
    return MediaPlayer::NoError;
}

MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamer::generateKeyRequest(const String& keySystem, const unsigned char* initDataPtr, unsigned initDataLength)
{
    LOG_MEDIA_MESSAGE("generateKeyRequest");
    m_player->keyMessage(keySystem, createCanonicalUUIDString(), initDataPtr, initDataLength, URL());
    return MediaPlayer::NoError;
}

MediaPlayer::MediaKeyException MediaPlayerPrivateGStreamer::cancelKeyRequest(const String& /* keySystem */ , const String& /* sessionID */)
{
    LOG_MEDIA_MESSAGE("cancelKeyRequest");
    return MediaPlayer::KeySystemNotSupported;
}

void MediaPlayerPrivateGStreamer::needKey(const String& keySystem, const String& sessionId, const unsigned char* initData, unsigned initDataLength)
{
    if (!m_player->keyNeeded(keySystem, sessionId, initData, initDataLength)) {
        GST_DEBUG("no event handler for key needed, waking up GStreamer thread");
        m_drmKeySemaphore.signal();
    }
}

void MediaPlayerPrivateGStreamer::signalDRM()
{
    GST_DEBUG("key/license was changed or failed, signal semaphore");
    // Wake up a potential waiter blocked in the GStreamer thread
    m_drmKeySemaphore.signal();
}
#endif


#if ENABLE(ENCRYPTED_MEDIA_V2)
MediaPlayer::SupportsType MediaPlayerPrivateGStreamer::extendedSupportsType(const MediaEngineSupportParameters& parameters)
{
    // From: <http://dvcs.w3.org/hg/html-media/raw-file/eme-v0.1b/encrypted-media/encrypted-media.html#dom-canplaytype>
    // In addition to the steps in the current specification, this method must run the following steps:

    // 1. Check whether the Key System is supported with the specified container and codec type(s) by following the steps for the first matching condition from the following list:
    //    If keySystem is null, continue to the next step.
    if (parameters.keySystem.isNull() || parameters.keySystem.isEmpty())
        return supportsType(parameters);

    // If keySystem contains an unrecognized or unsupported Key System, return the empty string
    if (!supportsKeySystem(parameters.keySystem, emptyString()))
        return MediaPlayer::IsNotSupported;

    // If the Key System specified by keySystem does not support decrypting the container and/or codec specified in the rest of the type string.
    // (AVFoundation does not provide an API which would allow us to determine this, so this is a no-op)

    // 2. Return "maybe" or "probably" as appropriate per the existing specification of canPlayType().
    return supportsType(parameters);
}

void MediaPlayerPrivateGStreamer::needKey(RefPtr<Uint8Array> initData)
{
    if (!m_player->keyNeeded(initData.get())) {
        GST_DEBUG("no event handler for key needed, waking up GStreamer thread");
        m_drmKeySemaphore.signal();
    }
}

std::unique_ptr<CDMSession> MediaPlayerPrivateGStreamer::createSession(const String& keySystem)
{
    if (!supportsKeySystem(keySystem, emptyString()))
        return nullptr;

#if USE(DXDRM)
    if (equalIgnoringCase(keySystem, "com.microsoft.playready")
        || equalIgnoringCase(keySystem, "com.youtube.playready"))
        return std::make_unique<CDMPRSessionGStreamer>(this);
#endif

    return nullptr;
}

void MediaPlayerPrivateGStreamer::setCDMSession(CDMSession* session)
{
    m_cdmSession = session;
}

void MediaPlayerPrivateGStreamer::keyAdded()
{
#if USE(DXDRM)
    gst_element_send_event(m_pipeline.get(), gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB,
       gst_structure_new("drm-cipher", "stream", G_TYPE_POINTER, static_cast<CDMPRSessionGStreamer*>(m_cdmSession), nullptr)));
#endif
}
#endif


MediaPlayer::SupportsType MediaPlayerPrivateGStreamer::supportsType(const MediaEngineSupportParameters& parameters)
{
#if ENABLE(MEDIA_SOURCE)
    // Disable VPX/Opus on MSE for now, mp4/avc1 seems way more reliable currently.
    if (parameters.isMediaSource && parameters.type.endsWith("webm"))
        return MediaPlayer::IsNotSupported;
#endif

    if (parameters.type.isNull() || parameters.type.isEmpty())
        return MediaPlayer::IsNotSupported;

    // spec says we should not return "probably" if the codecs string is empty
    if (mimeTypeCache().contains(parameters.type))
        return parameters.codecs.isEmpty() ? MediaPlayer::MayBeSupported : MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

void MediaPlayerPrivateGStreamer::setDownloadBuffering()
{
    if (!m_pipeline)
        return;

#if ENABLE(MEDIA_SOURCE)
    if (isMediaSource())
        return;
#endif

    unsigned flags;
    g_object_get(m_pipeline.get(), "flags", &flags, nullptr);

    unsigned flagDownload = getGstPlayFlag("download");

    // We don't want to stop downloading if we already started it.
    if (flags & flagDownload && m_readyState > MediaPlayer::HaveNothing && !m_resetPipeline)
        return;

    bool shouldDownload = !isLiveStream() && m_preload == MediaPlayer::Auto;
    if (shouldDownload) {
        LOG_MEDIA_MESSAGE("Enabling on-disk buffering");
        g_object_set(m_pipeline.get(), "flags", flags | flagDownload, nullptr);
        m_fillTimer.startRepeating(0.2);
    } else {
        LOG_MEDIA_MESSAGE("Disabling on-disk buffering");
        g_object_set(m_pipeline.get(), "flags", flags & ~flagDownload, nullptr);
        m_fillTimer.stop();
    }
}

void MediaPlayerPrivateGStreamer::setPreload(MediaPlayer::Preload preload)
{
    if (preload == MediaPlayer::Auto && isLiveStream())
        return;

    m_preload = preload;
    setDownloadBuffering();

    if (m_delayingLoad && m_preload != MediaPlayer::None) {
        m_delayingLoad = false;
        commitLoad();
    }
}

GstElement* MediaPlayerPrivateGStreamer::createAudioSink()
{
    m_autoAudioSink = gst_element_factory_make("autoaudiosink", 0);
    g_signal_connect(m_autoAudioSink.get(), "child-added", G_CALLBACK(setAudioStreamPropertiesCallback), this);

    GstElement* audioSinkBin;

    if (webkitGstCheckVersion(1, 4, 2)) {
#if ENABLE(WEB_AUDIO)
        audioSinkBin = gst_bin_new("audio-sink");
        m_audioSourceProvider->configureAudioBin(audioSinkBin, nullptr);
        return audioSinkBin;
#else
        return m_autoAudioSink.get();
#endif
    }

    // Construct audio sink only if pitch preserving is enabled.
    // If GStreamer 1.4.2 is used the audio-filter playbin property is used instead.
    if (m_preservesPitch) {
        GstElement* scale = gst_element_factory_make("scaletempo", nullptr);
        if (!scale) {
            GST_WARNING("Failed to create scaletempo");
            return m_autoAudioSink.get();
        }

        audioSinkBin = gst_bin_new("audio-sink");
        gst_bin_add(GST_BIN(audioSinkBin), scale);
        GRefPtr<GstPad> pad = adoptGRef(gst_element_get_static_pad(scale, "sink"));
        gst_element_add_pad(audioSinkBin, gst_ghost_pad_new("sink", pad.get()));

#if ENABLE(WEB_AUDIO)
        m_audioSourceProvider->configureAudioBin(audioSinkBin, scale);
#else
        GstElement* convert = gst_element_factory_make("audioconvert", nullptr);
        GstElement* resample = gst_element_factory_make("audioresample", nullptr);

        gst_bin_add_many(GST_BIN(audioSinkBin), convert, resample, m_autoAudioSink.get(), nullptr);

        if (!gst_element_link_many(scale, convert, resample, m_autoAudioSink.get(), nullptr)) {
            GST_WARNING("Failed to link audio sink elements");
            gst_object_unref(audioSinkBin);
            return m_autoAudioSink.get();
        }
#endif
        return audioSinkBin;
    }

#if ENABLE(WEB_AUDIO)
    audioSinkBin = gst_bin_new("audio-sink");
    m_audioSourceProvider->configureAudioBin(audioSinkBin, nullptr);
    return audioSinkBin;
#endif
    ASSERT_NOT_REACHED();
    return 0;
}

GstElement* MediaPlayerPrivateGStreamer::audioSink() const
{
    GstElement* sink;
    g_object_get(m_pipeline.get(), "audio-sink", &sink, nullptr);
    return sink;

    GRefPtr<GstElement> playsink = adoptGRef(gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "playsink"));
    if (playsink) {
        // The default value (0) means "send events to all the sinks", instead
        // of "only to the first that returns true". This is needed for MSE seek.
        g_object_set(G_OBJECT(playsink.get()), "send-event-mode", 0, NULL);
    }
}

void MediaPlayerPrivateGStreamer::configurePlaySink()
{
    GRefPtr<GstElement> playsink = adoptGRef(gst_bin_get_by_name(GST_BIN(m_pipeline.get()), "playsink"));
    if (playsink) {
        // The default value (0) means "send events to all the sinks", instead
        // of "only to the first that returns true". This is needed for MSE seek.
        g_object_set(G_OBJECT(playsink.get()), "send-event-mode", 0, NULL);
    }
}

void MediaPlayerPrivateGStreamer::createGSTPlayBin()
{
    ASSERT(!m_pipeline);

    // gst_element_factory_make() returns a floating reference so
    // we should not adopt.
    setPipeline(gst_element_factory_make("playbin", "play"));
    setStreamVolumeElement(GST_STREAM_VOLUME(m_pipeline.get()));

    unsigned flagText = getGstPlayFlag("text");
    unsigned flagAudio = getGstPlayFlag("audio");
    unsigned flagVideo = getGstPlayFlag("video");
    unsigned flagNativeVideo = getGstPlayFlag("native-video");
    g_object_set(m_pipeline.get(), "flags", flagText | flagAudio | flagVideo | flagNativeVideo, nullptr);

    GRefPtr<GstBus> bus = adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(m_pipeline.get())));
    gst_bus_add_signal_watch(bus.get());
    g_signal_connect(bus.get(), "message", G_CALLBACK(mediaPlayerPrivateMessageCallback), this);

    gst_bus_enable_sync_message_emission(bus.get());
    g_signal_connect(bus.get(), "sync-message", G_CALLBACK(mediaPlayerPrivateSyncMessageCallback), this);

    g_object_set(m_pipeline.get(), "mute", m_player->muted(), nullptr);

    g_signal_connect(m_pipeline.get(), "notify::source", G_CALLBACK(mediaPlayerPrivateSourceChangedCallback), this);

    // If we load a MediaSource later, we will also listen the signals from
    // WebKitMediaSrc, which will be connected later in sourceChanged()
    // METRO FIXME: In that case, we shouldn't listen to these signals coming from playbin, or the callbacks will be called twice.
    g_signal_connect(m_pipeline.get(), "video-changed", G_CALLBACK(mediaPlayerPrivateVideoChangedCallback), this);
    g_signal_connect(m_pipeline.get(), "audio-changed", G_CALLBACK(mediaPlayerPrivateAudioChangedCallback), this);
#if ENABLE(VIDEO_TRACK)
    if (webkitGstCheckVersion(1, 1, 2)) {
        g_signal_connect(m_pipeline.get(), "text-changed", G_CALLBACK(mediaPlayerPrivateTextChangedCallback), this);

        GstElement* textCombiner = webkitTextCombinerNew();
        ASSERT(textCombiner);
        g_object_set(m_pipeline.get(), "text-stream-combiner", textCombiner, nullptr);

        m_textAppSink = webkitTextSinkNew();
        ASSERT(m_textAppSink);

        m_textAppSinkPad = adoptGRef(gst_element_get_static_pad(m_textAppSink.get(), "sink"));
        ASSERT(m_textAppSinkPad);

        g_object_set(m_textAppSink.get(), "emit-signals", true, "enable-last-sample", false, "caps", gst_caps_new_empty_simple("text/vtt"), NULL);
        g_signal_connect(m_textAppSink.get(), "new-sample", G_CALLBACK(mediaPlayerPrivateNewTextSampleCallback), this);

        g_object_set(m_pipeline.get(), "text-sink", m_textAppSink.get(), NULL);
    }
#endif

    g_object_set(m_pipeline.get(), "video-sink", createVideoSink(), "audio-sink", createAudioSink(), nullptr);
    configurePlaySink();

    // On 1.4.2 and newer we use the audio-filter property instead.
    // See https://bugzilla.gnome.org/show_bug.cgi?id=735748 for
    // the reason for using >= 1.4.2 instead of >= 1.4.0.
    if (m_preservesPitch && webkitGstCheckVersion(1, 4, 2)) {
        GstElement* scale = gst_element_factory_make("scaletempo", 0);

        if (!scale)
            GST_WARNING("Failed to create scaletempo");
        else
            g_object_set(m_pipeline.get(), "audio-filter", scale, nullptr);
    }

    GRefPtr<GstPad> videoSinkPad = adoptGRef(gst_element_get_static_pad(m_videoSink.get(), "sink"));
    if (videoSinkPad)
        g_signal_connect(videoSinkPad.get(), "notify::caps", G_CALLBACK(mediaPlayerPrivateVideoSinkCapsChangedCallback), this);
}

void MediaPlayerPrivateGStreamer::simulateAudioInterruption()
{
    GstMessage* message = gst_message_new_request_state(GST_OBJECT(m_pipeline.get()), GST_STATE_PAUSED);
    gst_element_post_message(m_pipeline.get(), message);
}

bool MediaPlayerPrivateGStreamer::didPassCORSAccessCheck() const
{
    if (WEBKIT_IS_WEB_SRC(m_source.get()))
        return webKitSrcPassedCORSAccessCheck(WEBKIT_WEB_SRC(m_source.get()));
    return false;
}

bool MediaPlayerPrivateGStreamer::canSaveMediaData() const
{
    if (isLiveStream())
        return false;

    if (m_url.isLocalFile())
        return true;

    if (m_url.protocolIsInHTTPFamily())
        return true;

    return false;
}

}

#endif // USE(GSTREAMER)
