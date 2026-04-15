// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

/*
 * MACRO CONFIGURATION
 * ---------------------------
 * This file relies on the following preprocessor macros to control build variants,
 * memory management strategies, and I/O models:
 *
 * LUXON_SERVER_ENABLE_PLUGINS
 * Enables asynchronous plugin architecture.
 * - Switches connection handlers (HANDLER_PTR) from std::unique_ptr to std::shared_ptr
 *   to support shared ownership required by coroutines.
 * - Wraps incoming ENet packet handling (HandleENetCommand) inside a coroutine
 *   (minicoro), allowing handlers to yield execution without blocking the server.
 * - Enables the `main_loop_calls_` queue to safely execute tasks scheduled from
 *   coroutines onto the main thread.
 *
 * NDEBUG
 * Standard C++ macro for Release builds.
 * - If UNDEFINED (Debug build):
 * 1. Sets logger levels to `trace` for high verbosity.
 * 2. Enables expensive packet visualization (visualizer::print_ser_message)
 * and hex dumps for every incoming ENet command to aid protocol debugging.
 *
 * LUXON_SERVER_ENABLE_WEBSERVER
 * Toggles the embedded HTTP server component.
 * - Parses the "HTTP" section of the YAML configuration.
 * - Instantiates the `http_server_` and manages its lifecycle.
 * - Collects performance metrics (`idle_time`, `busy_time`) to be served via HTTP.
 * - Hooks HTTP socket descriptors into the main loop (if not polling).
 *
 * LUXON_SERVER_POLL
 * Determines the network I/O multiplexing strategy.
 * - If DEFINED: Uses a polling model. The `sock_selector_` logic is skipped,
 *   and the server presumably relies on non-blocking checks or external polling.
 * - If UNDEFINED: Uses an event-driven model via `sock_selector_` (wrapping select/epoll).
 *   It registers file descriptors and blocks execution in `run_once` until sockets are
 *   readable.
 */

#include "server_manager.hpp"
#include "global.hpp"
#include "platform.hpp"
#include "peer.hpp"
#include "logger.hpp"
#include "handler_nameserver.hpp"
#include "handler_masterserver.hpp"
#include "handler_gameserver.hpp"
#include "yaml.hpp"

#include <iostream>
#include <string_view>
#include <fstream>
#include <sstream>
#include <format>
#include <random>
#include <exception>
#include <stdexcept>
#include <algorithm>
#ifdef LUXON_SERVER_ENABLE_PLUGINS
#include <thread>
#include <minicoropp.hpp>
#endif
#include <luxon/ser_gp_binary_v18.hpp>
#include <luxon/ser_encryption.hpp>
#include <luxon/visualizer.hpp>
#include <tracy/Tracy.hpp>

namespace server {
namespace {
ServerType StringToServerType(const std::string& str) {
    if (str == "NameServer")
        return ServerType::NameServer;
    if (str == "MasterServer")
        return ServerType::MasterServer;
    if (str == "GameServer")
        return ServerType::GameServer;

    // Throw error
    throw std::runtime_error("Unknown ServerType: " + str);
}

ServerProtocol StringToEndpointProtocol(const std::string& str) {
    if (str == "UDP")
        return ServerProtocol::UDP;
    if (str == "TCP")
        return ServerProtocol::TCP;
    if (str == "WebSocket")
        return ServerProtocol::WebSocket;

    // Throw error
    throw std::runtime_error("Unknown protocol: " + str);
}

std::string LoadFile(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f)
        throw std::runtime_error("Failed to open config file: " + filename);
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

std::string_view ServerTypeToString(ServerType type) {
    switch (type) {
    case ServerType::NameServer:
        return "NameServer";
    case ServerType::MasterServer:
        return "MasterServer";
    case ServerType::GameServer:
        return "GameServer";
    default:
        return "Unknown???";
    }
}

HandlerPtr<HandlerBase> ServerTypeToHandler(ServerType type, ServerManager& server_man, std::shared_ptr<Peer>&& peer) {
    switch (type) {
    case ServerType::NameServer:
        return HandlerPtr<NameServerHandler>(new NameServerHandler(server_man, peer));
    case ServerType::MasterServer:
        return HandlerPtr<MasterServerHandler>(new MasterServerHandler(server_man, peer));
    case ServerType::GameServer:
        return HandlerPtr<GameServerHandler>(new GameServerHandler(server_man, peer));
    default:
        return nullptr;
    }
}
} // namespace

ServerManager::ServerManager(const std::string& config_file) {
    log_ = create_logger("ServerManager");
#ifndef NDEBUG
    log_->set_level(log_level::trace);
#endif

    log_->debug("Reading config file contents");
    std::string contents = LoadFile(config_file);

    // Parse YAML
    log_->info("Parsing config file");
    Yaml::Node root;

    try {
        // MiniYaml parses the string directly
        Yaml::Parse(root, contents);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("YAML Parsing failed: ") + e.what());
    }

