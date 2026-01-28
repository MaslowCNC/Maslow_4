// When we can change to proper ESM - uncomment this
// import { sendCommand } from "./maslow";

var interval_status = -1
var probe_progress_status = 0
var grbl_error_msg = ''
var WCO = undefined
var OVR = { feed: undefined, rapid: undefined, spindle: undefined }
var MPOS = [0, 0, 0]
var WPOS = [0, 0, 0]
var grblaxis = 3;
var grblzerocmd = 'X0 Y0 Z0';

let axis_feedrate = [0, 0, 0, 0, 0, 0];
/** gets/sets the GRBL axis feedrates [x, y, z, a, b, c]
 * Note this does not include the probe feed rate
*/
const AxisFeedrate = (value) => {
  if (Array.isArray(value) && value.length === 6) {
    axis_feedrate = value;
  }
  return axis_feedrate;
}

var last_axis_letter = 'Z';

var axisNames = ['x', 'y', 'z', 'a', 'b', 'c']

var gCodeModal = { modes: '', plane: 'G17', units: 'G21', wcs: 'G54', distance: 'G90' }

let calibrationResults = {}

function setClickability(element, visible) {
  setDisplay(element, visible ? 'table-row' : 'none')
}

var autocheck = 'report_auto'
function getAutocheck() {
  return getChecked(autocheck)
}
function setAutocheck(flag) {
  setChecked(autocheck, flag)
}

/** Build the axis selection dropdown, if there are more than 3 axes */
const build_axis_selection = () => {
  const minAxisCount = 3;
  if (grblaxis < minAxisCount) {
    return;
  }

  const axisOpts = [
    '<option value="Z" selected>Z</option>',
    '<option value="A">A</option>',
    '<option value="B">B</option>',
    '<option value="C">C</option>',
  ];

  const html = ["<select class='form-control wauto' id='control_select_axis' onchange='control_changeaxis()' >"];
  for (let i = 3; i <= grblaxis; i++) {
    html.push(axisOpts[i - 3]);
  }
  html.push("</select>");

  setHTML("axis_selection", html.join("\n"));
  setHTML("axis_label", `${translate_text_item('Axis')}:`);
  setClickability("axis_selection", true);
}

/** Change the selected axis. Relevant for axes "Z", "A", "B", "C". Not relevant for axes "X" or "Y" */
function control_changeaxis() {
  const letter = getValue('control_select_axis');
  setHTML('axisup', `+${letter}`);
  setHTML('axisdown', `-${letter}`);
  setHTML('homeZlabel', ` ${letter} `);

  const getLastNonXYFeedRate = getValue('controlpanel_z_feedrate');
  switch (last_axis_letter) {
    case 'Z': AxisFeedrate()[2] = getLastNonXYFeedRate; break;
    case 'A': AxisFeedrate()[3] = getLastNonXYFeedRate; break;
    case 'B': AxisFeedrate()[4] = getLastNonXYFeedRate; break;
    case 'C': AxisFeedrate()[5] = getLastNonXYFeedRate; break;
  }

  // Change over to the new axis that's been selected
  switch (letter) {
    case 'Z': setValue('controlpanel_z_feedrate', AxisFeedrate()[2]); break;
    case 'A': setValue('controlpanel_z_feedrate', AxisFeedrate()[3]); break;
    case 'B': setValue('controlpanel_z_feedrate', AxisFeedrate()[4]); break;
    case 'C': setValue('controlpanel_z_feedrate', AxisFeedrate()[5]); break;
  }

  // And keep a record of it
  last_axis_letter = letter;
}

const floatOrZero = (value) => {
  const val = Number.parseFloat(value);
  return Number.isNaN(val) ? 0.0 : val;
}

const prefList = () => {
  // This has a possible race condition
  // ideally GetPreferencesList() should be awaited on in some way
  // but that would require a lot of changes to the code
  if (!isPreferencesListDefined()) {
    GetPreferencesList();
  }
  return isPreferencesListDefined() ? preferenceslist[0] : default_preferenceslist[0];
}

