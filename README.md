# Luxon Server

Luxon Server is a clean-room implementation of the Photon LoadBalancing server. It is built on top of the Luxon project, which provides the necessary reimplementation of the ENet protocol and Photon's binary serialization format.
The goal of this project is to be a drop-in replacement for the official server for multiplayer games that utilize Photon. It aims to support games out of the box, provided they do not rely on complex server-side plugins, though a plugin system is available if needed.
This entire project was pulled up in about two weeks, so while it is functional, expect it to be fast moving.

## Legal Disclaimer

> Luxon Server is an independent, open-source project developed for educational, interoperability and game preservation purposes. It is strictly a clean-room implementation.
> 
> This project is **not** affiliated with, endorsed by, sponsored by, or authorized by Exit Games Inc., Exit Games GmbH, or any of their subsidiaries. "Photon", "Photon Engine", and "Exit Games" are trademarks or registered trademarks of Exit Games. All trademarks and registered trademarks are the property of their respective owners. Use of these names does not imply any affiliation with or endorsement by them.

## Compatibility

Most games using standard matchmaking logic (joining lobbies, creating rooms, random matching) should work immediately without modification to the game client or server configuration.\
Chat opcodes aren't implemented yet.

## Getting Started

### WORK IN PROGRESS

I haven't open sourced Luxon itself yet. Unfortunately **that makes building this project impossible right now**. I'm planning to open source Luxon until Sunday.

### Prerequisites

To build Luxon Server, you will need:

* CMake 3.16 or higher
* A C++ compiler and standard library capable of supporting **C++23**

### Building

The project uses standard CMake build procedures.

```bash
git submodule update --init --depth 1 --recursive
mkdir build
cd build
cmake ..
cmake --build .
```

### Configuration

The server is configured via a config.yml file. A config.example.yml is provided in the repository.
The configuration defines the listening ports for the three main server components:

1. **NameServer:** Handles initial region requests (ignored for now) and authentication (stubbed for now).
2. **MasterServer:** Handles lobbies and matchmaking.
3. **GameServer:** Hosts the actual room logic and relay.

By default, an HTTP server is also available to provide a web-based dashboard for monitoring connections and server load.

## Usage

The easiest way to use Luxon Server with an existing game is to redirect the game's DNS requests to your local machine (or wherever you are hosting the server).
You do not need to patch the game executable. Instead, add an entry to your hosts file (or configure your router's DNS) to point the standard Photon domains to your server IP.\
For example, if your server is running on 192.168.1.56:

```
192.168.1.56 ns.exitgames.com ns.photonengine.io
```

Once this is set, the game will connect to Luxon Server thinking it is the official cloud.

## Features

* **LoadBalancing Logic:** Full implementation of the Name/Master/Game server flow.
* **Web Dashboard:** An embedded HTTP server (default port 5088) provides a real-time monitor. It shows active connections, packet loss, round-trip times, and a visual graph of server load/busy time at path `/stats`.
* **Peer Persistence:** Handles player authentication tokens and state transfer between Master and Game servers.
* **Plugin System (Optional):** If you need custom server-side logic, Luxon supports plugins written in C++ using coroutines. This is disabled by default in CMake (LUXON_SERVER_ENABLE_PLUGINS=OFF) to keep the build lightweight, strictly single-threaded and coroutine-free.

## Platform Support

The server is primarily developed for Linux and Windows. There is also full support (excluding plugins) for Nintendo 3DS (because why not).

## FAQ

**Q:** Why is the server completely single-threaded?\
**A:** Luxon Server is NOT supposed to be used as an alternative the the official LoadBalancing software. That means it doesn't have to handle loads big enough to saturate a single core even on very low-end systems. I have estimated the *New Nintendo 3DS* as a server to be able to handle at least 10, probably up to 30 concurrently active players! Plus, strict single-threading keeps the codebase simple.

**Q:** Are there any plans on implementing *actual* load balancing (not just the protocol) across multiple systems/processes?\
**A:** I have looked into spawning more processes running GameServer instances, as an alternative to multi-threading. However, I am strictly against supporting load balancing across different systems. I do NOT want to agitate Exit Games by releasing a competitive product.

**Q:** Are you going to write bindings for writing plugins in C#, Python, Javascript, ...?\
**A:** No. First of all, the project is moving very quickly right now. These bindings would need constant updating and maintenance. Feel free to contribute your own bindings.

**Q:** Isn't it a bad idea to provide blocking functions (functions that only return when an operation has completed) in `ServerManager` when the server is purely single-threaded?\
**A:** This is fairly well hidden, but plugins actually always run in coroutines. These functions suspend the coroutine until the work is complete.
