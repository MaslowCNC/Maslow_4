// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"
#include "GCode.h"
#include "System.h"

InputFile::InputFile(const char* defaultFs, const char* path, WebUI::AuthenticationLevel auth_level, Channel& out) :
    FileStream(path, "r", defaultFs), _auth_level(auth_level), _out(out), _line_num(0) {}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, int maxlen) {
    ++_line_num;
    int len = 0;
    int c;
    while ((c = read()) >= 0) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}

// return a percentage complete 50.5 = 50.5%
float InputFile::percent_complete() {
    return (float)position() / (float)size() * 100.0f;
}

void InputFile::ack(Error status) {
    if (status != Error::Ok) {
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << path() << " at line " << getLineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not
            // Stop the file job on other errors
            _notifyf("File job error", "Error:%d in %s at line: %d", status, path(), getLineNumber());
            allChannels.kill(this);
            return;
        }
    }
    _readyNext = true;
}

std::string InputFile::_progress    = "";
std::string InputFile::_pauseStatus = "";

#include <sstream>
#include <iomanip>

Channel* InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!_readyNext || !line) {
        return nullptr;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            std::ostringstream s;
            s << "SD:" << std::fixed << std::setprecision(2) << percent_complete() << "," << path().c_str();
            _progress = s.str();
        }
            return &allChannels;
        case Error::Eof:
            _progress    = "";
            _pauseStatus = "";
            _notifyf("File job done", "%s file job succeeded", path());
            log_msg(path() << " file job succeeded");
            allChannels.kill(this);
            return nullptr;
        default:
            _progress = "";
            log_error(static_cast<int>(err) << " (" << errorString(err) << ") in " << path() << " at line " << getLineNumber());
            allChannels.kill(this);
            return nullptr;
    }
}


void InputFile::stopJob() {
    _pauseStatus = "";
    //Report print stopped
    _notifyf("File print canceled", "Reset during file job at line: %d (%.2f%% complete)", getLineNumber(), percent_complete());
    log_info("Reset during file job at line: " << getLineNumber() << " (" << percent_complete() << "% complete)"
             << " - Last motion command: " << getMotionCommandString());
    allChannels.kill(this);
}

void InputFile::pauseJob() {
    float* mpos = get_mpos();
    float  x    = mpos[X_AXIS];
    float  y    = mpos[Y_AXIS];
    float  z    = mpos[Z_AXIS];
    //Report print paused
    _notifyf("File print paused",
             "Paused file job at line: %d (%.2f%% complete) - Position: X=%.3f Y=%.3f Z=%.3f",
             getLineNumber(),
             percent_complete(),
             x,
             y,
             z);
    std::ostringstream s;
    s << "Paused file job at line: " << getLineNumber() << " (" << std::fixed << std::setprecision(2) << percent_complete() << "% complete)"
      << " - Last motion command: " << getMotionCommandString()
      << " - Position: X=" << std::setprecision(3) << x << " Y=" << y << " Z=" << z;
    _pauseStatus = s.str();
    log_info(_pauseStatus);
}

const char* InputFile::getMotionCommandString() {
    switch (gc_state.modal.motion) {
        case Motion::None:        return "G80";
        case Motion::Seek:        return "G0";
        case Motion::Linear:      return "G1";
        case Motion::CwArc:       return "G2";
        case Motion::CcwArc:      return "G3";
        case Motion::ProbeToward: return "G38.2";
        case Motion::ProbeTowardNoError: return "G38.3";
        case Motion::ProbeAway:   return "G38.4";
        case Motion::ProbeAwayNoError: return "G38.5";
        default: return "unknown";
    }
}

InputFile::~InputFile() {
    _progress    = "";
    _pauseStatus = "";
}