const probeValues = {
  travel: { fldId: "grblpanel_probemaxtravel", prefId: "probemaxtravel", valType: "float", valTitle: "maximum probe travel", minVal: 1, maxVal: 999, units: "mm" },
  feedrate: { fldId: "grblpanel_probefeedrate", prefId: "probefeedrate", valType: "float", valTitle: "probe feedrate", minVal: 1, maxVal: 9999, units: "mm/min" },
  retract: { fldId: "grblpanel_proberetract", prefId: "proberetract", valType: "float", valTitle: "probe retract", minVal: 0, maxVal: 999, units: "mm" },
  plateThickness: { fldId: "grblpanel_probetouchplatethickness", prefId: "probetouchplatethickness", valType: "float", valTitle: "probe touch plate thickness", minVal: 0, maxVal: 999, units: "mm" },
};

/** This must be done after the preferences have been set */
function init_grbl_panel() {
  const preferences = prefList();
  // Feed rate for X and Y Axes
  AxisFeedrate()[0] = floatOrZero(preferences.xy_feedrate);
  AxisFeedrate()[1] = floatOrZero(preferences.xy_feedrate);

  AxisFeedrate()[2] = floatOrZero(preferences.z_feedrate);
  AxisFeedrate()[3] = floatOrZero(preferences.a_feedrate);
  AxisFeedrate()[4] = floatOrZero(preferences.b_feedrate);
  AxisFeedrate()[5] = floatOrZero(preferences.c_feedrate);

  setValue('controlpanel_xy_feedrate', AxisFeedrate()[0]);
  setValue('controlpanel_z_feedrate', AxisFeedrate()[2]);

  for (const pvFld in probeValues) {
    const pv = probeValues[pvFld];
    if (!(pv.prefId in preferences)) {
      continue;
    }

    const prefValue = preferences[pv.prefId];
    const val = Number.parseFloat(prefValue);
    if (!Number.isNaN(val)) {
      setValue(pv.fldId, val);
    }
  };

  grbl_set_probe_detected(false);
}

function grbl_clear_status() {
  grbl_set_probe_detected(false)
  grbl_error_msg = ''
  setHTML('grbl_status_text', grbl_error_msg)
  setHTML('grbl_status', '')
}

function grbl_set_probe_detected(state) {
  const color = state ? 'green' : 'grey'
  const glyph = state ? 'ok-circle' : 'record'
  setHTML('touch_status_icon', get_icon_svg(glyph, '1.3em', '1.3em', color))
}

const trxOOR = () => translate_text_item("Out of range");
const trxValErr = (valTitle, minVal, maxVal, units) => translate_text_item(`Value of ${valTitle} must be between ${minVal} ${units} and ${maxVal} ${units} !`);
const alertdlgOOR = (valTitle, minVal, maxVal, units) => alertdlg(trxOOR(), trxValErr(valTitle, minVal, maxVal, units));

var reportType = 'none';

function disablePolling() {
  setAutocheck(false)
  // setValue('grblpanel_interval_status', 0);
  if (interval_status !== -1) {
    clearInterval(interval_status)
    interval_status = -1
  }

  grbl_clear_status()
  reportType = 'none'
}

function enablePolling() {
  const interval = getValueFloat("grblpanel_interval_status");
  if (!Number.isNaN(interval)) {
    if (interval === 0) {
      if (interval_status !== -1) {
        clearInterval(interval_status);
      }
      disablePolling();
      reportNone();
      return;
    }
    if (interval > 0 && interval < 100) {
      if (interval_status !== -1) {
        clearInterval(interval_status);
      }
      interval_status = setInterval(() => { get_status() }, interval * 1000);
      reportType = 'polled';
      setChecked('report_poll', true);
      return;
    }
  }

  setValue('grblpanel_interval_status', 0);
  alertdlgOOR("auto-check", 0, 99, "s");
  disablePolling();
  reportNone();
}

