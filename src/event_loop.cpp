#include "result.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <ctime>
#include <memory>
#include <sys/epoll.h>
#include <utility>

#include <event_loop.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace juan {

FDEventSlot::FDEventSlot(FDEventsList::iterator it, u32 gen) noexcept : it{it}, gen{gen} {}

EventLoop::EventLoop(shared_ptr<Logger>&& logger, FD epoll_fd, passkey<EventLoop>) noexcept
    : logger(std::move(logger)), free_slots{std::nullopt}, epoll_fd(epoll_fd) {}

Result<EventLoop> EventLoop::create(shared_ptr<Logger> logger) noexcept {
    using std::unexpected;

    FD epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        logger->error("Failed to create epoll: {}", strerror(errno));
        return unexpected(Status::EPOLL_CREATE_ERROR);
    }
    return Result<EventLoop>{std::in_place, std::move(logger), epoll_fd, passkey<EventLoop>{}};
}

Result<DeadlineEventHandle>
EventLoop::add_deadline_event(steady_clock::time_point deadline,
                              DeadlineEventCallback&& callback) noexcept {
    using std::unexpected;
    if (!callback) {
        logger->error("Attempt to add deadline event with empty callback");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    try {
        return {deadline_events.emplace(deadline, std::move(callback))};
    } catch (std::bad_alloc& ex) {
        logger->error("Failed to allocate deadline event: {}", ex.what());
        return unexpected(Status::ALLOC_ERROR);
    } catch (...) {
        logger->error("Unexpected error adding deadline event");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }
}

Result<void> EventLoop::remove_deadline_event(DeadlineEventHandle handle) noexcept {
    using std::unexpected;
    deadline_events.erase(handle);
    return {};
}

Result<FDEventHandle> EventLoop::add_fd_event(FD fd, FDEventFlags events,
                                              FDEventCallback&& callback) noexcept {
    using std::unexpected;
    if (!callback) {
        logger->error("Attempt to add fd event with empty callback");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }
    std::list<FDEvent>::iterator it;

    try {
        it = fd_events.emplace(fd_events.end(), fd, events, std::move(callback), false);
    } catch (std::bad_alloc& ex) {
        logger->error("Failed to allocate fd event: {}", ex.what());
        return unexpected(Status::ALLOC_ERROR);
    } catch (...) {
        logger->error("Unexpected error adding fd event");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    auto create_handle = [this](FDEventsList::iterator it) -> Result<FDEventHandle> {
        if (free_slots) {
            u32 idx = *free_slots;
            free_slots = fd_event_its[idx].next;
            std::destroy_at(&fd_event_its[idx].next);
            std::construct_at(&fd_event_its[idx].it, it);
            return Result<FDEventHandle>{std::in_place, idx, fd_event_its[idx].gen};
        } else {
            if (fd_event_its.size() == std::numeric_limits<u32>::max()) {
                logger->error("Failed to create fd event handle: too many handles");
                return unexpected(Status::EVENT_LOOP_ERROR);
            }
            u32 idx = static_cast<u32>(fd_event_its.size());
            try {
                fd_event_its.emplace_back(it, 0);
            } catch (std::bad_alloc& ex) {
                logger->error("Failed to allocate fd event handle: {}", ex.what());
                return unexpected(Status::ALLOC_ERROR);
            } catch (...) {
                logger->error("Unexpected error creating fd event handle");
                return unexpected(Status::EVENT_LOOP_ERROR);
            }
            return Result<FDEventHandle>{std::in_place, idx, 0};
        }
    };

    auto handle_res = create_handle(it);
    if (!handle_res) {
        fd_events.erase(it);
        return handle_res;
    }

    struct epoll_event event{};

    event.events = events;
    event.data.ptr = &*it;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        logger->info("Failed to add fd to epoll instance: {}", strerror(errno));
        fd_events.erase(it);
        FDEventSlot& slot = fd_event_its[handle_res->idx];
        std::destroy_at(&slot.it);
        std::construct_at(&slot.next, free_slots);
        free_slots = handle_res->idx;
        return unexpected(Status::EPOLL_ADD_ERROR);
    }

    return handle_res;
}

Result<void> EventLoop::modify_fd_event(FDEventHandle handle, FDEventFlags events) noexcept {
    using std::unexpected;

    if (handle.idx >= fd_event_its.size()) {
        logger->error("Attempt to modify invalid fd event handle");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    FDEventSlot& slot = fd_event_its[handle.idx];

    if (slot.gen != handle.gen) {
        logger->error("Attempt to modify stale fd event handle");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }
    struct epoll_event event{};

    FDEvent& fd_event = *slot.it;

    event.events = events;
    event.data.ptr = &fd_event;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd_event.fd, &event) == -1) {
        logger->error("Failed to modify fd event: {}", strerror(errno));
        return unexpected(Status::EPOLL_MOD_ERROR);
    }

    fd_event.events = events;

    return {};
}

