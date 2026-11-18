// Every device action the UI can send, and what it becomes on the machine API from PR #1132 (docs/protocol.md,
// ESP3D-WEBUI/www/js/machine.js). `api` is the machine.js action when one exists. "exists" works on Maslow-Main
// today; "planned" needs firmware or machine.js work listed in ESP3D-WEBUI/REWRITE.md ("Where the firmware and
// the UI meet").
export const COMMANDS = {
  retract:        { api: "retractBelts()", cmd: "$ALL", status: "exists" },
  extend:         { api: "extendBelts()", cmd: "$EXT", status: "exists" },
  doneExtending:  { api: null, cmd: null, status: "planned", note: "Extend still ends by itself at extendDist" },
  tension:        { api: "applyTension()", cmd: "$TKSLK", status: "exists", note: "Ends in READY_TO_CUT, or TENSIONED (10) when the belts don't fit the anchors" },
  release:        { api: "releaseTension()", cmd: "$CMP", status: "exists" },
  findAnchors:    { api: "calibrate()", cmd: "$CAL", status: "exists", note: "No progress field yet (issue 16)" },
  toolCheck:      { api: "homeSpindleZ()", cmd: "$Spindle/Home", status: "exists", note: "Mini only. The result (tool or no tool) is spindle board log text until the report has a spindle board field" },
  removeTool:     { api: "removeTool()", cmd: "$Spindle/RemoveTool", status: "exists", note: "Mini only. Done on ok, before the board has finished (or failed); needs the spindle board field" },
  watchForTool:   { api: null, cmd: null, status: "exists", note: "The spindle board loads a bit by itself when it breaks the beam; nothing to send" },
  setZFromStickout: { api: null, cmd: null, status: "planned", note: "Set Z from the measured stick-out" },
  setExtendDist:  { api: "setSetting() + saveConfig()", cmd: "$/Maslow_Extend_Dist=…", status: "exists", note: "0 (extend until Done extending) needs the firmware change" },
  setSpoilboard:  { api: "setSetting()", cmd: "$/Maslow_spoilboardThickness=…", status: "exists" },
  setStock:       { api: "setSetting()", cmd: "$/Maslow_workThickness=…", status: "exists" },
  run:            { api: "runFile(path)", cmd: "$SD/Run=<file>", status: "exists" },
  continueJob:    { api: "resumeFile(path, line)", cmd: "$SD/RunFrom=<line>,<file>", status: "exists", note: "From the report's STOPPED:line,file; discardStopped() forgets it" },
  trace:          { api: "tracePath(points)", cmd: "G0 moves, one at a time", status: "exists" },
  park:           { api: "park()", cmd: "G0 Z, then G53 G0 X Y, to Maslow_Park_X/Y/Z", status: "exists", note: "Needs READY_TO_CUT" },
  stop:           { api: "stop()", cmd: "0x18, then $STOP", status: "exists" },
  clearError:     { api: "clearAlarm()", cmd: "$X", status: "exists", note: "Only in Alarm; done when the machine leaves it" },
  rehomeZ:        { api: "homeSpindleZ()", cmd: "$Spindle/Home", status: "exists", note: "Mini only. Done on ok, before Z has homed; needs the spindle board field" },
  reboot:         { api: null, cmd: "[ESP444]RESTART", status: "exists", note: "HTTP; not in machine.js" },
  deleteFile:     { api: "files.delete(path)", cmd: "GET /upload?action=delete", status: "exists", note: "HTTP, like the current UI" },
  jog:            { api: "jog(delta, {feed})", cmd: "$J=G91 G21 …", status: "exists" },
  setHomeXY:      { api: "setWorkZero({x, y})", cmd: "G21 G10 L20 P0 X… Y…", status: "exists", note: "The current machine position as the origin; needs Idle" },
  setZ:           { api: "setWorkZero({z})", cmd: "G21 G10 L20 P0 Z…", status: "exists", note: "The current machine Z as work Z 0; needs Idle" },
  joinNetwork:    { api: null, cmd: "WiFi settings ([ESP401])", status: "exists", note: "HTTP; placeholder until the WiFi dialog is designed" }
};
