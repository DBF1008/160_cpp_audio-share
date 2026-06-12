#include "audio_manager.hpp"

#include <thread>

namespace {
// Join a worker thread, unless we are running on that very thread (which can
// happen when the manager's last owner is the worker's own captured `self`):
// joining self would deadlock, so detach instead. By the time that situation
// can arise the worker's main function has already returned, so detaching is
// safe.
void join_or_detach_self(std::thread& t)
{
    if (!t.joinable()) {
        return;
    }
    if (t.get_id() == std::this_thread::get_id()) {
        t.detach();
    } else {
        t.join();
    }
}
} // namespace

audio_manager::audio_manager()
{
    _format = std::make_unique<AudioFormat>();
}

audio_manager::~audio_manager()
{
    // Ensure the recording thread is stopped and joined before this object (and
    // its platform backend in the base class) is destroyed, so a still-joinable
    // std::thread can never call std::terminate() from its destructor.
    stop();
}

void audio_manager::start_loopback_recording(std::shared_ptr<network_manager> network_manager, const capture_config& config)
{
    // Defensive: never move-assign onto a still-joinable thread, which would
    // call std::terminate(). If a previous recording was left running (e.g. a
    // start that was not cleanly stopped), stop it before starting a new one.
    if (_record_thread.joinable()) {
        _stopped = true;
        join_or_detach_self(_record_thread);
    }
    _stopped = false;
    _record_thread = std::thread([network_manager = network_manager, config = config, self = shared_from_this()] {
        self->do_loopback_recording(network_manager, config);
    });
}

void audio_manager::stop()
{
    // Idempotent: safe to call when recording never started or was already
    // stopped.
    _stopped = true;
    join_or_detach_self(_record_thread);
}

std::string audio_manager::get_format_binary()
{
    return _format->SerializeAsString();
}