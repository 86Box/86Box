#include <mutex>
#include <thread>
#include <condition_variable>

#include <86box/plat.h>
#include <86box/thread.h>

struct event_cpp11_t
{
    std::condition_variable cond;
    std::mutex mutex;
    bool state = false;
};

extern "C" {

thread_t *
thread_create(void (*thread_rout)(void *param), void *param)
{
    auto thread = new std::thread([thread_rout, param] {
	thread_rout(param);
    });
    return thread;
}

int
thread_wait(thread_t *arg)
{
    auto thread = reinterpret_cast<std::thread*>(arg);
    thread->join();
    return 0;
}

mutex_t *
thread_create_mutex(void)
{
    auto mutex = new std::mutex;
    return mutex;
}

int
thread_test_mutex(mutex_t *_mutex)
{
    if (_mutex == nullptr)
	return 0;

    auto mutex = reinterpret_cast<std::mutex*>(_mutex);
    return mutex->try_lock() ? 1 : 0;
}

int
thread_wait_mutex(mutex_t *_mutex)
{
    if (_mutex == nullptr)
	return 0;

    auto mutex = reinterpret_cast<std::mutex*>(_mutex);
    mutex->lock();
    return 1;
}


int
thread_release_mutex(mutex_t *_mutex)
{
    if (_mutex == nullptr)
	return 0;

    auto mutex = reinterpret_cast<std::mutex*>(_mutex);
    mutex->unlock();
    return 1;
}


void
thread_close_mutex(mutex_t *_mutex)
{
    auto mutex = reinterpret_cast<std::mutex*>(_mutex);
    delete mutex;
}

event_t *
thread_create_event()
{
    auto ev = new event_cpp11_t;
    return ev;
}

int
thread_wait_event(event_t *handle, int timeout)
{
    auto event = reinterpret_cast<event_cpp11_t*>(handle);
    auto lock = std::unique_lock<std::mutex>(event->mutex);

    if (timeout < 0) {
        event->cond.wait(lock, [event] { return event->state; });
    } else {
        auto to = std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
        std::cv_status status;

        do {
            status = event->cond.wait_until(lock, to);
        } while ((status != std::cv_status::timeout) && !event->state);

        if (status == std::cv_status::timeout) {
            return 1;
        }
    }
    return 0;
}

void
thread_set_event(event_t *handle)
{
    auto event = reinterpret_cast<event_cpp11_t*>(handle);
    {
        auto lock = std::unique_lock<std::mutex>(event->mutex);
        event->state = true;
    }
    event->cond.notify_all();
}

void
thread_reset_event(event_t *handle)
{
    auto event = reinterpret_cast<event_cpp11_t*>(handle);
    auto lock = std::unique_lock<std::mutex>(event->mutex);
    event->state = false;
}

void
thread_destroy_event(event_t *handle)
{
    auto event = reinterpret_cast<event_cpp11_t*>(handle);
    delete event;
}

}
