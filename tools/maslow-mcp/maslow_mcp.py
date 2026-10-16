#!/usr/bin/env python3
"""MCP server for debugging a Maslow (FluidNC) machine from Claude Code.

Runs on the computer, not the machine.  It talks to the XY board over interfaces the firmware already
has (telnet for commands and logs, HTTP for files and firmware updates) and to the spindle board
through the shared serial hub (spindle/tools/serial_hub.py on localhost:4567).

It is designed to run while a person drives the machine from the WebUI.  FluidNC gives every
connection its own channel, but it interleaves lines from all channels into one G-code stream, so:

  * Read-only commands are always allowed.  The ones that print a lot ($CD, $S, [ESP400], ...)
    are only sent while the machine is Idle/Alarm/Sleep so they never compete with a running job.
  * Anything that can move the machine or change its configuration is refused unless the server
    was started with --allow-motion, and even then only while the machine is Idle or Alarm.
  * feed_hold / soft_reset are always available, because stopping is always safe, but they act
    on the whole machine (they stop the user's job too).

Everything the XY board prints on our telnet channel is kept in memory and written with timestamps
to tools/maslow-mcp/logs/xy-*.log (commands we send are logged as ">> CMD").

Configuration (environment variables):
    MASLOW_HOST         XY board host name or IP          (default maslow.local)
    MASLOW_TELNET_PORT  FluidNC telnet port               (default 23)
    MASLOW_HTTP_PORT    FluidNC WebUI HTTP port           (default 80)
    SPINDLE_HUB         host:port of the spindle hub      (default 127.0.0.1:4567)
"""

import argparse
import collections
import glob
import os
import re
import shutil
import socket
import subprocess
import threading
import time

import anyio
import requests
from mcp.server.mcpserver import MCPServer

import trip_analysis

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
LOGDIR = os.path.join(HERE, "logs")
FIRMWARE_DIR = os.path.join(REPO, "firmware")
SPINDLE_DIR = os.path.join(REPO, "spindle")
SPINDLE_LOGDIR = os.path.join(SPINDLE_DIR, "logs")
SPINDLE_OTA_ENV = "esp32-s3-devkitc-1-ota"
SPINDLE_FLASH_SH = os.environ.get("SPINDLE_FLASH_SH", os.path.join(SPINDLE_DIR, "tools", "flash.sh"))  # USB upload

HOST = os.environ.get("MASLOW_HOST", "maslow.local")
TELNET_PORT = int(os.environ.get("MASLOW_TELNET_PORT", "23"))
HTTP_PORT = int(os.environ.get("MASLOW_HTTP_PORT", "80"))
HUB_HOST, HUB_PORT = os.environ.get("SPINDLE_HUB", "127.0.0.1:4567").rsplit(":", 1)
HUB_PORT = int(HUB_PORT)

ALLOW_MOTION = False  # set from --allow-motion in main()

SPINDLE_TRIP_FILE = "/spindle_trip.csv"  # where the XY board saves the spindle's trip recording
# Status polling for status_history: fast while something is running, slow otherwise.
POLL_BUSY_S, POLL_IDLE_S = 0.5, 2.0
BUSY_STATES = {"Run", "Hold", "Jog", "Home", "Door"}

# ---------------------------------------------------------------------------------------------
# Command classification
# ---------------------------------------------------------------------------------------------

# Read-only and short: safe in any state, including mid-job.
LIGHT_READS = {
    "$G", "$GCODE/MODES",
    "$I", "$BUILD/INFO",
    "$A", "$ALARMS/LIST",
    "$E", "$ERRORS/LIST",
    "$HEAP", "$HEAP/SHOW",
    "$T", "$STATE",
    "$CI", "$CHANNEL/INFO",
    "$GSTATE", "$MASLOW/GSTATE",
    "$MINFO", "$MASLOW/GETINFO",
    "[ESP800]", "[ESP420]", "[ESP111]",
}

# Read-only but long output: only while the machine is not running anything.
HEAVY_READS = {
    "$", "$HELP",
    "$CD", "$CONFIG/DUMP",
    "$S", "$SETTINGS/LIST",
    "$SC", "$SETTINGS/LISTCHANGED",
    "$$", "$GRBLSETTINGS/LIST",
    "$L", "$GRBLNAMES/LIST",
    "$CMD", "$COMMANDS/LIST",
    "$SS", "$STARTUP/SHOW",
    "$#", "$GCODE/OFFSETS",
    "$N", "$GCODE/STARTUPLINES",
    "$GD", "$GPIO/DUMP",
    "$LIMITS", "$LIMITS/SHOW",
    "$V", "$SETTINGS/STATS",
    "[ESP400]",
}

