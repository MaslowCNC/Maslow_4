// Copyright (c) 2021 -  Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#pragma once

#include "Config.h"

#include "Channel.h"
#include <queue>

class StartupLog : public Channel {
private:
    std::string _messages;
    bool        _active;

    static const size_t MAX_BYTES = 4096;

public:
    StartupLog(const char* name) : Channel(name), _active(true) {}
    virtual ~StartupLog();

    size_t      write(uint8_t data) override;
    std::string messages();
    void        dump(Channel& channel);
    void        stop();  // Stop capturing new logs
    bool        isActive() const { return _active; }
};

extern StartupLog startupLog;
