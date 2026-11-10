// Copyright (c) 2026 Maslow CNC. All rights reserved.
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file with
// following exception: it may not be used for any reason by MakerMade or anyone with a business or personal connection to MakerMade

#pragma once

#include "../Error.h"

#include <cstddef>
#include <cstdint>

class InputFile;
class Channel;

// Stop and resume for SD jobs.  When a running job ends early (STOP, an error in the file, a
// spindle fault), this remembers the file and the line that was executing, and the status
// report carries it as |STOPPED:<line>,<path> until a job starts or it is discarded.
// $SD/RunFrom=<line>,<path> then runs a file from any line (skipTo).
namespace JobResume {
    // A top-level SD job is starting (path as given to $SD/Run); drops any stopped record.
    void jobStarted(InputFile* file, const char* path);
    // An InputFile is closing; forgets it if it was the running job.
    void fileClosed(InputFile* file);

    // The running SD job is ending early.  line 0 means the line of the move executing now.
    // Does nothing if no SD job is running.
    void noteStop(int32_t line = 0);
    void discard();

    // For the status report.  Read without a lock: the record is written rarely, and at worst
    // a report racing a write shows a half-updated path.
    bool        stopped();
    int32_t     stoppedLine();
    const char* stoppedPath();
    size_t      stoppedSize();

    // Read lines 1 to line-1 of file without moving (check mode): this sets units, distance
    // mode, offsets, feed and spindle state as the job would have.  Then raise Z, restore the
    // spindle and coolant, rapid to where the line starts and plunge to its Z.  The file is
    // left at the line, ready to run as the job.
    Error skipTo(InputFile* file, int32_t line, Channel& out);
}