READ_STATES = {"Idle", "Alarm", "Sleep"}  # states where heavy reads are allowed
WRITE_STATES = {"Idle", "Alarm"}  # states where motion / config changes are allowed

# Spindle-board commands that only print information (or stop the spindle).
SPINDLE_READS = {"?", "H", "WHY", "DUMP", "LOAD", "TRIP", "E"}


def classify(cmd):
    """Return 'light', 'heavy' or 'write' for an XY-board command line."""
    key = re.sub(r"\s+", "", cmd).upper()
    if key in LIGHT_READS:
        return "light"
    if key in HEAVY_READS:
        return "heavy"
    return "write"


# ---------------------------------------------------------------------------------------------
# XY board telnet connection
# ---------------------------------------------------------------------------------------------


def stamp(t=None):
    t = time.time() if t is None else t
    return time.strftime("%H:%M:%S", time.localtime(t)) + ".%03d" % int((t % 1) * 1000)


class Board:
    """Keeps a telnet connection to the XY board open, logging every line it prints."""

    def __init__(self):
        self.sock = None
        self.ip = None
        self.lines = collections.deque(maxlen=50000)  # (seq, time, text)
        self.seq = 0
        self.cond = threading.Condition()
        self.cmd_lock = threading.Lock()  # one command/response exchange at a time
        self.last_error = "not connected yet"
        self.connected_since = None
        os.makedirs(LOGDIR, exist_ok=True)
        self.log_path = os.path.join(LOGDIR, time.strftime("xy-%y%m%d-%H%M%S.log"))
        self.log_file = open(self.log_path, "a", buffering=1)
        # '?' replies from the status poller, kept apart from the log: (time, raw report).
        self.status_hist = collections.deque(maxlen=20000)
        self.status_log = open(self.log_path.replace("xy-", "status-"), "a", buffering=1)
        threading.Thread(target=self._run, daemon=True).start()
        threading.Thread(target=self._poll, daemon=True).start()

    # -- connection -------------------------------------------------------------------------

    def _resolve(self):
        return socket.getaddrinfo(HOST, TELNET_PORT, socket.AF_INET, socket.SOCK_STREAM)[0][4][0]

    def _run(self):
        while True:
            try:
                ip = self._resolve()
                s = socket.create_connection((ip, TELNET_PORT), timeout=5)
                s.settimeout(None)
                s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                with self.cond:
                    self.sock, self.ip = s, ip
                    self.connected_since = time.time()
                    self.last_error = None
                self._add("--- connected to %s:%d ---" % (ip, TELNET_PORT))
                self._read(s)
                self._add("--- connection closed by board ---")
            except OSError as e:
                with self.cond:
                    if self.last_error != str(e):
                        self._add("--- connect failed: %s ---" % e)
                    self.last_error = str(e)
            with self.cond:
                if self.sock:
                    try:
                        self.sock.close()
                    except OSError:
                        pass
                self.sock = None
                self.connected_since = None
            time.sleep(3)

    def _read(self, s):
        buf = b""
        while True:
            data = s.recv(4096)
            if not data:
                return
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").strip("\r")
                if text:
                    self._add(text)

    def _add(self, text):
        t = time.time()
        with self.cond:
            self.seq += 1
            self.lines.append((self.seq, t, text))
            self.cond.notify_all()
        if is_status(text):
            # Status reports go to their own log, so the poller does not bury the messages.
            self.status_hist.append((t, text))
            self.status_log.write("%s %s\n" % (stamp(t), text))
        else:
            self.log_file.write("%s %s\n" % (stamp(t), text))

    def _poll(self):
        """Ask for '?' periodically so status_history can show what the machine was doing at any
        moment, e.g. the feed and position when the spindle tripped."""
        while True:
            delay = POLL_IDLE_S
            if self.connected():
                try:
                    rep = self.status(timeout=2.0)
                    if rep and parse_status(rep)["state"].split(":")[0] in BUSY_STATES:
                        delay = POLL_BUSY_S
                except (OSError, ConnectionError):
                    pass
            time.sleep(delay)

    def _send_raw(self, data):
        with self.cond:
            s = self.sock
        if s is None:
            raise ConnectionError("not connected to the XY board at %s (%s)" % (HOST, self.last_error))
        s.sendall(data)

    def _wait_for(self, start_seq, done, timeout):
        """Collect lines after start_seq until done(text) is true or timeout. Returns (lines, ok)."""
        deadline = time.time() + timeout
        out = []
        seen = start_seq
        with self.cond:
            while True:
                fresh = []
                for seq, t, text in reversed(self.lines):
                    if seq <= seen:
                        break
                    fresh.append((seq, text))
                for seq, text in reversed(fresh):
                    seen = seq
                    out.append(text)
                    if done(text):
                        return out, True
                remaining = deadline - time.time()
                if remaining <= 0:
                    return out, False
                self.cond.wait(remaining)

    # -- public API ---------------------------------------------------------------------------

    def connected(self):
        with self.cond:
            return self.sock is not None

    def status(self, timeout=2.0):
        """Send '?' and return the raw <...> status report, or None."""
        with self.cmd_lock:
            with self.cond:
                start = self.seq
            self._send_raw(b"?")
            lines, ok = self._wait_for(start, lambda s: s.startswith("<") and s.endswith(">"), timeout)
        return lines[-1] if ok else None

    def state(self):
        rep = self.status()
        return parse_status(rep)["state"].split(":")[0] if rep else None

    def command(self, cmd, timeout):
        """Send one line and collect output until ok / error:N."""
        with self.cmd_lock:
            with self.cond:
                start = self.seq
            self.log_file.write("%s >> %s\n" % (stamp(), cmd))
            self._send_raw((cmd + "\n").encode())
            lines, ok = self._wait_for(start, lambda s: s == "ok" or s.startswith("error"), timeout)
        if not ok:
            lines.append("(no ok/error within %.0f s; the command may still be running)" % timeout)
        return lines

    def realtime(self, byte, label):
        self.log_file.write("%s >> %s\n" % (stamp(), label))
        self._send_raw(byte)

    def recent(self, n, pattern=None, since_seconds=None):
        cutoff = time.time() - since_seconds if since_seconds else 0
        rx = re.compile(pattern, re.I) if pattern else None
        with self.cond:
            sel = [(t, s) for _, t, s in self.lines
                   if t >= cutoff and not is_status(s) and (rx is None or rx.search(s))]
        return ["%s %s" % (stamp(t), s) for t, s in sel[-n:]]

    def lines_between(self, t0, t1, pattern=None):
        rx = re.compile(pattern, re.I) if pattern else None
        with self.cond:
            return [(t, s) for _, t, s in self.lines
                    if t0 <= t <= t1 and not is_status(s) and (rx is None or rx.search(s))]

    def statuses_between(self, t0, t1):
        with self.cond:
            return [(t, s) for t, s in self.status_hist if t0 <= t <= t1]