function tryAutoReport() {
  if (reportType === 'polled') {
    disablePolling();
  }
  reportType = "auto";
  const interval = getValue("grblpanel_autoreport_interval") ?? 0;
  if (interval === 0) {
    enablePolling();
    return;
  }
  setChecked("report_auto", true);
  reportType = 'auto'
  SendPrinterCommand(
    `$Report/Interval=${interval}`,
    true,
    // Do nothing more on success
    () => { },

    // Fall back to polling if the firmware does not support auto-reports
    () => { enablePolling(); },

    99.1,
    1
  )
}
function onAutoReportIntervalChange() {
  tryAutoReport()
}

function disableAutoReport() {
  SendPrinterCommand('$Report/Interval=0', true, null, null, 99.0, 1)
  setChecked('report_auto', false)
}

function reportNone() {
  switch (reportType) {
    case 'polled':
      disablePolling()
      break
    case 'auto':
      disableAutoReport()
      break
  }
  setChecked('report_none', true)
  reportType = 'none'
}

function reportPolled() {
  if (reportType === 'auto') {
    disableAutoReport()
  }
  enablePolling()
}

function onstatusIntervalChange() {
  enablePolling()
}

//TODO handle authentication issues
//errorfn cannot be NULL
function get_status() {
  //ID 114 is same as M114 as '?' cannot be an ID
  SendPrinterCommand('?', false, null, null, 114, 1)
}

function parseGrblStatus(response) {
  var grbl = {
    stateName: '',
    message: '',
    wco: undefined,
    mpos: undefined,
    wpos: undefined,
    feedrate: 0,
    spindle: undefined,
    spindleSpeed: undefined,
    ovr: undefined,
    lineNumber: undefined,
    flood: undefined,
    mist: undefined,
    pins: undefined,
  }
  response = response.replace('<', '').replace('>', '')
  var fields = response.split('|')
  fields.forEach(function (field) {
    var tv = field.split(':')
    var tag = tv[0]
    var value = tv[1]
    switch (tag) {
      case 'Door':
        grbl.stateName = tag
        grbl.message = field
        break
      case 'Hold':
        grbl.stateName = tag
        grbl.message = field
        break
      case 'Run':
      case 'Jog':
      case 'Idle':
      case 'Home':
      case 'Alarm':
      case 'Check':
      case 'Sleep':
        grbl.stateName = tag
        break

      case 'Ln':
        grbl.lineNumber = parseInt(value)
        break
      case 'MPos':
        grbl.mpos = value.split(',').map(function (v) {
          return parseFloat(v)
        })
        break
      case 'WPos':
        grbl.wpos = value.split(',').map(function (v) {
          return parseFloat(v)
        })
        break
      case 'WCO':
        grbl.wco = value.split(',').map(function (v) {
          return parseFloat(v)
        })
        break
      case 'FS':
        var rates = value.split(',')
        grbl.feedrate = parseFloat(rates[0])
        grbl.spindleSpeed = parseInt(rates[1])
        break
      case 'Ov':
        var rates = value.split(',')
        grbl.ovr = {
          feed: parseInt(rates[0]),
          rapid: parseInt(rates[1]),
          spindle: parseInt(rates[2]),
        }
        break
      case 'A':
        grbl.spindleDirection = 'M5'
        Array.from(value).forEach(function (v) {
          switch (v) {
            case 'S':
              grbl.spindleDirection = 'M3'
              break
            case 'C':
              grbl.spindleDirection = 'M4'
              break
            case 'F':
              grbl.flood = true
              break
            case 'M':
              grbl.mist = true
              break
          }
        })
        break
      case 'SD':
        var sdinfo = value.split(',')
        grbl.sdPercent = parseFloat(sdinfo[0])
        grbl.sdName = sdinfo[1]
        break
      case 'Pn':
        // pin status
        grbl.pins = value
        break
      default:
        // ignore other fields that might happen to be present
        break
    }
  })
  return grbl
}

