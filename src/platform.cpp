// Copyright (c) 2026, the Luxon Server contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "platform.hpp"

#ifdef PLATFORM_3DS
#include <string>
#include <exception>
#include <cerrno>
#include <3ds.h>

void Platform::custom_terminate() noexcept {
    // Get error message
    std::string message;
    try {
        std::rethrow_exception(std::current_exception());
    } catch (const std::exception& e) {
        message = e.what();
    } catch (...) {
        message = "Unknown";
    }
    // Display error
    errorConf conf = {.type = ERROR_TEXT_WORD_WRAP,
                      .errorCode = errno,
                      .upperScreenFlag = ERROR_NORMAL,
                      .useLanguage = CFG_LANGUAGE_EN,
                      .Text = {L'I', L'N', L'V', L'A', L'L', L'I', L'D', L'\0'},
                      .homeButton = true,
                      .softwareReset = false,
                      .appJump = false,
                      .returnCode = ERROR_UNKNOWN,
                      .eulaVersion = 0};
    errorText(&conf, ("An exception was thrown but never handled:\n\n" + message).c_str());
    errorDisp(&conf);
    // Exit
    aptExit();
    socExit();
    gfxExit();
    exit(-errno);
}

#endif