Result<void> EventLoop::remove_fd_event(FDEventHandle handle) noexcept {
    using std::unexpected;

    if (handle.idx >= fd_event_its.size()) {
        logger->error("Attempt to remove invalid fd event handle");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    FDEventSlot& slot = fd_event_its[handle.idx];

    if (slot.gen != handle.gen) {
        logger->error("Attempt to remove stale fd event handle");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    auto delete_event = [&, this]() {
        FDEventSlot& slot = fd_event_its[handle.idx];
        if (running) {
            slot.it->removed = true;
            removed_events.splice(removed_events.end(), fd_events, slot.it);
        } else {
            fd_events.erase(slot.it);
        }
        slot.gen++;
        std::destroy_at(&slot.it);
        std::construct_at(&slot.next, free_slots);
        free_slots = handle.idx;
    };

    FDEvent& fd_event = *slot.it;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd_event.fd, nullptr) == -1) {
        logger->error("Failed to remove fd event: {}", strerror(errno));
        delete_event();
        return unexpected(Status::EPOLL_DEL_ERROR);
    }

    delete_event();

    return {};
}

Result<void> EventLoop::run_once() {
    using std::unexpected;
    if (running) {
        logger->error("Attempt to call run from a callback");
        return unexpected(Status::EVENT_LOOP_ERROR);
    }

    running = true;
    auto on_destroy = [this] {
        running = false;
        removed_events.clear();
    };
    struct RunningGuard {
        decltype(on_destroy)& func;
        ~RunningGuard() {
            func();
        }
    } guard(on_destroy);
    steady_clock::time_point now;

    auto handle_expired = [&]() {
        while (!deadline_events.empty() && deadline_events.begin()->first <= now) {
            auto cb = std::move(deadline_events.begin()->second);
            deadline_events.erase(deadline_events.begin());
            cb();
            now = steady_clock::now();
        }
    };

    struct timespec timeout{};
    auto calc_timeout = [&]() -> struct timespec* {
        now = steady_clock::now();
        handle_expired();
        if (!deadline_events.empty()) {
            auto nearest_deadline = deadline_events.begin()->first;
            auto rem = nearest_deadline - now;
            auto ns = std::chrono::ceil<std::chrono::nanoseconds>(rem);
            auto s = std::chrono::duration_cast<std::chrono::seconds>(ns);
            timeout.tv_sec = s.count();
            timeout.tv_nsec = (ns - s).count();
            return &timeout;
        } else {
            return {};
        }
    };

    auto alloc_events = [&, this] -> Result<void> {
        auto num_events = std::max(fd_events.size(), 1ZU);
        if (wait_events.size() < num_events)
            try {
                wait_events.resize(num_events);
            } catch (std::bad_alloc& ex) {
                logger->error("Failed to allocate epoll event vector: {}", ex.what());
                return unexpected(Status::ALLOC_ERROR);
            }
        return {};
    };

    int epoll_res = 0;
    struct timespec* timeout_ptr;
    timeout_ptr = calc_timeout();
    if (!alloc_events())
        return unexpected(Status::ALLOC_ERROR);
    while ((timeout_ptr || !fd_events.empty()) &&
           (epoll_res = epoll_pwait2(epoll_fd, wait_events.data(), wait_events.size(), timeout_ptr,
                                     nullptr)) == -1 &&
           errno == EINTR) {
        timeout_ptr = calc_timeout();
        if (!alloc_events())
            return unexpected(Status::ALLOC_ERROR);
        epoll_res = 0;
    }
    if (epoll_res == -1) {
        logger->error("Unexpected error in epoll wait: {}", strerror(errno));
        return unexpected(Status::EVENT_LOOP_ERROR);
    }
    for (int idx = 0; idx < epoll_res; ++idx) {
        FDEvent& event = *static_cast<FDEvent*>(wait_events[idx].data.ptr);
        if (event.removed)
            continue;
        event.callback(wait_events[idx].events);
    }
    now = steady_clock::now();
    handle_expired();
    return {};
}

EventLoop::EventLoop(EventLoop&& other) noexcept
    : logger(std::move(other.logger)), running(std::exchange(other.running, false)),
      deadline_events(std::move(other.deadline_events)), fd_events(std::move(other.fd_events)),
      removed_events(std::move(other.removed_events)), fd_event_its(std::move(other.fd_event_its)),
      wait_events(std::move(other.wait_events)),
      free_slots(std::exchange(other.free_slots, std::nullopt)),
      epoll_fd(std::exchange(other.epoll_fd, -1)) {
    assert(!running);
}

EventLoop& EventLoop::operator=(EventLoop&& other) noexcept {
    assert(!running);
    assert(!other.running);
    if (epoll_fd != -1) {
        close(epoll_fd);
    }

    logger = std::move(other.logger);
    running = std::exchange(other.running, false);
    deadline_events = std::move(other.deadline_events);
    fd_events = std::move(other.fd_events);
    removed_events = std::move(other.removed_events);
    fd_event_its = std::move(other.fd_event_its);
    wait_events = std::move(other.wait_events);
    free_slots = std::exchange(other.free_slots, std::nullopt);
    epoll_fd = std::exchange(other.epoll_fd, -1);
    return *this;
}

EventLoop::~EventLoop() noexcept {
    if (epoll_fd != -1) {
        close(epoll_fd);
    }
}

} // namespace juan
