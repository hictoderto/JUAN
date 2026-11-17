#ifndef JUAN_EVENT_LOOP_H
#define JUAN_EVENT_LOOP_H

#include "logger.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <map>

#include <passkey.h>
#include <result.h>
#include <types.h>

namespace juan {

using steady_clock = std::chrono::steady_clock;

using DeadlineEventCallback = std::move_only_function<void()>;

using DeadlineEventsMap = std::multimap<steady_clock::time_point, DeadlineEventCallback>;
using DeadlineEventHandle = DeadlineEventsMap::iterator;

using FD = int;
using FDEventFlags = uint32_t;

using FDEventCallback = std::move_only_function<void(FDEventFlags)>;

struct FDEvent {
    FD fd;
    FDEventFlags events;
    FDEventCallback callback;
    bool removed;
};

using FDEventsList = std::list<FDEvent>;

struct FDEventHandle {
    u32 idx;
    u32 gen;
};

struct FDEventSlot {
    union {
        FDEventsList::iterator it{};
        opt<u32> next;
    };
    u32 gen{};

    FDEventSlot(FDEventsList::iterator it, u32 gen) noexcept;
};

class EventLoop {
    shared_ptr<Logger> logger;

    bool running = false;

    DeadlineEventsMap deadline_events;

    FDEventsList fd_events;
    FDEventsList removed_events;
    vec<FDEventSlot> fd_event_its;
    vec<struct epoll_event> wait_events;

    opt<u32> free_slots;

    FD epoll_fd;

  public:
    EventLoop(shared_ptr<Logger>&& logger, FD epoll_fd, passkey<EventLoop>) noexcept;

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    EventLoop(EventLoop&& other) noexcept;
    EventLoop& operator=(EventLoop&& other) noexcept;

    [[nodiscard]] Result<DeadlineEventHandle>
    add_deadline_event(steady_clock::time_point deadline,
                       DeadlineEventCallback&& callback) noexcept;

    [[nodiscard]] Result<void> remove_deadline_event(DeadlineEventHandle handle) noexcept;

    [[nodiscard]] Result<FDEventHandle> add_fd_event(FD fd, FDEventFlags events,
                                                     FDEventCallback&& callback) noexcept;

    [[nodiscard]] Result<void> modify_fd_event(FDEventHandle handle, FDEventFlags events) noexcept;

    [[nodiscard]] Result<void> remove_fd_event(FDEventHandle handle) noexcept;

    [[nodiscard]] Result<void> run_once();

    static Result<EventLoop> create(shared_ptr<Logger> logger) noexcept;

    ~EventLoop() noexcept;
};

} // namespace juan

#endif /* ifndef JUAN_EVENT_LOOP_H */