const clickableFromStateName = (state = "", hasSD = false) => {
  const clickable = {
    resume: false,
    pause: false,
    reset: false,
  };

  if (!["Run", "Hold", "Alarm"].includes(state)) {
    return clickable;
  }

  switch (state) {
    case "Run":
      clickable.pause = true;
      clickable.reset = true;
      break;
    case "Hold":
      clickable.resume = true;
      clickable.reset = true;
      break;
    case "Alarm":
      if (hasSD) {
        //guess print is stopped because of alarm so no need to pause/hold
        clickable.resume = true;
      }
      break;
    default:
      break;
  }

  return clickable;
}

// Update the unified play/pause button based on machine state
const updateUnifiedPlayPauseButton = (stateName, clickable) => {
  // Update the main control area buttons instead of the hidden GRBL panel buttons
  const playButton = id("tablettab_gcode_play");
  const stopButton = id("tablettab_gcode_stop");
  
  if (!playButton || !stopButton) return;

  // Always show the stop button
  stopButton.style.display = '';
  
  // Always show the play button
  playButton.style.display = '';
  
  if (clickable.pause) {
    // Machine is running - show pause button (convert play button to pause)
    playButton.style.backgroundColor = '#f0ad4e'; // Orange background
    playButton.innerHTML = `
      <svg width="2em" height="1.4em" viewBox="0 0 1300 1200">
        <g transform="translate(50,1200) scale(1, -1)">
          <path fill="white"
            d="M250 1000h200q21 0 35.5 -14.5t14.5 -35.5v-800q0 -21 -14.5 -35.5t-35.5 -14.5h-200q-21 0 -35.5 14.5t-14.5 35.5v800q0 21 14.5 35.5t35.5 14.5zM650 1000h200q21 0 35.5 -14.5t14.5 -35.5v-800q0 -21 -14.5 -35.5t-35.5 -14.5h-200q-21 0 -35.5 14.5t-14.5 35.5v800 q0 21 14.5 35.5t35.5 14.5z">
          </path>
        </g>
      </svg>`;
    playButton.onclick = () => SendRealtimeCmd(0x21); // Pause command
    
  } else if (clickable.resume) {
    // Machine is paused - show play button
    playButton.style.backgroundColor = '#5cb85c'; // Green background
    playButton.innerHTML = `
      <svg width="2em" height="1.4em" viewBox="0 0 1300 1200">
        <g transform="translate(50,1200) scale(1, -1)">
          <path fill="white"
            d="M243 1074l814 -498q18 -11 18 -26t-18 -26l-814 -498q-18 -11 -30.5 -4t-12.5 28v1000q0 21 12.5 28t30.5 -4z">
          </path>
        </g>
      </svg>`;
    playButton.onclick = () => SendRealtimeCmd(0x7e); // Resume command
  } else {
    // Machine is idle or in another state - reset button and let tablet.js handle it
    // Only reset if the button was previously in pause mode (orange background) or resume mode (green background)
    const currentBgColor = playButton.style.backgroundColor;
    if (currentBgColor === 'rgb(240, 173, 78)' || currentBgColor === '#f0ad4e' || 
        currentBgColor === 'rgb(92, 184, 92)' || currentBgColor === '#5cb85c') {
      // Reset the button styling
      playButton.style.backgroundColor = '#4aa85c'; // Set div background to green (same as HTML template)
      playButton.onclick = null;
      
      // Restore the canvas element that tablet.js expects, if it doesn't exist
      let playBtnCanvas = id("playBtn");
      if (!playBtnCanvas) {
        playButton.innerHTML = '<canvas id="playBtn" style="width:100%;height:100%"></canvas>';
        playBtnCanvas = id("playBtn");
      }
      
      // Set up the proper click handler for idle state (same as tablet.js would do)
      if (typeof doPlayButton === 'function') {
        playButton.onclick = doPlayButton;
      }
      
      // Draw the white triangle on transparent canvas so div's green background shows through
      if (playBtnCanvas) {
        // Clear any text content and draw triangle
        playBtnCanvas.innerHTML = '';
        
        // Set canvas size to match container
        const rect = playButton.getBoundingClientRect();
        playBtnCanvas.width = rect.width || 200;
        playBtnCanvas.height = rect.height || 200;
        
        const playC = playBtnCanvas.getContext("2d");
        // Clear the canvas (transparent background)
        playC.clearRect(0, 0, playBtnCanvas.width, playBtnCanvas.height);
        
        // Calculate center and size for triangle
        const centerX = playBtnCanvas.width / 2;
        const centerY = playBtnCanvas.height / 2;
        const size = Math.min(playBtnCanvas.width, playBtnCanvas.height) * 0.3;
        
        // Draw white triangle
        playC.beginPath();
        playC.strokeStyle = 'white';
        playC.fillStyle = 'white';
        playC.lineWidth = 1;
        playC.lineCap = 'butt';
        playC.lineJoin = 'miter';
        playC.moveTo(centerX - size/2, centerY - size/2);
        playC.lineTo(centerX - size/2, centerY + size/2);
        playC.lineTo(centerX + size/2, centerY);
        playC.closePath();
        playC.fill();
        playC.stroke();
      }
    }
  }
};