def is_status(text):
    return text.startswith("<") and text.endswith(">")


def parse_status(rep):
    """'<Idle|MPos:1,2,3|FS:0,0|Pn:P>' -> {'state': 'Idle', 'MPos': '1,2,3', ...}"""
    parts = rep.strip("<>").split("|")
    out = {"state": parts[0]}
    for p in parts[1:]:
        k, _, v = p.partition(":")
        out[k] = v
    return out


board = None  # created in main()


def base_url():
    return "http://%s:%d" % (board.ip or HOST, HTTP_PORT)


# ---------------------------------------------------------------------------------------------
# Gating helpers
# ---------------------------------------------------------------------------------------------


class Refused(Exception):
    pass


def require_state(allowed, what):
    st = board.state()
    if st is None:
        raise Refused("Refused %s: could not read the machine state (no reply to '?')." % what)
    if st not in allowed:
        raise Refused("Refused %s: machine is busy (state %s; needs %s)." % (what, st, "/".join(sorted(allowed))))
    return st


def require_motion(what):
    if not ALLOW_MOTION:
        raise Refused(
            "Refused %s: it can move the machine or change its configuration, and this server was "
            "started without --allow-motion. Ask the user to enable it (see tools/maslow-mcp/README.md)." % what
        )


async def run(fn, *args):
    try:
        return await anyio.to_thread.run_sync(fn, *args)
    except Refused as e:
        return str(e)
    except (OSError, ConnectionError, requests.RequestException) as e:
        return "Error: %s" % e


# ---------------------------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------------------------