    if (!root.IsMap())
        throw std::runtime_error("Root of config must be a map");

    // Iterate over the top-level sections (NameServer, MasterServer, etc.)
    log_->info("Extracting data from config file");

    for (auto it = root.Begin(); it != root.End(); it++) {
        std::string key = (*it).first;
        Yaml::Node& section = (*it).second;

        // Handle known server types (NameServer, MasterServer, GameServer)
        bool isKnownType = (key == "NameServer" || key == "MasterServer" || key == "GameServer");

        if (isKnownType) {
            ServerType currentType = StringToServerType(key);

            if (section.IsSequence()) {
                for (auto itemIt = section.Begin(); itemIt != section.End(); itemIt++) {
                    Yaml::Node& item = (*itemIt).second;

                    if (!item["port"].IsNone()) {
                        uint16_t port = item["port"].As<uint16_t>();
                        configs_.push_back({currentType, port});
                    }
                    if (!item["address"].IsNone()) {
                        std::string addr = item["address"].As<std::string>();
                        endpoints.push_back({currentType, ServerProtocol::UDP, addr});
                    }
                }
            }
        }
        // Handle "External" Section
        else if (key == "External") {
            if (section.IsSequence()) {
                for (auto itemIt = section.Begin(); itemIt != section.End(); itemIt++) {
                    Yaml::Node& item = (*itemIt).second;

                    ServerType extType = ServerType::None;
                    ServerProtocol extProto = ServerProtocol::UDP;
                    std::string extAddr;
                    bool addrFound = false;

                    if (!item["type"].IsNone()) {
                        std::string typeStr = item["type"].As<std::string>();
                        extType = StringToServerType(typeStr);
                    }
                    if (!item["protocol"].IsNone()) {
                        std::string protoStr = item["protocol"].As<std::string>();
                        extProto = StringToEndpointProtocol(protoStr);
                    }
                    if (!item["address"].IsNone()) {
                        extAddr = item["address"].As<std::string>();
                        addrFound = true;
                    }

                    if (extType != ServerType::None && addrFound)
                        endpoints.push_back({extType, extProto, extAddr});
                }
            }
        }
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Handle "Http Server" Section
        else if (key == "HTTP") {
            bool httpActive = false;
            std::string httpAddress = "0.0.0.0";
            uint16_t httpPort = 5088;

            if (section.IsSequence()) {
                for (auto itemIt = section.Begin(); itemIt != section.End(); itemIt++) {
                    Yaml::Node& item = (*itemIt).second;

                    if (!item["active"].IsNone())
                        httpActive = item["active"].As<bool>();
                    if (!item["address"].IsNone())
                        httpAddress = item["address"].As<std::string>();
                    if (!item["port"].IsNone())
                        httpPort = item["port"].As<uint16_t>();
                }
            }

            if (httpActive) {
                log_->info("Initializing HTTP Server on port {}", httpPort);
                http_server_.emplace(*this);
#ifndef LUXON_SERVER_POLL
                http_server_->on_create_fd = std::bind(&SockSelector::add_read_fd, &sock_selector_, std::placeholders::_1);
                http_server_->on_delete_fd = std::bind(&SockSelector::remove_read_fd, &sock_selector_, std::placeholders::_1);
#endif
                http_server_->bind(httpAddress, httpPort);
            } else {
                log_->debug("HTTP Server disabled in config.");
            }
        }
#endif
    }

    log_->info("Config looks alright, setting up accordingly");
    setup();
}

#ifdef LUXON_SERVER_ENABLE_PLUGINS
bool ServerManager::call_in_side_thread(const SideThreadPtr& side_thread, std::move_only_function<void()>&& fn) {
    if (!side_thread)
        return false;

    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    bool ok = false;
    side_thread->enqueue([&, this] {
        try {
            fn();
            ok = true;
        } catch (const std::exception& e) {
            log_->error("Unhandled exception in side thread: {}: {}", typeid(e).name(), e.what());
        } catch (...) {
            log_->error("Unknown unhandled exception in side thread!");
        }

        enqueue_in_main_loop([coro, this] {
            if (!coro->resume())
                log_->error("Failed to resume coroutine from side thread!");
        });
    });

    coro->yield();
    return ok;
}

bool ServerManager::call_in_new_thread(std::move_only_function<void()>&& fn) {
    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    bool ok = false;
    std::thread([&, this] {
        try {
            fn();
            ok = true;
        } catch (const std::exception& e) {
            log_->error("Unhandled exception in side thread: {}: {}", typeid(e).name(), e.what());
        } catch (...) {
            log_->error("Unknown unhandled exception in side thread!");
        }

        enqueue_in_main_loop([coro, this] {
            if (!coro->resume())
                log_->error("Failed to resume coroutine from side thread!");
        });
    }).detach();

    coro->yield();
    return ok;
}

