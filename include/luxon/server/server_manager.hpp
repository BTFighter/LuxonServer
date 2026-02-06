// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "global.hpp"
#include "metrics.hpp"
#include "handler_base.hpp"
#include "string_hash.hpp"
#include "logger.hpp"
#ifndef LUXON_SERVER_POLL
#include "sock_selector.hpp"
#endif
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
#include "http_server.hpp"
#endif

#include <string>
#include <vector>
#include <list>
#include <queue>
#include <unordered_map>
#include <optional>
#include <memory>
#include <utility>
#include <functional>
#ifdef LUXON_SERVER_ENABLE_PLUGINS
#include <mutex>
#endif
#include <cstdint>
#include <luxon/enet_peer.hpp>
#include <commoncpp/timer.hpp>

namespace server {
class HandlerBase;
struct Peer;
struct PeerPersistent;
class App;

#ifdef LUXON_SERVER_ENABLE_PLUGINS
template <typename T> using HandlerPtr = std::shared_ptr<T>;
#else
template <typename T> using HandlerPtr = std::unique_ptr<T>;
#endif

enum class ServerType { None, NameServer, MasterServer, GameServer };

struct ServerConfig {
    ServerType type;
    uint16_t port;
};

struct ServerEndpoint {
    ServerType type;
    std::string address;
    bool external;
};

class ServerManager {
    struct ScheduledTask {
        unsigned execution_time;
        std::function<void()> cb;
        bool operator>(const ScheduledTask& other) const { return execution_time > other.execution_time; }
    };

public:
    std::unordered_map<std::pair<std::string, std::string>, std::weak_ptr<App>, StringPairHasher> apps;
    std::vector<std::unique_ptr<PeerPersistent>> peer_persistent_data;
    std::vector<ServerEndpoint> endpoints;

private:
#ifndef LUXON_SERVER_POLL
    SockSelector sock_selector_;
#endif

    std::shared_ptr<logger> log_;
    std::vector<ServerConfig> configs_;
    std::unordered_map<uint16_t, enet::EnetServer> servers_;
    std::list<HandlerPtr<HandlerBase>> connections_;
    std::priority_queue<ScheduledTask, std::vector<ScheduledTask>, std::greater<ScheduledTask>> scheduled_tasks_;
#ifdef LUXON_SERVER_ENABLE_PLUGINS
    std::queue<std::move_only_function<void()>> main_loop_calls_;
    std::mutex main_loop_calls_mutex_;
#endif

    common::Timer startup_time_;
    common::Timer last_slow_update_;

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    std::optional<HttpServer> http_server_;
#endif

    bool running_;

    void setup();

    void run_scheduled_tasks();

public:
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
    Metric busy_time, idle_time;
#endif

    ServerManager(const std::string& config_file);

    ///
    /// \brief Runs server until stop() is called
    ///
    void run();
    ///
    /// \brief Ticks once
    /// \return True if the server should continue running, false if stop() was called
    /// \note Non-blocking if polling is enabled
    ///
    bool run_once();
    ///
    /// \brief Signals the server to stop running asap
    /// \note It might take the server up to about 125 milliseconds to stop if polling is disabled
    ///
    void stop() { running_ = false; }

    ///
    /// \brief Schedules a function to be called from main loop
    /// \param delay_ms Delay in milliseconds
    /// \param callback Function to be called
    ///
    void add_scheduled_task(unsigned delay_ms, std::function<void()>&& callback) {
        unsigned target_time = startup_time_.get() + delay_ms;
        scheduled_tasks_.push({target_time, std::move(callback)});
    }

#ifdef LUXON_SERVER_ENABLE_PLUGINS
    ///
    /// \brief Calls the given function in a newly created thread
    /// \param fn Function to call in new thread
    /// \return True if thread creation was successful and function didn't throw, otherwise false
    /// \note Non-blocking; Can only be used from inside of a coroutine
    ///
    bool call_in_new_thread(std::move_only_function<void()>&& fn);
    ///
    /// \brief Delays execution
    /// \param milliseconds Amount of milliseconds until function returns
    /// \return True if at least the given amount of time has elapsed, otherwise false
    /// \note Non-blocking; Can only be used from inside of a coroutine
    /// \note Might wait up to around 125 milliseconds longer than specified if polling is disabled
    ///
    bool delay(unsigned milliseconds);

    ///
    /// \brief Enqueues a function to be called from main loop, on main thread
    /// \param fn Function to be called
    /// \note This function is thread-safe
    ///
    void enqueue_in_main_loop(std::move_only_function<void()>&& fn) {
        std::scoped_lock L(main_loop_calls_mutex_);
        main_loop_calls_.emplace(std::move(fn));
    }
#endif

    ///
    /// \brief Gets the external address of a random server of given type
    /// \param server_type Type of server to request
    /// \return External address of server, e.g. "127.0.0.1:5058"
    ///
    const std::string& get_endpoint_of(ServerType server_type);

    ///
    /// \brief Gets a list of active connections to this instance
    /// \return Linked list of handlers representing a connection
    ///
    const std::list<HandlerPtr<HandlerBase>>& get_connections() { return connections_; }

    ///
    /// \brief Counts connections to servers on this instance of any type
    /// \return Amount of connections
    ///
    size_t get_connection_count() { return connections_.size(); }
    ///
    /// \brief Counts connections to servers on this instance with handler of or derived from given type
    /// \return Amount of connections
    ///
    template <class HandlerT> size_t get_connection_count() {
        size_t fres = 0;
        for (const auto& connection : connections_)
            fres += !!dynamic_cast<HandlerT *>(connection.get());
        return fres;
    }
};
} // namespace server
