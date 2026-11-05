// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#include "JobResume.h"

#include "Maslow.h"
#include "../GCode.h"
#include "../InputFile.h"
#include "../Job.h"
#include "../Logging.h"
#include "../Machine/MachineConfig.h"  // config->_coolant
#include "../NutsBolts.h"              // MM_PER_INCH
#include "../Planner.h"
#include "../Protocol.h"
#include "../Settings.h"  // execute_line
#include "../Spindles/Spindle.h"
#include "../System.h"

#include <cstring>

namespace {
    InputFile*    s_running = nullptr;  // the top-level SD job, while it is open
    char          s_runningPath[Channel::maxLine];
    volatile bool s_stopped = false;
    int32_t       s_line    = 0;
    size_t        s_size    = 0;
    char          s_path[Channel::maxLine];

    // Plunge rate for the move down to where the resumed line starts.
    constexpr float RESUME_PLUNGE_MM_PER_MIN = 300.0f;
    // How often to service the protocol (Maslow.update and its 100 ms watchdog) while skipping.
    constexpr uint32_t SKIP_SERVICE_MS = 20;

    // Run one G-code line for the entry move, with machine-coordinate values in mm converted
    // to the job's units.
    Error entryLine(const char* fmt, float a, float b = 0, float c = 0) {
        float scale = gc_state.modal.units == Units::Inches ? 1.0f / MM_PER_INCH : 1.0f;
        char  line[96];
        snprintf(line, sizeof(line), fmt, a * scale, b * scale, c * scale);
        return gc_execute_line(line);
    }
}

namespace JobResume {
    void jobStarted(InputFile* file, const char* path) {
        s_running = file;
        strncpy(s_runningPath, path, sizeof(s_runningPath) - 1);
        s_runningPath[sizeof(s_runningPath) - 1] = '\0';
        if (s_stopped) {
            discard();
        }
    }

    void fileClosed(InputFile* file) {
        if (file == s_running) {
            s_running = nullptr;
        }
    }

    void noteStop(int32_t line) {
        if (!s_running || !Job::active()) {
            return;
        }
        if (line <= 0) {
            plan_block_t* block = plan_get_current_block();
            line                = (block && block->file_line_number > 0) ? block->file_line_number : InputFile::_current_line_num;
        }
        if (line <= 0) {
            return;
        }
        s_stopped = false;
        s_line    = line;
        s_size    = s_running->size();
        strcpy(s_path, s_runningPath);
        s_stopped = true;
        s_running = nullptr;  // one record per job, even if several stop paths see it end
        log_info("Job stopped at line " << line << " of " << s_path);
    }

    void discard() {
        s_stopped = false;
    }

    bool stopped() {
        return s_stopped;
    }
    int32_t stoppedLine() {
        return s_line;
    }
    const char* stoppedPath() {
        return s_path;
    }
    size_t stoppedSize() {
        return s_size;
    }

    Error skipTo(InputFile* file, int32_t line, Channel& out) {
        // Highest machine Z the skipped lines reach: the entry move clears at least that.
        float clearZ = get_mpos()[Z_AXIS];

        sys.set_state(State::CheckMode);
        char     buf[Channel::maxLine];
        uint32_t serviced = millis();
        Error    err      = Error::Ok;
        while (file->lineNumber() + 1 < size_t(line)) {
            err = file->pollLine(buf);
            if (err != Error::Ok) {
                break;
            }
            // $ and [ lines are commands, not G-code: skipping them is the safe choice
            if (buf[0] != '$' && buf[0] != '[') {
                Error lineErr = execute_line(buf, out, AuthenticationLevel::LEVEL_GUEST, true);
                if (lineErr != Error::Ok && lineErr != Error::GcodeUnsupportedCommand) {
                    log_error_to(out, "Resume: error " << int(lineErr) << " in " << file->path() << " at line " << file->lineNumber());
                    err = lineErr;
                    break;
                }
            }
            if (gc_state.position[Z_AXIS] > clearZ) {
                clearZ = gc_state.position[Z_AXIS];
            }
            if (millis() - serviced > SKIP_SERVICE_MS) {
                protocol_execute_realtime();
                serviced = millis();
                if (sys.abort()) {
                    return Error::Reset;
                }
            }
        }
        sys.set_state(State::Idle);
        if (err == Error::Eof) {
            log_error_to(out, "Resume: " << file->path() << " has only " << file->lineNumber() << " lines");
            return Error::InvalidValue;
        }
        if (err != Error::Ok) {
            return err;
        }

        // Where the line starts, and the state the skipped lines left, before the entry move
        // changes the modal motion and feed.
        float    target[MAX_N_AXIS];
        Motion   motion   = gc_state.modal.motion;
        float    feedRate = gc_state.feed_rate;
        SpindleState spindleState = gc_state.modal.spindle;
        float    spindleSpeed = gc_state.spindle_speed;
        memcpy(target, gc_state.position, sizeof(target));
        if (target[Z_AXIS] > clearZ) {
            clearZ = target[Z_AXIS];
        }
        gc_sync_position();  // the parser to where the machine really is

        log_info("Resuming " << file->path() << " at line " << line);

        Error moveErr = entryLine("G53 G0 Z%.4f", clearZ);
        if (moveErr == Error::Ok && spindleState != SpindleState::Disable) {
            protocol_buffer_synchronize();  // at the clearance height before the spindle starts
            spindle->setState(spindleState, uint32_t(spindleSpeed));  // waits out spinup_ms
        }
        if (moveErr == Error::Ok) {
            config->_coolant->set_state(gc_state.modal.coolant);
            moveErr = entryLine("G53 G0 X%.4f Y%.4f", target[X_AXIS], target[Y_AXIS]);
        }
        if (moveErr == Error::Ok) {
            moveErr = entryLine("G53 G1 Z%.4f F%.4f", target[Z_AXIS], RESUME_PLUNGE_MM_PER_MIN);
        }
        // The file's own modal motion and feed again, for lines that rely on them
        gc_state.modal.motion = motion;
        gc_state.feed_rate    = feedRate;
        if (moveErr != Error::Ok) {
            log_error_to(out, "Resume: the move to line " << line << " failed: " << errorString(moveErr));
        }
        return moveErr;
    }
}
