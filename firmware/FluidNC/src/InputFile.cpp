// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"
#include "GCode.h"

#include <cstdio>

InputFile::InputFile(const char* defaultFs, const char* path, WebUI::AuthenticationLevel auth_level, Channel& out) :
    FileStream(path, "r", defaultFs), _auth_level(auth_level), _out(out), _line_num(0), _pathCache(FileStream::path()) {}
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

char    InputFile::_progress[128]    = "";
int32_t InputFile::_current_line_num = 0;

Channel* InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!_readyNext || !line) {
        return nullptr;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            _current_line_num = _line_num;
            // Runs once per GCode line.  The ostringstream version this replaces
            // allocated a stringbuf, a path() string and two more std::strings on
            // every single line of the job.
            snprintf(_progress, sizeof(_progress), "SD:%.2f,%s", percent_complete(), _pathCache.c_str());
        }
            return &allChannels;
        case Error::Eof:
            _progress[0]      = '\0';
            _current_line_num = 0;
            _notifyf("File job done", "%s file job succeeded", path());
            log_msg(path() << " file job succeeded");
            allChannels.kill(this);
            return nullptr;
        default:
            _progress[0]      = '\0';
            _current_line_num = 0;
            log_error(static_cast<int>(err) << " (" << errorString(err) << ") in " << path() << " at line " << getLineNumber());
            allChannels.kill(this);
            return nullptr;
    }
}


void InputFile::stopJob() {
    //Report print stopped
    _notifyf("File print canceled", "Reset during file job at line: %d (%.2f%% complete)", getLineNumber(), percent_complete());
    log_info("Reset during file job at line: " << getLineNumber() << " (" << percent_complete() << "% complete)"
             << " - Last motion command: " << getMotionCommandString());
    allChannels.kill(this);
}

void InputFile::pauseJob() {
    //Report print paused
    _notifyf("File print paused", "Paused file job at line: %d (%.2f%% complete)", getLineNumber(), percent_complete());
    log_info("Paused file job at line: " << getLineNumber() << " (" << percent_complete() << "% complete)"
             << " - Last motion command: " << getMotionCommandString());
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
    _progress[0]      = '\0';
    _current_line_num = 0;
}