function show_grbl_position(wpos, mpos) {
  if (wpos) {
    wpos.forEach(function (pos, axis) {
      const element = `control_${axisNames[axis]}_position`;
      setHTML(element, pos.toFixed(3));
    });
  }
  if (mpos) {
    mpos.forEach(function (pos, axis) {
      const element = `control_${axisNames[axis]}m_position`;
      setHTML(element, pos.toFixed(3));
    });
  }
}

const show_grbl_status = (stateName = "", message = "", hasSD = false) => {
  setHTML("grbl_status_text", translate_text_item(message))
  setClickability("clear_status_btn", stateName === "Alarm");

  if (!stateName) {
    return;
  }

  setHTML("grbl_status", stateName);
  // Set systemStatus for tablet view (will be updated with progress by show_grbl_SD if file is running)
  setHTML("systemStatus", stateName);

  if (stateName === "Alarm") {
    id("systemStatus").classList.add("system-status-alarm");
  } else {
    id("systemStatus").classList.remove("system-status-alarm");
  }

  const clickable = clickableFromStateName(stateName, hasSD);
  updateUnifiedPlayPauseButton(stateName, clickable);

  // Keep original GRBL panel button behavior for the GRBL tab
  setClickability("sd_resume_btn", clickable.resume);
  setClickability("sd_pause_btn", clickable.pause);
  setClickability("sd_reset_btn", clickable.reset);

  if (stateName == "Hold" && probe_progress_status != 0) {
    probe_failed_notification();
  }
}

function finalize_probing() {
  // No need for this when using the FluidNC-specific G38.6 probe command.
  // SendPrinterCommand("G90", true, null, null, 90, 1);
  probe_progress_status = 0
  setClickability('probingbtn', true)
  setClickability('probingtext', false)
  setClickability('sd_pause_btn', false)
  setClickability('sd_resume_btn', false)
  setClickability('sd_reset_btn', false)
}

function show_grbl_SD(sdName, sdPercent, stateName) {
  const status = sdName
    ? `${sdName}&nbsp;<progress id="print_prg" value=${sdPercent} max="100"></progress>${sdPercent.toFixed(1)}%`
    : ''
  setHTML('grbl_SD_status', status)

  // Also update systemStatus in tablet view with progress when file is running
  // Only show progress if the machine state is "Run" to avoid showing "Run: 100%" when state is "Idle"
  if (sdName && sdPercent != null && !isNaN(sdPercent) && stateName === 'Run') {
    // Show progress when file is running
    const progressStatus = `Run: ${sdPercent.toFixed(1)}%`
    setHTML('systemStatus', progressStatus)
  }
}