mcp = MCPServer(
    "maslow",
    instructions=(
        "Debug a Maslow CNC (FluidNC XY board + spindle board). The user may be driving the machine "
        "from the WebUI at the same time: read state and logs freely, but never send motion, setting "
        "changes, feed_hold or soft_reset unless the user asked for that action."
    ),
)


@mcp.tool()
async def machine_status() -> dict:
    """Connection info and the XY board's current status report ('?'): state, position, feed/speed, pins."""

    def go():
        info = {
            "host": HOST,
            "ip": board.ip,
            "telnet_connected": board.connected(),
            "connected_since": stamp(board.connected_since) if board.connected_since else None,
            "last_error": board.last_error,
            "motion_allowed": ALLOW_MOTION,
            "log_file": board.log_path,
        }
        if board.connected():
            rep = board.status()
            info["status_raw"] = rep
            if rep:
                info["status"] = parse_status(rep)
        return info

    return await run(go)


@mcp.tool()
async def send_command(command: str, timeout: float = 10) -> str:
    """Send one line (G-code, $ command or [ESPxxx]) to the XY board and return its reply up to ok/error.

    Read-only commands always work (long ones like $CD/$S/[ESP400] wait for the machine to be idle).
    Anything else - motion, $X, $H, $Maslow/* actions, setting writes - needs --allow-motion and an
    Idle/Alarm machine. Broadcast [MSG:...] lines that arrive meanwhile are included in the reply.
    Use machine_status for '?', and feed_hold / cycle_start / soft_reset for realtime characters.
    """

    def go():
        cmd = command.strip()
        if not cmd:
            return "Empty command."
        if cmd in ("?", "!", "~") or "\x18" in cmd:
            return "Use machine_status / feed_hold / cycle_start / soft_reset for realtime characters."
        kind = classify(cmd)
        if kind == "heavy":
            require_state(READ_STATES, "'%s'" % cmd)
        elif kind == "write":
            require_motion("'%s'" % cmd)
            require_state(WRITE_STATES, "'%s'" % cmd)
        return "\n".join(board.command(cmd, timeout))

    return await run(go)


@mcp.tool()
async def status_history(seconds: float = 60, around: str = "", window: float = 10) -> str:
    """The XY board's status reports ('?', polled every 0.5 s while running, 2 s otherwise).

    Shows state, machine position, feed/spindle speed (FS:) and pins over time. By default the last
    `seconds`; with `around` = a time from read_log ("HH:MM:SS[.mmm]", today) it shows `window` s
    either side of that moment instead. The full history is in logs/status-*.log.
    """
    if around:
        t = _today_at(around)
        if t is None:
            return "Could not read the time '%s' (want HH:MM:SS or HH:MM:SS.mmm)." % around
        sel = board.statuses_between(t - window, t + window)
    else:
        sel = board.statuses_between(time.time() - seconds, time.time())
    return "\n".join("%s %s" % (stamp(t), s) for t, s in sel) or "(no status reports in that window)"


def _today_at(hms):
    m = re.fullmatch(r"(\d{1,2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?", hms.strip())
    if not m:
        return None
    lt = time.localtime()
    base = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, int(m.group(1)), int(m.group(2)), int(m.group(3)), 0, 0, -1))
    return base + (int(m.group(4).ljust(3, "0")) / 1000 if m.group(4) else 0)


@mcp.tool()
async def read_log(lines: int = 200, grep: str = "", since_seconds: float = 0) -> str:
    """Recent lines the XY board printed on this server's telnet channel, timestamped.

    Includes broadcast messages ([MSG:...], Maslow log output) caused by anyone, e.g. the user's
    WebUI actions. grep is a case-insensitive regex; since_seconds limits to the last N seconds.
    Older history is in the log file named by machine_status.
    """
    out = board.recent(lines, grep or None, since_seconds or None)
    return "\n".join(out) if out else "(no matching lines)"


def _filtered(cmd, pattern):
    require_state(READ_STATES, "'%s'" % cmd)
    out = board.command(cmd, 20)
    if pattern:
        rx = re.compile(pattern, re.I)
        out = [s for s in out if rx.search(s)]
    return "\n".join(out)


@mcp.tool()
async def get_settings(grep: str = "") -> str:
    """All FluidNC/WebUI settings ($S), optionally filtered by a case-insensitive regex. Machine must be idle."""
    return await run(_filtered, "$S", grep)


