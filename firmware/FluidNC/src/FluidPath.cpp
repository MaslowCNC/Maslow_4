// Copyright (c) 2022 Mitch Bradley
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "FluidPath.h"
#include "Driver/sdspi.h"
#include "Driver/localfs.h"
#include "Config.h"
#include "Error.h"
#include "HashFS.h"
#include "Maslow/Maslow.h"  // resetUpdateWatchdog()

int FluidPath::_refcnt = 0;

FluidPath::FluidPath(const char* name, const char* fs, std::error_code* ecptr) : std::filesystem::path(canonicalPath(name, fs)) {
    auto mount = *(++begin());  // Use the path iterator to get the first component
    _isSD      = mount == "sd";

    if (_isSD) {
        if (_refcnt == 0) {
            std::error_code ec = sd_mount();
            // Mounting the card runs the full SD init sequence and blocks for
            // ~100 ms.  Whichever task does it - the protocol task, when a job
            // is started with $sd/run - is not calling Maslow.update() while it
            // waits, which trips the 100 ms update watchdog into an emergency
            // stop.  Same treatment as the other known-blocking operations.
            Maslow.resetUpdateWatchdog();
            if (ec) {
                if (ecptr) {
                    *ecptr = ec;
                    return;
                }
                throw stdfs::filesystem_error { "SD card is inaccessible", ec };
            }
        }
        ++_refcnt;
    }
    // log_debug("construct " << _isSD << " " << _refcnt);
}

FluidPath::FluidPath(const FluidPath& o) : path(o), _isSD(o._isSD) {
    if (this != &o && _isSD) {
        ++_refcnt;
    }
    // log_debug("path construct " << _isSD << " " << _refcnt);
}

FluidPath::FluidPath(FluidPath&& o) : path(std::move(o)), _isSD(o._isSD) {
    if (this != &o) {
        // After a move, the other object is dead so we do not want
        // to decrement the refcount on destruction
        o._isSD = false;
    }
    // log_debug(" move construct " << _isSD << " " << _refcnt);
}

FluidPath& FluidPath::operator=(const FluidPath& o) {
    stdfs::path::operator=(o);

    _isSD = o._isSD;
    if (&o != this && _isSD) {
        ++_refcnt;
    }
    // log_debug(" copy assign " << _isSD << " " << _refcnt);
    return *this;
}

FluidPath& FluidPath::operator=(FluidPath&& o) {
    std::swap(_isSD, o._isSD);
    stdfs::path::operator=(std::move(o));
    // log_debug(" move assign " << _isSD << " " << _refcnt);
    return *this;
}

FluidPath::~FluidPath() {
    // log_debug("~ refcnt " << _isSD << " " << _refcnt);
    if (_isSD && (_refcnt && --_refcnt == 0)) {
        sd_unmount();
        Maslow.resetUpdateWatchdog();
    }
}

void FluidPath::rehash_fs() {
    if (!_isSD) {
        HashFS::rehash();
    }
}