bool ServerManager::delay(unsigned int milliseconds) {
    auto *coro = minicoro::Coroutine::current();
    if (!coro)
        return false;

    add_scheduled_task(milliseconds, [coro, this] {
        if (!coro->resume())
            log_->error("Failed to resume coroutine from scheduled task!");
    });

    coro->yield();
    return true;
}
#endif

const std::string& ServerManager::get_endpoint_of(ServerType server_type, ServerProtocol server_proto) {
    ZoneScoped;

    std::vector<const std::string *> candidates;
    candidates.reserve(endpoints.size());

    // Collect all valid addresses for the requested type
    for (const auto& endpoint : endpoints)
        if (endpoint.type == server_type && endpoint.protocol == server_proto)
            candidates.push_back(&endpoint.address);

    // Handle cases where no config exists
    if (candidates.empty())
        throw std::runtime_error(std::format("No endpoint configuration found for {}", ServerTypeToString(server_type)));

    // Return a random address from the candidates
    static std::mt19937 generator{1234};
    std::uniform_int_distribution<size_t> distribution(0, candidates.size() - 1);

    return *candidates[distribution(generator)];
}

void ServerManager::run_scheduled_tasks() {
    ZoneScoped;

    // Check if queue is empty first to avoid segfaults on top()
    if (scheduled_tasks_.empty())
        return;

    const auto& task = scheduled_tasks_.top();
    if (task.execution_time < startup_time_.get()) {
        auto callback = task.cb;
        scheduled_tasks_.pop();
        callback();
    }
}

void ServerManager::run() {
    // Main Service Loop
    running_ = true;
    do {
        run_once();
    } while (running_ && Platform::cooperate());
    running_ = true;
}

bool ServerManager::run_once() {
    ZoneScoped;

    {
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Start idle performance timer
        const auto start_time = std::chrono::steady_clock::now();
#endif

#ifndef LUXON_SERVER_POLL
        // Run sock selector
        sock_selector_.run(125);
#endif

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // End idle performance timer
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        // Store metric
        idle_time.add(static_cast<unsigned>(duration));
#endif
    }

    {
#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Start busy performance timer
        const auto start_time = std::chrono::steady_clock::now();
#endif

        // Check if slow update should be done
        const bool slow_update = last_slow_update_.get() > 250;
        if (slow_update)
            last_slow_update_.reset();

#ifdef LUXON_SERVER_ENABLE_PLUGINS
        // Run stuff that's queued to run in the main loop asap
        while (true) {
            // Get next callback safely
            std::move_only_function<void()> fn{};
            {
                std::scoped_lock L(main_loop_calls_mutex_);
                if (!main_loop_calls_.empty()) {
                    fn = std::move(main_loop_calls_.front());
                    main_loop_calls_.pop();
                }
            }

            if (fn)
                fn();
            else
                break;
        }
#endif

        // Dispatch and handle incoming application messages
#ifndef LUXON_SERVER_POLL
        const auto& readable_socks = sock_selector_.get_readable_socks();
#endif
        for (auto& [port, server] : servers_) {
            try {
#ifndef LUXON_SERVER_POLL
                if (std::ranges::contains(readable_socks, server.native_handle()))
#endif
                    server.service_self();
                if (!server.service_peers())
                    log_->warn("Queueing UDP datagrams!");
            } catch (const std::exception& e) {
                log_->warn("Uncaught exception: {}", e.what());
            }
        }

        // Trigger updates
        for (auto& connection : connections_) {
            try {
                connection->HandleUpdate();
            } catch (const std::exception& e) {
                auto& peer = *connection->get_peer();
                log_->warn("Disconnecting due to uncaught exception in update: {}", e.what());
                peer.disconnect();
            }
        }

        if (slow_update) {
            for (auto& connection : connections_) {
                try {
                    connection->HandleSlowUpdate();
                } catch (const std::exception& e) {
                    auto& peer = *connection->get_peer();
                    log_->warn("Disconnecting due to uncaught exception in slow update: {}", e.what());
                    peer.disconnect();
                }
            }
        }

        // Run scheduled tasks
        run_scheduled_tasks();

#ifdef LUXON_SERVER_ENABLE_WEBSERVER
        // Update HTTP server
        if (http_server_)
#ifndef LUXON_SERVER_POLL
            http_server_->service(readable_socks);
#else
            http_server_->service();
#endif

        // End busy performance timer
        const auto end_time = std::chrono::steady_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

        // Store metric
        busy_time.add(static_cast<unsigned>(duration));
#endif
    }

    FrameMark;
    return running_;
}

