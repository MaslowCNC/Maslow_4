#include "StartupLog.h"
#include <sstream>
#include "Protocol.h"  // send_line()

// The log keeps capturing until the first WebSocket connects (see WSChannel.cpp).  Without a
// WebUI it used to grow with every broadcast line until the heap ran out (2026-10-19), so it
// stops at MAX_BYTES; the boot messages $SS shows are about 3.5 KB.
size_t StartupLog::write(uint8_t data) {
    if (_active) {
        if (_messages.size() < MAX_BYTES) {
            _messages += (char)data;
        } else {
            _active = false;  // Stays registered; writes are dropped from here on
        }
    }
    return 1;
}
std::string StartupLog::messages() {
    return _messages;
}
void StartupLog::dump(Channel& out) {
    std::istringstream iss(_messages);
    for (std::string line; std::getline(iss, line);) {
        log_to(out, line);
    }
}

void StartupLog::stop() {
    _active = false;
}

StartupLog::~StartupLog() {}

StartupLog startupLog("Startup Log");
