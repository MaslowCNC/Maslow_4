// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Protocol.h"
#include "Serial.h"
#include "SettingsDefinitions.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

// Shared buffer for log formatting with mutex protection
// Using 1400 bytes as suggested to handle the largest log messages
static char logBuffer[1400];
static SemaphoreHandle_t logBufferMutex = nullptr;

// Initialize mutex on first use
static void ensureLogBufferMutex() {
    if (logBufferMutex == nullptr) {
        logBufferMutex = xSemaphoreCreateMutex();
    }
}

// Get the shared log buffer (thread-safe)
char* getLogBuffer() {
    ensureLogBufferMutex();
    xSemaphoreTake(logBufferMutex, portMAX_DELAY);
    return logBuffer;
}

// Release the shared log buffer
void releaseLogBuffer() {
    xSemaphoreGive(logBufferMutex);
}

LogStream::LogStream(Print& channel, const char* name) : _channel(channel) {
    _line = new std::string();
    print(name);
}
LogStream::LogStream(const char* name) : LogStream(allChannels, name) {}

size_t LogStream::write(uint8_t c) {
    *_line += (char)c;
    return 1;
}

LogStream::~LogStream() {
    if ((*_line).length() && (*_line)[0] == '[') {
        *_line += ']';
    }
    send_line(_channel, _line);
}