@mcp.tool()
async def get_config(grep: str = "") -> str:
    """The running machine config as YAML ($CD), optionally filtered by regex. Machine must be idle."""
    return await run(_filtered, "$CD", grep)


@mcp.tool()
async def firmware_info() -> str:
    """Firmware version string ([ESP800]); the version includes the branch and commit."""
    return await run(lambda: "\n".join(board.command("[ESP800]", 5)))


@mcp.tool()
async def list_files(path: str = "/") -> dict:
    """List files on the XY board's local flash filesystem (config.yaml, index.html.gz, ...)."""

    def go():
        r = requests.get(base_url() + "/files", params={"action": "list", "path": path}, timeout=10)
        r.raise_for_status()
        return r.json()

    return await run(go)


@mcp.tool()
async def download_file(path: str) -> str:
    """Download a file from the XY board's flash filesystem into tools/maslow-mcp/logs/downloads.

    Returns the local path, and the content too when it is small text (e.g. config.yaml).
    """

    def go():
        r = requests.get(base_url() + "/" + path.lstrip("/"), timeout=30)
        r.raise_for_status()
        dest_dir = os.path.join(LOGDIR, "downloads")
        os.makedirs(dest_dir, exist_ok=True)
        dest = os.path.join(dest_dir, os.path.basename(path.rstrip("/")) or "file")
        with open(dest, "wb") as f:
            f.write(r.content)
        msg = "Saved %d bytes to %s" % (len(r.content), dest)
        if len(r.content) < 16000:
            try:
                msg += "\n\n" + r.content.decode("utf-8")
            except UnicodeDecodeError:
                pass
        return msg

    return await run(go)


def _upload(endpoint, local_path, remote_dir, remote_name):
    size = os.path.getsize(local_path)
    remote = remote_dir.rstrip("/") + "/" + remote_name
    with open(local_path, "rb") as f:
        r = requests.post(
            base_url() + endpoint,
            # FluidNC needs the "<remote path>S" size field; /updatefw divides by it.
            data={"path": remote_dir, remote + "S": str(size)},
            files={"myfile[]": (remote, f, "application/octet-stream")},
            timeout=180,
        )
    r.raise_for_status()
    return size, r.text


@mcp.tool()
async def upload_file(local_path: str, remote_dir: str = "/", remote_name: str = "") -> str:
    """Upload a local file (e.g. config.yaml, index.html.gz) to the XY board's flash filesystem.

    Needs --allow-motion (it can change the configuration) and an Idle/Alarm machine.
    """

    def go():
        require_motion("upload_file")
        require_state(WRITE_STATES, "upload_file")
        size, reply = _upload("/files", local_path, remote_dir, remote_name or os.path.basename(local_path))
        return "Uploaded %d bytes. Board replied: %s" % (size, reply[:500])

    return await run(go)


def _pio():
    for p in (os.environ.get("MASLOW_PIO"), os.path.expanduser("~/.platformio/penv/bin/pio"), shutil.which("pio")):
        if p and os.path.exists(p):
            return p
    raise Refused("PlatformIO (pio) not found.")


@mcp.tool()
async def flash_firmware(env: str = "wifi_s3", build: bool = True) -> str:
    """Build the XY firmware (firmware/, PlatformIO env) and flash it over WiFi via /updatefw.

    Needs --allow-motion and an Idle/Alarm machine. The board reboots about a second after the
    upload; this waits for telnet to come back and reports the new [ESP800] version.
    """

    def go():
        require_motion("flash_firmware")
        require_state(WRITE_STATES, "flash_firmware")
        log = ""
        if build:
            p = subprocess.run([_pio(), "run", "-e", env], cwd=FIRMWARE_DIR, capture_output=True, text=True, timeout=900)
            log = (p.stdout + p.stderr)[-3000:]
            if p.returncode != 0:
                return "Build failed:\n" + log
        binfile = os.path.join(FIRMWARE_DIR, ".pio", "build", env, "firmware.bin")
        if not os.path.exists(binfile):
            return "No firmware at %s; build first." % binfile
        size, reply = _upload("/updatefw", binfile, "/", "firmware.bin")
        result = "Uploaded %d bytes; board replied %s (status 3 = success)." % (size, reply.strip())
        # Wait for the reboot: the old connection drops, then a new one comes up.
        time.sleep(3)
        deadline = time.time() + 60
        while time.time() < deadline and not board.connected():
            time.sleep(1)
        if board.connected():
            time.sleep(1)
            result += "\nBack online: " + " ".join(board.command("[ESP800]", 5))
        else:
            result += "\nBoard has not reconnected after 60 s."
        return result

    return await run(go)