function show_grbl_probe_status(probed) {
  grbl_set_probe_detected(probed)
}

function SendRealtimeCmd(code) {
  var cmd = String.fromCharCode(code)
  SendPrinterCommand(cmd, false, null, null, code, 1)
}

function pauseGCode() {
  SendRealtimeCmd(0x21) // '!'
}

function resumeGCode() {
  SendRealtimeCmd(0x7e) // '~'
}

function stopGCode() {
  grbl_reset() // 0x18, ctrl-x
}

// Callback for WCO updates - can be set by other modules
var onWCOUpdateCallback = null;

function grblProcessStatus(response) {
  var grbl = parseGrblStatus(response)
  // Record persistent values of data
  const oldWCO = WCO ? [WCO[0], WCO[1], WCO[2]] : null;
  if (grbl.wco) {
    WCO = grbl.wco;
    // Check if WCO has changed and trigger callback if set
    if (onWCOUpdateCallback && oldWCO &&
        (WCO[0] !== oldWCO[0] || WCO[1] !== oldWCO[1] || WCO[2] !== oldWCO[2])) {
      onWCOUpdateCallback(WCO, oldWCO);
    }
  }
  if (grbl.ovr) {
    OVR = grbl.ovr;
  }
  if (grbl.mpos) {
    MPOS = grbl.mpos;
    if (WCO) {
      WPOS = grbl.mpos.map((v, index) => v - WCO[index]);
    }
  } else if (grbl.wpos) {
    WPOS = grbl.wpos;
    if (WCO) {
      MPOS = grbl.wpos.map((v, index) => v + WCO[index]);
    }
  }
  show_grbl_position(WPOS, MPOS);
  show_grbl_status(grbl.stateName, grbl.message, grbl.sdName);
  show_grbl_SD(grbl.sdName, grbl.sdPercent, grbl.stateName);
  show_grbl_probe_status(grbl.pins && grbl.pins.indexOf('P') !== -1);
  tabletGrblState(grbl, response);
}

function grbl_reset() {
  if (probe_progress_status !== 0) {
    probe_failed_notification();
  }
  SendRealtimeCmd(0x18);
}

function grblGetProbeResult(response) {
  const tab1 = response.split(':')
  if (tab1.length > 2) {
    const status = tab1[2].replace(']', '')
    if (Number.parseInt(status.trim()) === 1) {
      if (probe_progress_status !== 0) {
        const cmd =
          `$J=G90 G21 F1000 Z${getValueFloat("probetouchplatethickness") + getValueFloat("grblpanel_proberetract")}`
        SendPrinterCommand(cmd, true, null, null, 0, 1)
        finalize_probing()
      }
    } else {
      probe_failed_notification()
    }
  }
}

function probe_failed_notification(errMsg = "Probe failed !") {
  finalize_probing();
  alertdlg(translate_text_item('Error'), translate_text_item(errMsg));
  beep(3, 140, 261);
}
const modalModes = [
  { name: 'motion', values: ['G80', 'G0', 'G1', 'G2', 'G3', 'G38.1', 'G38.2', 'G38.3', 'G38.4'] },
  { name: 'wcs', values: ['G54', 'G55', 'G56', 'G57', 'G58', 'G59'] },
  { name: 'plane', values: ['G17', 'G18', 'G19'] },
  { name: 'units', values: ['G20', 'G21'] },
  { name: 'distance', values: ['G90', 'G91'] },
  { name: 'arc_distance', values: ['G90.1', 'G91.1'] },
  { name: 'feed', values: ['G93', 'G94'] },
  { name: 'program', values: ['M0', 'M1', 'M2', 'M30'] },
  { name: 'spindle', values: ['M3', 'M4', 'M5'] },
  { name: 'mist', values: ['M7'] }, // Also M9, handled separately
  { name: 'flood', values: ['M8'] }, // Also M9, handled separately
  { name: 'parking', values: ['M56'] },
]

