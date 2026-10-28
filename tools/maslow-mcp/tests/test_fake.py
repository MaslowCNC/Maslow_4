#!/usr/bin/env python3
"""End-to-end test: start the fake board, start maslow_mcp.py over stdio, call every tool.

    .venv/bin/python tests/test_fake.py
"""

import asyncio
import os
import re
import subprocess
import sys
import tempfile

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TELNET, HTTP = "2323", "8080"
LOGDIR = tempfile.mkdtemp(prefix="maslow-mcp-test-")  # keep test logs out of the real logs/


def text(result):
    return "\n".join(c.text for c in result.content if hasattr(c, "text"))


async def session(allow_motion, **extra_env):
    env = dict(os.environ, MASLOW_HOST="127.0.0.1", MASLOW_TELNET_PORT=TELNET, MASLOW_HTTP_PORT=HTTP, SPINDLE_HUB="127.0.0.1:1", MASLOW_LOGDIR=LOGDIR,
               MASLOW_PIO=os.path.join(HERE, "fake_pio.sh"), SPINDLE_FLASH_SH=os.path.join(HERE, "fake_flash.sh"), **extra_env)
    args = [os.path.join(ROOT, "maslow_mcp.py")] + (["--allow-motion"] if allow_motion else [])
    return StdioServerParameters(command=sys.executable, args=args, env=env)


failures = []


def check(name, cond, detail):
    print(("PASS " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name)
        print("     " + detail.replace("\n", "\n     "))


async def run(allow_motion):
    params = await session(allow_motion)
    async with stdio_client(params) as (r, w), ClientSession(r, w) as s:
        await s.initialize()
        await asyncio.sleep(3.0)  # let the telnet connection come up (incl. the 2.5 s grace)
        call = lambda n, **a: s.call_tool(n, a)
        tag = "[motion]" if allow_motion else "[read-only]"

        t = text(await call("machine_status"))
        check(tag + " status", '"state": "Idle"' in t or "'state': 'Idle'" in t or "Idle" in t, t)

        if not allow_motion:
            names = {x.name for x in (await s.list_tools()).tools}
            check("tool list", {"send_command", "read_log", "flash_firmware", "spindle_command"} <= names, str(names))

            t = text(await call("send_command", command="$CD"))
            check("heavy read while idle", "trX" in t and t.strip().endswith("ok"), t)
            t = text(await call("get_config", grep="tr"))
            check("get_config filter", "trX" in t and "board:" not in t, t)
            t = text(await call("firmware_info"))
            check("firmware_info", "Maslow-Mini" in t, t)
            t = text(await call("send_command", command="G0 X10"))
            check("motion refused", "--allow-motion" in t, t)
            t = text(await call("send_command", command="$X"))
            check("$X refused", "Refused" in t, t)
            t = text(await call("feed_hold"))
            check("feed_hold always allowed", "sent" in t, t)
            await asyncio.sleep(0.3)
            t = text(await call("read_log", grep="realtime|tick"))
            check("read_log sees broadcasts", "tick" in t and "realtime '!'" in t, t)
            t = text(await call("list_files"))
            check("list_files", "config.yaml" in t, t)
            t = text(await call("download_file", path="/config.yaml"))
            check("download_file", "Maslow S3 Board" in t, t)
            t = text(await call("spindle_command", command="WHY"))
            check("spindle hub down handled", "not reachable" in t, t)
            t = text(await call("spindle_command", command="S5000"))
            check("spindle motion refused", "--allow-motion" in t, t)
            t = text(await call("upload_file", local_path=__file__))
            check("upload refused", "--allow-motion" in t, t)
            t = text(await call("flash_spindle"))
            check("flash_spindle refused", "--allow-motion" in t, t)
            t = text(await call("spindle_trip"))
            check("spindle_trip before any trip", "No /spindle_trip.csv" in t, t)
            t = text(await call("status_history", seconds=30))
            check("status_history polls", "<Idle|MPos" in t, t)
            t = text(await call("read_log"))
            check("status reports kept out of read_log", "<Idle" not in t, t)
        else:
            t = text(await call("send_command", command="G0 X10"))
            check("motion allowed when idle", t.strip() == "ok", t)
            t = text(await call("flash_spindle", wait_for_ip=10))
            check("flash_spindle over WiFi", "to 127.0.0.2" in t and "WiFi upload succeeded" in t
                  and "link back" in t and "USB" not in t, t)
            await asyncio.sleep(3)  # let the fake's OTA chatter finish
            with tempfile.NamedTemporaryFile("w", suffix=".yaml", delete=False) as f:
                f.write("name: test\n")
            t = text(await call("upload_file", local_path=f.name))
            check("upload with size field", '"Ok"' in t, t)
            t = text(await call("spindle_trip", action="test", wait=10))
            check("spindle_trip test + diagnosis", "saved to /spindle_trip.csv" in t and "CLIMBED" in t
                  and "pull-out" in t and "over-current on M2" in t and "Status reports" in t, t)
            # The recording was re-sent 1.5 s after the fault: the context must centre on the fault.
            anchor = re.search(r"around the trip \(([\d:.]+)\)", t)
            fault = re.search(r"([\d:.]+) \[MSG:WARN: SpindleBoard: over-current on M2", t)
            check("spindle_trip context anchored on the fault", anchor and fault and anchor.group(1) == fault.group(1), t)
            t = text(await call("spindle_trip"))
            check("spindle_trip analyse of the current recording", "STALE" not in t and "CLIMBED" in t, t)
            await call("send_command", command="TRIPSTALE")  # a new trip whose recording is held back
            await asyncio.sleep(1)
            t = text(await call("spindle_trip"))
            check("spindle_trip refuses a stale recording", "STALE" in t and "CLIMBED" not in t
                  and "nFAULT retries" in t and "over-current on M2" in t and "Same bytes" in t
                  and "at rest" in t, t)
            t = text(await call("spindle_trip", action="fetch", wait=2))  # the spindle has nothing to re-send
            check("spindle_trip fetch that saves nothing is stale", "STALE" in t and "CLIMBED" not in t, t)
            # Put the fake machine into Run (via a raw line, which the fake treats as a state switch).
            await call("send_command", command="RUN")
            t = text(await call("send_command", command="G0 X20"))
            check("motion refused while running", "busy" in t and "Run" in t, t)
            t = text(await call("send_command", command="$S"))
            check("heavy read refused while running", "busy" in t, t)
            t = text(await call("send_command", command="$G"))
            check("light read allowed while running", t.strip().endswith("ok"), t)


async def run_ota_failure():
    """Both WiFi attempts fail: the retry must use the re-announced address, then USB takes over."""
    params = await session(True, FAKE_PIO_FAIL_UPLOAD="1")
    async with stdio_client(params) as (r, w), ClientSession(r, w) as s:
        await s.initialize()
        await asyncio.sleep(3.0)
        t = text(await s.call_tool("flash_spindle", {"wait_for_ip": 10}))
        check("flash_spindle retries on new address", "Attempt 2: espota upload to 127.0.0.3" in t, t)
        check("flash_spindle USB fallback", "fake USB flash" not in t and "USB upload succeeded" in t, t)


async def main():
    fake = subprocess.Popen([sys.executable, os.path.join(HERE, "fake_board.py"), TELNET, HTTP])
    try:
        await asyncio.sleep(0.5)
        await run(False)
        await run(True)
        await run_ota_failure()
    finally:
        fake.terminate()
    print("\n%d failure(s)" % len(failures))
    sys.exit(1 if failures else 0)


asyncio.run(main())