# Lines the XY board logs while relaying the spindle's OTA lifecycle (SpindleBoard.cpp).
OTA_IP_RX = re.compile(r"spindle board is on WiFi at (\d+\.\d+\.\d+\.\d+)")
# Logged when the spindle link comes up, or when data resumes after a gap (e.g. a reboot).
LINK_BACK_RX = re.compile(r"link to spindle board confirmed|no data from spindle board for")
OTA_ERR_RX = re.compile(r"spindle board could not start OTA|cannot enable spindle OTA|OTA_ERR")


def _tail(p, n=2500):
    return (p.stdout + p.stderr)[-n:]


def _wait_spindle_ip(start_seq, timeout):
    """Wait for the XY board to relay the spindle's OTA address. Returns (ip, error_line)."""
    lines, _ = board._wait_for(start_seq, lambda s: OTA_IP_RX.search(s) or OTA_ERR_RX.search(s), timeout)
    for s in reversed(lines):
        m = OTA_IP_RX.search(s)
        if m:
            return m.group(1), None
        if OTA_ERR_RX.search(s):
            return None, s
    return None, None


@mcp.tool()
async def flash_spindle(build: bool = True, usb_fallback: bool = True, wait_for_ip: float = 90) -> str:
    """Build the spindle firmware and flash it over WiFi (ArduinoOTA), falling back to USB.

    Steps: build the OTA env; send [ESP810] ($Spindle/EnableOTA) so the XY board hands its WiFi
    credentials to the spindle; wait for the XY log to report the spindle's IP; push with espota.
    A failed transfer makes the spindle reboot and offer OTA again, so the upload is retried once
    on the new address. If WiFi does not work and usb_fallback is set, runs spindle/tools/flash.sh
    (USB, pauses the serial hub). Needs --allow-motion and an Idle/Alarm machine; the spindle's
    motors are disabled while it is in OTA mode.
    """

    def go():
        require_motion("flash_spindle")
        require_state(WRITE_STATES, "flash_spindle")
        pio = _pio()
        report = []
        if build:
            p = subprocess.run([pio, "run", "-e", SPINDLE_OTA_ENV], cwd=SPINDLE_DIR, capture_output=True, text=True, timeout=900)
            if p.returncode != 0:
                return "Spindle build failed:\n" + _tail(p)
            report.append("Built %s." % SPINDLE_OTA_ENV)

        with board.cond:
            start = board.seq
        reply = board.command("[ESP810]", 5)
        report.append("[ESP810] -> " + " | ".join(reply))
        ip, err = _wait_spindle_ip(start, wait_for_ip)
        ota_ok = False
        if ip is None:
            report.append(err or "The spindle did not report a WiFi address within %.0f s." % wait_for_ip)
        for attempt in (1, 2):
            if ip is None:
                break
            report.append("Attempt %d: espota upload to %s." % (attempt, ip))
            with board.cond:
                start = board.seq
            p = subprocess.run(
                [pio, "run", "-e", SPINDLE_OTA_ENV, "-t", "upload", "--upload-port", ip],
                cwd=SPINDLE_DIR, capture_output=True, text=True, timeout=600,
            )
            if p.returncode == 0:
                ota_ok = True
                report.append("WiFi upload succeeded.")
                break
            report.append("WiFi upload failed:\n" + _tail(p, 1500))
            if attempt == 1:
                # The spindle reboots after a failed transfer and reports its address again.
                ip, err = _wait_spindle_ip(start, 60)
                if ip is None:
                    report.append(err or "The spindle did not come back into OTA mode within 60 s.")

        if not ota_ok and usb_fallback:
            report.append("Falling back to USB (%s)." % SPINDLE_FLASH_SH)
            p = subprocess.run(["sh", SPINDLE_FLASH_SH], capture_output=True, text=True, timeout=900)
            if p.returncode == 0:
                report.append("USB upload succeeded. Note: the OTA request is still stored on the spindle, "
                              "so it may sit in OTA mode (motors off) for up to 5 minutes before resuming.")
            else:
                report.append("USB upload failed:\n" + _tail(p, 1500))
        elif not ota_ok:
            report.append("Not flashed (USB fallback disabled).")

        if ota_ok:
            # The new firmware boots and the XY board logs the link coming back.
            with board.cond:
                start = board.seq
            lines, ok = board._wait_for(start, lambda s: bool(LINK_BACK_RX.search(s)), 45)
            report.append("Spindle link back: " + lines[-1] if ok else "No spindle link confirmation in the XY log within 45 s.")
        return "\n".join(report)

    return await run(go)