function grblGetModal(msg) {
  gCodeModal.modes = msg.replace('[GC:', '').replace(']', '')
  var modes = gCodeModal.modes.split(' ')
  gCodeModal.parking = undefined // Otherwise there is no way to turn it off
  gCodeModal.program = '' // Otherwise there is no way to turn it off
  modes.forEach(function (mode) {
    if (mode == 'M9') {
      gCodeModal.flood = mode
      gCodeModal.mist = mode
    } else {
      if (mode.charAt(0) === 'T') {
        gCodeModal.tool = mode.substring(1)
      } else if (mode.charAt(0) === 'F') {
        gCodeModal.feedrate = mode.substring(1)
      } else if (mode.charAt(0) === 'S') {
        gCodeModal.spindle = mode.substring(1)
      } else {
        modalModes.forEach(function (modeType) {
          modeType.values.forEach(function (s) {
            if (mode == s) {
              gCodeModal[modeType.name] = mode
            }
          })
        })
      }
    }
  })
  tabletUpdateModal()
}

// Whenever [MSG: BeginData] is seen, subsequent lines are collected
// in collectedData, until [MSG: EndData] is seen.  Then collectHander()
// is called, if it is defined.
// To run a command that generates such data, first set collectHandler
// to a callback function to receive the data, then issue the command.
var collecting = false
var collectedData = ''
var collectHandler = undefined

// Settings are collected separately because they bracket the data with
// the legacy protocol messages  $0= ... ok
var collectedSettings = null

async function handleCalibrationData(measurements) {
  document.querySelector('#messages').textContent += '\nComputing... This may take several minutes'
  sendCommand("$ACKCAL");
  await sleep(500)
  try {
    calibrationResults = await findMaxFitness(measurements)
  } catch (error) {
    console.error('An error occurred:', error)
  }
}

const grblHandleMessage = (msg) => {
  tabletShowMessage(msg, collecting);

  // We handle these two before collecting data because they can be
  // sent at any time, maybe requested by a timer.

  if (valueStartsWith(msg, ["CLBM:"])) {
    const validJsonMSG = msg
      .replace(/(\b(?:bl|br|tr|tl)\b):/g, '"$1":')
      .replace("CLBM:", "")
      .replace(/,]$/, "]");
    try {
      const measurements = JSON.parse(validJsonMSG);
      handleCalibrationData(measurements);
    } catch (error) {
      console.error("Parsing the GRBL `CLBM` message failed, the calibration data has not been 'handled'. This is probably a programmer error.");
      return;
    }
  }
  if (valueStartsWith(msg, ["<"])) {
    grblProcessStatus(msg);
    return;
  }
  if (valueStartsWith(msg, ["[GC:"])) {
    grblGetModal(msg);
    console.log(msg);
    return;
  }

  // Block data collection
  if (collecting) {
    if (valueStartsWith(msg, ["[MSG: EndData]"])) {
      collecting = false;
      // Finish collecting data
      if (collectHandler) {
        collectHandler(collectedData);
        collectHandler = undefined;
      }
      collectedData = "";
    } else {
      // Continue collecting data
      collectedData += msg;
    }
    return;
  }
  if (valueStartsWith(msg, ["[MSG: BeginData]"])) {
    // Start collecting data
    collectedData = "";
    collecting = true;
    return;
  }

  // Handle probe problem
  if (msg === "[MSG:INFO: No probe pin defined]") {
    probe_failed_notification("No probe pin defined");
    return;
  }

  // Setting collection
  if (collectedSettings) {
    if (valueStartsWith(msg, ["ok"])) {
      // Finish collecting settings
      getESPconfigSuccess(collectedSettings);
      collectedSettings = null;
      if (grbl_errorfn) {
        grbl_errorfn();
        grbl_errorfn = null;
        grbl_processfn = null;
      }
    } else {
      // Continue collecting settings
      collectedSettings += msg;
    }
    return;
  }
  if (valueStartsWith(msg, ["$0=", "$10="])) {
    // Start collecting settings
    collectedSettings = msg;
    return;
  }

  // Handlers for standard Grbl protocol messages

  if (valueStartsWith(msg, ["ok"])) {
    if (grbl_processfn) {
      grbl_processfn();
      grbl_processfn = null;
      grbl_errorfn = null;
    }
    return;
  }
  if (valueStartsWith(msg, ["[PRB:"])) {
    grblGetProbeResult(msg);
    return;
  }
  if (valueStartsWith(msg, ["[MSG:"])) {
    // Check for motor current debugging messages
    if (typeof parseMotorCurrentMessage === 'function' && parseMotorCurrentMessage(msg)) {
      return;
    }
    return;
  }
  if (valueStartsWith(msg, ["error:"])) {
    if (grbl_errorfn) {
      grbl_errorfn(msg.replace("error:", "").trim());
      grbl_errorfn = null;
      grbl_processfn = null;
    }
  }
  if (valueStartsWith(msg, ["error:", "ALARM:", "Hold:", "Door:"])) {
    if (probe_progress_status !== 0) {
      probe_failed_notification();
    }
    if (grbl_error_msg.length === 0) {
      grbl_error_msg = translate_text_item(msg.trim());
    }
    return;
  }
  if (valueStartsWith(msg, ["Grbl "])) {
    console.log("Reset detected");
    return;
  }
};