void ServerManager::setup() {
    ZoneScoped;

    enet::EnetPeerConfig cfg;
    cfg.time_base = enet::EnetPeer::create_time_base();
    cfg.time_ping_interval_ms = 1000;
    cfg.disconnect_timeout_ms = 5000;

    // Create servers
    for (const auto& config : configs_) {
        // Create enet server and configure it
        log_->info("Setting up {} on port {}", ServerTypeToString(config.type), config.port);

        auto& server = servers_.try_emplace(config.port, cfg).first->second;

        server.on_peer_connected = [this, server_type = config.type](std::shared_ptr<enet::EnetPeer> enetPeer) {
            // Construct peer
            auto peer = std::make_shared<Peer>();
            peer->enet_peer = enetPeer;
            peer->log = create_logger(std::format("Peer {}@{}", enetPeer->peer_id(), enetPeer->remote_endpoint()->to_string()));
            peer->protocol = std::make_unique<ser::GpBinaryV18>(); // Default version
#ifndef NDEBUG
            peer->log->set_level(log_level::trace);
#endif
            peer->log->info("Peer {} constructed with {} handler", peer->enet_peer->peer_id(), ServerTypeToString(server_type));

            // Construct handler
            auto handler = ServerTypeToHandler(server_type, *this, std::move(peer));

            // Handler pointer must be owning with plugins enabled to ensure no destruction while coroutine is active
#ifdef LUXON_SERVER_ENABLE_PLUGINS
            auto& handler_ptr = handler;
#else
            auto *handler_ptr = handler.get();
#endif

            // Install handlers
            enetPeer->on_state_changed = [this, handler = handler.get()](enet::EnetConnectionState state) {
                ZoneScopedN("on_state_changed");

                try {
                    handler->HandleENetConnectionStateChange(state);
                } catch (const std::exception& e) {
                    auto& peer = *handler->get_peer();
                    peer.log->warn("Uncaught exception in ENet connect state change handler: {}", e.what());
                }

                if (state == enet::EnetConnectionState::Disconnected) {
                    handler->HandleDisconnect();
                    // Self-destruct handler, this will invalidate the pointer
                    connections_.remove_if([handler](auto& v) { return v.get() == handler; });
                }
            };

            enetPeer->on_payload_command = [this, handler = handler_ptr](const enet::EnetCommand& cmd) {
                ZoneScopedN("on_payload_command");

                auto& peer = handler->get_peer();
#ifndef NDEBUG
                peer->log->trace("Received message using mode {} on channel {}:", static_cast<int>(enet::FlagsToEnetDeliveryMode(cmd.header.flags)),
                                 cmd.header.channel_id);
                if (!visualizer::print_ser_message(cmd.payload, 2, *peer->protocol)) {
                    if (!visualizer::print_http_message(cmd.payload, 2)) {
                        peer->log->error("Message not understood!");
                        visualizer::helpers::print_hex_dump(cmd.payload, 2);
                    }
                }
#endif
#ifdef LUXON_SERVER_ENABLE_PLUGINS
                if (!(new minicoro::Coroutine([handler, cmd, this](minicoro::Coroutine& coro) {
                         // Make coroutine own itself
                         auto owned_coro =
                             std::unique_ptr<minicoro::Coroutine, std::function<void(minicoro::Coroutine *)>>(&coro, [this](minicoro::Coroutine *coro) {
                                 // Discard coroutine on main thread, where it can be destroyed safely
                                 enqueue_in_main_loop([coro] { delete coro; });
                             });
#endif

                try {
                    handler->HandleENetCommand(cmd);
                } catch (const std::exception& e) {
                    auto& peer = *handler->get_peer();
                    peer.log->critical("Disconnecting due to uncaught exception in ENet command handler: {}", e.what());
                    peer.disconnect();
                }

#ifdef LUXON_SERVER_ENABLE_PLUGINS
                     }))->resume()) {
                    peer->log->critical("Disconnecting because ENet command handler coroutine couldn't be started");
                    peer->disconnect();
                }
#endif
            };

            // Add to connection list
            auto& handlerPtr = connections_.emplace_back(std::move(handler));

            // Tell handler that we're connected
            handlerPtr->HandleConnect();
        };

        // Make server ready for listening
        log_->info("Starting {} on port {}", ServerTypeToString(config.type), config.port);

        if (!server.bind(config.port)) {
            log_->error("Failed to bind {} to port {}!", ServerTypeToString(config.type), config.port);
            continue;
        }

        // Add server to sock selector
#ifndef LUXON_SERVER_POLL
        if (!sock_selector_.add_read_fd(server.native_handle()))
            log_->error("Failed to add new server to sock selector!", ServerTypeToString(config.type), config.port);
#endif
    }
}
} // namespace server