@mcp.tool()
async def feed_hold() -> str:
    """Send '!' (feed hold) - pauses ALL motion on the machine, including the user's job.

    Only use when the user asks, or to stop something this server started.
    """

    def go():
        board.realtime(b"!", "! (feed hold)")
        return "Feed hold sent."

    return await run(go)


@mcp.tool()
async def cycle_start() -> str:
    """Send '~' (cycle start / resume after hold). Needs --allow-motion."""

    def go():
        require_motion("cycle_start")
        board.realtime(b"~", "~ (cycle start)")
        return "Cycle start sent."

    return await run(go)


@mcp.tool()
async def soft_reset() -> str:
    """Send Ctrl-X (soft reset) - aborts everything the machine is doing, including the user's job.

    Only use when the user asks.
    """

    def go():
        board.realtime(b"\x18", "^X (soft reset)")
        return "Soft reset sent."

    return await run(go)


# ---------------------------------------------------------------------------------------------
# Spindle board (through the serial hub)
# ---------------------------------------------------------------------------------------------


@mcp.tool()
async def spindle_command(command: str, wait: float = 2) -> str:
    """Send a line to the spindle board through the serial hub and return what it prints for `wait` s.

    Read-only commands (?, H, WHY, DUMP, LOAD, TRIP) and E (emergency stop) always work; anything
    else (S speed, Z move, CAL, VSWEEP, ...) needs --allow-motion. Needs the hub running
    (spindle/tools/serial_hub.py).
    """

    def go():
        cmd = command.strip()
        if cmd.upper() not in SPINDLE_READS:
            require_motion("spindle command '%s'" % cmd)
        try:
            s = socket.create_connection((HUB_HOST, HUB_PORT), timeout=3)
        except OSError as e:
            return "Spindle hub not reachable at %s:%d (%s). Start spindle/tools/serial_hub.py." % (HUB_HOST, HUB_PORT, e)
        with s:
            s.sendall((cmd + "\n").encode())
            out = b""
            deadline = time.time() + wait
            while (remaining := deadline - time.time()) > 0:
                s.settimeout(remaining)
                try:
                    data = s.recv(4096)
                except socket.timeout:
                    break
                if not data:
                    break
                out += data
        return out.decode("utf-8", "replace") or "(no output within %.1f s)" % wait

    return await run(go)


@mcp.tool()
async def spindle_log(lines: int = 200, grep: str = "") -> str:
    """Tail of the newest spindle serial-hub log (spindle/logs/hub-*.log), optionally filtered by regex."""
    files = sorted(glob.glob(os.path.join(SPINDLE_LOGDIR, "hub-*.log")), key=os.path.getmtime)
    if not files:
        return "No hub logs in %s." % SPINDLE_LOGDIR
    with open(files[-1], errors="replace") as f:
        sel = f.read().splitlines()
    if grep:
        rx = re.compile(grep, re.I)
        sel = [s for s in sel if rx.search(s)]
    return "%s\n%s" % (files[-1], "\n".join(sel[-lines:]))


def _trip_context(t_trip):
    """XY-side messages and status reports around the moment the spindle tripped."""
    out = ["", "XY board around the trip (%s):" % stamp(t_trip)]
    msgs = board.lines_between(t_trip - 10, t_trip + 3, r"spindle|alarm|error|warn|load|over-?current|stall")
    out += ["  %s %s" % (stamp(t), s) for t, s in msgs[-25:]] or ["  (no messages - was this server running then?)"]
    sts = board.statuses_between(t_trip - 10, t_trip + 3)
    if sts:
        out.append("Status reports (state | position | feed,spindle):")
        out += ["  %s %s" % (stamp(t), s) for t, s in sts[-24:]]
    return out