const checkProbeValue = (pv) => {
  if (!("value" in pv)) {
    if (pv.valType === "int" && typeof getValueInt === "function") {
      pv.value = getValueInt(pv.fldId);
    } else if (pv.valType === "float" && typeof getValueFloat === "function") {
      pv.value = getValueFloat(pv.fldId);
    } else {
      return;
    }
  }
  if (Number.isNaN(pv.value) || pv.value > pv.maxVal || pv.value < pv.minVal) {
    alertdlgOOR(pv.valTitle, pv.minVal, pv.maxVal, pv.units);
    pv.value = Number.NaN;
  }
}

const onprobemaxtravelChange = () => !Number.isNaN(checkProbeValue(probeValues.travel));
const onprobefeedrateChange = () => !Number.isNaN(checkProbeValue(probeValues.feedrate));
const onproberetractChange = () => !Number.isNaN(checkProbeValue(probeValues.retract));
const onprobetouchplatethicknessChange = () => !Number.isNaN(checkProbeValue(probeValues.plateThickness));

function StartProbeProcess() {
  for (const key in probeValues) {
    checkProbeValue(probeValues[key]);
  }
  if (Object.values(probeValues).some(pv => Number.isNaN(pv.value))) {
    return;
  }

  probe_progress_status = 1;
  let restoreReport = false;
  if (reportType === 'none') {
    tryAutoReport(); // will fall back to polled if autoreport fails
    restoreReport = true;
  }

  const cmd = `G38.2 Z-${probeValues.travel.value} F${probeValues.feedrate.value} P${probeValues.plateThickness.value}`;
  SendPrinterCommand(cmd, true, null, null, 38.2, 1);
  setClickability('probingbtn', false);
  setClickability('probingtext', true);
  grbl_error_msg = '';
  setHTML('grbl_status_text', grbl_error_msg);
  if (restoreReport) {
    reportNone();
  }
}

var spindleSpeedSetTimeout
var spindleTabSpindleSpeed = 1

function setSpindleSpeed(speed) {
  if (spindleSpeedSetTimeout) clearTimeout(spindleSpeedSetTimeout)
  if (speed >= 1) {
    spindleTabSpindleSpeed = speed
    spindleSpeedSetTimeout = setTimeout(
      () => SendPrinterCommand('S' + spindleTabSpindleSpeed, false, null, null, 1, 1),
      500
    )
  }
}
