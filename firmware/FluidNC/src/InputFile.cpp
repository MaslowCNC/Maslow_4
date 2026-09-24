// Copyright (c) 2021 -	Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "InputFile.h"

#include "Report.h"
#include "GCode.h"

InputFile::InputFile(const Volume& defaultFs, const char* path) : FileStream(path, "r", defaultFs) {}
/*
  Read a line from the file
  Returns Error::Ok if a line was read, even if the line was empty.
  Returns Error::EOF on end of file.
  Returns other Error code on error, after displaying a message.
*/
Error InputFile::readLine(char* line, size_t maxlen) {
    size_t len = 0;
    int    c;
    while ((c = read()) >= 0) {
        if (len >= maxlen) {
            return Error::LineLengthExceeded;
        }
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            ++_line_number;
            if (len == 0) {
                ++_blank_lines;
            }
            break;
        }
        line[len++] = c;
    }
    line[len] = '\0';
    return len || c >= 0 ? Error::Ok : Error::Eof;
}

void InputFile::ack(Error status) {
    if (status != Error::Ok) {
        log_error(static_cast<int>(status) << " (" << errorString(status) << ") in " << name() << " at line " << lineNumber());
        if (status != Error::GcodeUnsupportedCommand) {
            // Do not stop on unsupported commands because most senders do not stop.
            // Stop the file job on other errors
            notifyf("File job error", "Error:%d in %s at line: %d", status, name().c_str(), lineNumber());
            _pending_error = status;
        }
    }
}

int32_t InputFile::_current_line_num = 0;

void InputFile::end_message() {
    _progress = "SD: ";
    _progress += name();
    _progress += ": Sent";
}

Error InputFile::pollLine(char* line) {
    // File input never returns realtime characters, so we do nothing
    // if line is null.
    if (!line) {
        return Error::NoData;
    }
    if (_pending_error != Error::Ok) {
        return _pending_error;
    }
    if (_percent) {
        _percent = false;
        // If the first non-blank line in the file is a % line, it denotes start-of-file.
        // Otherwise a % line causes the rest of the file to be skipped, per
        // https://linuxcnc.org/docs/html/gcode/overview.html#gcode:file-requirements
        // The line with % is not blank, so if it is the first non-blank line
        // _line_number will be one more than _blank_lines
        if (_line_number != (_blank_lines + 1)) {
            _ended = true;
        }
    }
    if (_ended) {
        end_message();
        return Error::Eof;
    }
    switch (auto err = readLine(line, Channel::maxLine)) {
        case Error::Ok: {
            _current_line_num = _line_number;

            float percent_complete = ((float)position()) * 100.0f / size();

            _progress = "SD:" + formatFloat(percent_complete, 2) + "," + path();
        }
            return Error::Ok;
        case Error::Eof:
            end_message();
            _current_line_num = 0;
            return Error::Eof;
        default:
            _progress         = "";
            _current_line_num = 0;
            return err;
    }
}

void InputFile::pauseJob() {
    float percent = size() ? ((float)position()) * 100.0f / size() : 0.0f;
    notifyf("File print paused", "Paused file job at line: %d (%.2f%% complete)", lineNumber(), percent);
    log_info("Paused file job at line: " << lineNumber() << " (" << percent << "% complete) - Last motion command: "
                                          << getMotionCommandString());
}

const char* InputFile::getMotionCommandString() {
    switch (gc_state.modal.motion) {
        case Motion::None:
            return "G80";
        case Motion::Seek:
            return "G0";
        case Motion::Linear:
            return "G1";
        case Motion::CwArc:
            return "G2";
        case Motion::CcwArc:
            return "G3";
        case Motion::ProbeToward:
            return "G38.2";
        case Motion::ProbeTowardNoError:
            return "G38.3";
        case Motion::ProbeAway:
            return "G38.4";
        case Motion::ProbeAwayNoError:
            return "G38.5";
        default:
            return "unknown";
    }
}

InputFile::~InputFile() {
    _current_line_num = 0;
}