def _trip_time(reason, uptime_s):
    """When the spindle tripped, as the XY board heard it, for the recording with this reason and uptime.

    The recording can reach the XY board long after the trip: one that arrives while the machine is
    still stopping is fetched again once it has stopped, and action="fetch" re-sends an old one. So
    use the spindle's own fault message, which quotes the same reason, then the XY board's alarm,
    and only then the first time this recording (same uptime) arrived.
    """
    received = [t for t, s in board.lines_between(0, time.time(), r"receiving spindle trip recording")
                if "(at %.3fs uptime)" % uptime_s in s]
    until = received[0] if received else time.time()
    # Lines about receiving or re-sending the recording quote the reason too; skip them.
    events = [t for t, s in board.lines_between(0, until, re.escape(reason)) if "trip recording" not in s]
    if events:
        return events[-1]
    if not reason.startswith("manual test"):
        alarms = board.lines_between(0, until, r"SpindleBoard reported fault")
        if alarms:
            return alarms[-1][0]
    return received[0] if received else None


@mcp.tool()
async def spindle_trip(action: str = "analyse", source: str = "xy", wait: float = 45) -> str:
    """Diagnose a spindle over-current / driver-fault stop from the spindle's trip recording.

    The spindle records ~2 s of motor state (per-phase currents, filtered protection current,
    voltage limit, load-sense Iq and lag, FOC loop rate, nFAULT retries, Z phase) and, after a trip,
    sends it to the XY board, which saves it as /spindle_trip.csv (~20 s after the trip). This tool
    downloads that file, prints a diagnosis, and adds what the XY board was doing at the time
    (messages and status reports).

    action: "analyse" (default) reads the saved recording; "fetch" first asks the spindle to send
    its last recording again ($Spindle/Trip); "test" records a test trip now ($Spindle/Trip=TEST,
    no motion) to check the path end to end. fetch/test need an Idle/Alarm machine.
    source: "xy" (the XY board's file, over WiFi) or "hub" (the newest spindle serial-hub log, USB).
    The raw CSV is saved under logs/downloads/.
    """

    def go():
        notes = []
        if action not in ("analyse", "analyze", "fetch", "test"):
            return "action must be analyse, fetch or test."
        if action in ("fetch", "test"):
            if source != "xy":
                return "fetch/test go through the XY board; use source='xy'."
            require_state(WRITE_STATES, "the trip request")
            with board.cond:
                start = board.seq
            board.command("$Spindle/Trip" + ("=TEST" if action == "test" else ""), 5)
            lines, ok = board._wait_for(
                start, lambda t: re.search(r"trip recording (saved|incomplete|not saved)|cannot create|"
                                           r"no trip recording|already being sent|trip test refused", t), wait)
            notes += [l for l in lines if "SpindleBoard" in l]
            if not ok:
                notes.append("(the XY board did not report the recording saved within %.0f s)" % wait)

        if source == "hub":
            files = sorted(glob.glob(os.path.join(SPINDLE_LOGDIR, "hub-*.log")), key=os.path.getmtime)
            if not files:
                return "No hub logs in %s." % SPINDLE_LOGDIR
            with open(files[-1], errors="replace") as f:
                text = f.read()
            where = files[-1]
        else:
            r = requests.get(base_url() + SPINDLE_TRIP_FILE, timeout=30)
            if r.status_code == 404:
                return "\n".join(notes + ["No %s on the XY board yet - no trip recorded since this firmware "
                                           "was flashed. Use action='fetch' or 'test'." % SPINDLE_TRIP_FILE])
            r.raise_for_status()
            text = r.content.decode("utf-8", "replace")
            dest_dir = os.path.join(LOGDIR, "downloads")
            os.makedirs(dest_dir, exist_ok=True)
            where = os.path.join(dest_dir, time.strftime("spindle_trip-%y%m%d-%H%M%S.csv"))
            with open(where, "w") as f:
                f.write(text)

        recs, warnings = trip_analysis.parse(text)
        if not recs:
            return "\n".join(notes + ["No trip recording found in %s." % where])
        out = notes + ["Recording: %s" % where] + ["Warning: " + w for w in warnings] + [""]
        out.append(trip_analysis.summarise(recs[-1]))
        if source == "xy":
            t_trip = _trip_time(recs[-1]["reason"], recs[-1]["uptime_s"])
            if t_trip:
                out += _trip_context(t_trip)
        return "\n".join(out)

    return await run(go)


def main():
    global ALLOW_MOTION, board
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--allow-motion", action="store_true", help="allow commands that move the machine or change its configuration")
    args = ap.parse_args()
    ALLOW_MOTION = args.allow_motion or os.environ.get("MASLOW_ALLOW_MOTION") == "1"
    board = Board()
    mcp.run()


if __name__ == "__main__":
    main()
