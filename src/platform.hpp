// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// --------------------------------------------------------------------------
// PLATFORM: LINUX
// --------------------------------------------------------------------------
#ifdef PLATFORM_LINUX
#include <iostream>
#include <stdexcept>
#include <string>
#include <mutex>
#include <cstring>
#include <csignal>
#include <cerrno>

class Platform {
    static inline bool stopping_ = false;

    static inline void handler(int signo, siginfo_t *info, void *context) { stopping_ = true; }

public:
    Platform() {
        struct sigaction act = {0};
        act.sa_flags = SA_SIGINFO;
        act.sa_sigaction = handler;
        for (int sig : {SIGTERM, SIGINT, SIGQUIT, SIGHUP}) {
            if (sigaction(sig, &act, nullptr) < 0) {
                throw std::runtime_error("sigaction() = " + std::string(strerror(errno)));
            }
        }
    }
    Platform(Platform&) = delete;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;

    ~Platform() {
        std::cout << std::flush;
        std::cerr << std::flush;
        std::clog << std::endl << "Runtime destroyed." << std::endl;
    }

    static inline bool cooperate() noexcept {
        // Linux runs threads preemptively, no need to actually cooperate
        return !stopping_;
    }

    static const char *read_input(const char *hint) {
        static std::string content;
        std::cout << hint << ": ";
        std::getline(std::cin, content);
        return content.c_str();
    }

    static void clear_screen() { std::cout << "\033[H\033[2J\033[3J"; }
};

// --------------------------------------------------------------------------
// PLATFORM: WINDOWS
// --------------------------------------------------------------------------
#elif PLATFORM_WINDOWS
#include <iostream>
#include <mutex>
#include <string>
#include <stdexcept>
#include <winsock2.h>

class Platform {
public:
    Platform() {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("Failed to initialize WinSock");
        }
    }
    Platform(Platform&) = delete;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;

    ~Platform() {
        std::cout << std::flush;
        std::cerr << std::flush;
        std::clog << std::endl << "Runtime destroyed." << std::endl;
        WSACleanup();
    }

    static constexpr bool cooperate() noexcept {
        // Windows runs threads preemptively, no need to cooperate.
        // No signals to handle either, Windows doesn't support them.
        return true;
    }

    static const char *read_input(const char *hint) {
        static std::string content;
        std::cout << hint << ": ";
        std::getline(std::cin, content);
        return content.c_str();
    }

    static void clear_screen() { system("cls"); }
};

// --------------------------------------------------------------------------
// PLATFORM: NINTENDO 3DS
// --------------------------------------------------------------------------
#elif PLATFORM_3DS
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <cstring>
#include <malloc.h>
#include <3ds.h>

class Platform {
    u32 *soc_buffer_ = NULL;
    constexpr static auto SOC_ALIGN = 0x1000, SOC_BUFFERSIZE = 0x100000;

    [[noreturn]]
    static void custom_terminate() noexcept;

public:
    Platform() {
        std::set_terminate(custom_terminate);
        gfxInitDefault();
        consoleInit(GFX_TOP, NULL);
        aptInit();
        soc_buffer_ = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
        auto ret = socInit(soc_buffer_, SOC_BUFFERSIZE);
        if (ret != 0) {
            throw std::runtime_error("socInit() = " + std::to_string((unsigned)ret));
        }
    }
    Platform(Platform&) = delete;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;

    ~Platform() {
        aptSetHomeAllowed(false);
        std::cout << std::flush;
        std::cerr << std::flush;
        std::clog << std::endl << "Runtime destroyed." << std::endl;
        std::clog << "Press START to exit" << std::flush;
        for (u32 kDown; !(hidKeysDown() & KEY_START) && cooperate(); hidScanInput()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        aptExit();
        socExit();
        gfxExit();
    }

    static inline bool cooperate() noexcept { return aptMainLoop(); }

    static const char *read_input(const char *hint) {
        static SwkbdState swkbd;
        static char swkbd_buf[2048];
        // Read input
        memset(swkbd_buf, 0, sizeof(swkbd_buf));
        swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 3, sizeof(swkbd_buf));
        swkbdSetHintText(&swkbd, hint);
        swkbdInputText(&swkbd, swkbd_buf, sizeof(swkbd_buf));
        // Return input as string
        return swkbd_buf;
    }

    static void clear_screen() { consoleClear(); }
};

// --------------------------------------------------------------------------
// PLATFORM: GENERIC
// --------------------------------------------------------------------------
#else
#include <iostream>

class Platform {
public:
    Platform() {}
    Platform(Platform&) = delete;
    Platform(const Platform&) = delete;
    Platform(Platform&&) = delete;

    ~Platform() {
        std::cout << std::flush;
        std::cerr << std::flush;
        std::clog << std::endl << "Runtime destroyed." << std::endl;
    }

    static inline bool cooperate() noexcept { return true; }

    static const char *read_input(const char *hint) {
        static std::string content;
        std::cout << hint << ": ";
        std::getline(std::cin, content);
        return content.c_str();
    }

    static void clear_screen() { std::cout << "\033[H\033[2J\033[3J"; }
};

#endif

// --------------------------------------------------------------------------
// SHARED DEFINITIONS
// --------------------------------------------------------------------------

#if !defined(PLATFORM_WINDOWS)
#define MSG_FLAGS_OR_ZERO(...) __VA_ARGS__
#else
#define MSG_FLAGS_OR_ZERO(...) 0
#endif
