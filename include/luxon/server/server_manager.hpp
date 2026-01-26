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

    void run();
    bool run_once();
    void stop() { running_ = false; }

    void add_scheduled_task(unsigned delay_ms, std::function<void()>&& callback) {
        unsigned target_time = startup_time_.get() + delay_ms;
        scheduled_tasks_.push({target_time, std::move(callback)});
    }

#ifdef LUXON_SERVER_ENABLE_PLUGINS
    // Only callable on main thread inside of a coroutine
    bool call_in_new_thread(std::move_only_function<void()>&& fn);
    void delay(unsigned milliseconds);

    // This is the only function that is safe to call from other threads
    void enqueue_in_main_loop(std::move_only_function<void()>&& fn) {
        std::scoped_lock L(main_loop_calls_mutex_);
        main_loop_calls_.emplace(std::move(fn));
    }
#endif

    const std::string& get_endpoint_of(ServerType server_type);

    const std::list<HandlerPtr<HandlerBase>>& get_connections() { return connections_; }

    size_t get_connection_count() { return connections_.size(); }
    template <class HandlerT> size_t get_connection_count() {
        size_t fres = 0;
        for (const auto& connection : connections_)
            fres += !!dynamic_cast<HandlerT *>(connection.get());
        return fres;
    }
};
} // namespace server
