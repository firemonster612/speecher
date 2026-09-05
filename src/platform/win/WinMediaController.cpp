#include "platform/win/WinMediaController.h"

#include <QDebug>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/base.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace speecher {

using MediaSession = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using PlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

struct WinMediaState {
    std::atomic<quint64> generation{0};
    std::mutex mutex;
    std::vector<MediaSession> paused;
};

namespace {

winrt::fire_and_forget pauseSessions(std::shared_ptr<WinMediaState> state,
                                     quint64 generation)
{
    std::vector<MediaSession> paused;
    try {
        const auto manager = co_await winrt::Windows::Media::Control::
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        for (const MediaSession &session : manager.GetSessions()) {
            if (session.GetPlaybackInfo().PlaybackStatus() == PlaybackStatus::Playing
                && co_await session.TryPauseAsync()) {
                paused.push_back(session);
            }
        }
        if (state->generation.load() != generation) {
            for (const MediaSession &session : paused) {
                co_await session.TryPlayAsync();
            }
            co_return;
        }
        {
            const std::lock_guard lock(state->mutex);
            state->paused = paused;
        }
        qInfo() << "media paused sessions=" << paused.size();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "media pause failed:" << QString::fromWCharArray(error.message().c_str());
    }
}

winrt::fire_and_forget resumeSessions(std::shared_ptr<WinMediaState> state,
                                      quint64 generation,
                                      std::vector<MediaSession> sessions)
{
    try {
        for (const MediaSession &session : sessions) {
            co_await session.TryPlayAsync();
        }
        if (state->generation.load() == generation) {
            qInfo() << "media resumed sessions=" << sessions.size();
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "media resume failed:" << QString::fromWCharArray(error.message().c_str());
    }
}

} // namespace

WinMediaController::WinMediaController(QObject *parent)
    : MediaController(parent)
    , m_native(std::make_shared<WinMediaState>())
{
}

WinMediaController::~WinMediaController()
{
    ++m_native->generation;
}

void WinMediaController::pausePlaying()
{
    const quint64 generation = ++m_native->generation;
    {
        const std::lock_guard lock(m_native->mutex);
        m_native->paused.clear();
    }
    pauseSessions(m_native, generation);
}

void WinMediaController::resumePaused()
{
    const quint64 generation = ++m_native->generation;
    std::vector<MediaSession> sessions;
    {
        const std::lock_guard lock(m_native->mutex);
        sessions = std::move(m_native->paused);
    }
    if (!sessions.empty()) {
        resumeSessions(m_native, generation, std::move(sessions));
    }
}

} // namespace speecher
