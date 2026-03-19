// When we can change to proper ESM - uncomment this
// import { checkHomed, maslowErrorMsgHandling, maslowInfoMsgHandling, maslowMsgHandling, sendCommand } from "maslow";

// Constants
const FILE_LIST_LOAD_DELAY_MS = 500; // Delay to ensure file list is loaded before restoration

var gCodeLoaded = false;
var gCodeDisplayable = false;
var _gcodeRaw = "";

var snd = null;
var sndok = true;

var versionNumber = "replaceVERSION";

const addMessage = (msg, scroll = true, clear = false) => {
  const msgWindow = id("messages");
  if (msgWindow) {
    msgWindow.textContent = clear ? msg : `${msgWindow.textContent}\n${msg}`;
    if (scroll) {
      msgWindow.scrollTop = msgWindow.scrollHeight;
    }
  }
}

/** Print the version number to the console */
const showVersionNumber = () => addMessage(`Index.html Version: ${versionNumber}`);

function beep(vol, freq, duration) {
  if (snd == null) {
    if (sndok) {
      try {
        snd = new Audio(
          'data:audio/wav;base64,//uQRAAAAWMSLwUIYAAsYkXgoQwAEaYLWfkWgAI0wWs/ItAAAGDgYtAgAyN+QWaAAihwMWm4G8QQRDiMcCBcH3Cc+CDv/7xA4Tvh9Rz/y8QADBwMWgQAZG/ILNAARQ4GLTcDeIIIhxGOBAuD7hOfBB3/94gcJ3w+o5/5eIAIAAAVwWgQAVQ2ORaIQwEMAJiDg95G4nQL7mQVWI6GwRcfsZAcsKkJvxgxEjzFUgfHoSQ9Qq7KNwqHwuB13MA4a1q/DmBrHgPcmjiGoh//EwC5nGPEmS4RcfkVKOhJf+WOgoxJclFz3kgn//dBA+ya1GhurNn8zb//9NNutNuhz31f////9vt///z+IdAEAAAK4LQIAKobHItEIYCGAExBwe8jcToF9zIKrEdDYIuP2MgOWFSE34wYiR5iqQPj0JIeoVdlG4VD4XA67mAcNa1fhzA1jwHuTRxDUQ//iYBczjHiTJcIuPyKlHQkv/LHQUYkuSi57yQT//uggfZNajQ3Vmz+Zt//+mm3Wm3Q576v////+32///5/EOgAAADVghQAAAAA//uQZAUAB1WI0PZugAAAAAoQwAAAEk3nRd2qAAAAACiDgAAAAAAABCqEEQRLCgwpBGMlJkIz8jKhGvj4k6jzRnqasNKIeoh5gI7BJaC1A1AoNBjJgbyApVS4IDlZgDU5WUAxEKDNmmALHzZp0Fkz1FMTmGFl1FMEyodIavcCAUHDWrKAIA4aa2oCgILEBupZgHvAhEBcZ6joQBxS76AgccrFlczBvKLC0QI2cBoCFvfTDAo7eoOQInqDPBtvrDEZBNYN5xwNwxQRfw8ZQ5wQVLvO8OYU+mHvFLlDh05Mdg7BT6YrRPpCBznMB2r//xKJjyyOh+cImr2/4doscwD6neZjuZR4AgAABYAAAABy1xcdQtxYBYYZdifkUDgzzXaXn98Z0oi9ILU5mBjFANmRwlVJ3/6jYDAmxaiDG3/6xjQQCCKkRb/6kg/wW+kSJ5//rLobkLSiKmqP/0ikJuDaSaSf/6JiLYLEYnW/+kXg1WRVJL/9EmQ1YZIsv/6Qzwy5qk7/+tEU0nkls3/zIUMPKNX/6yZLf+kFgAfgGyLFAUwY//uQZAUABcd5UiNPVXAAAApAAAAAE0VZQKw9ISAAACgAAAAAVQIygIElVrFkBS+Jhi+EAuu+lKAkYUEIsmEAEoMeDmCETMvfSHTGkF5RWH7kz/ESHWPAq/kcCRhqBtMdokPdM7vil7RG98A2sc7zO6ZvTdM7pmOUAZTnJW+NXxqmd41dqJ6mLTXxrPpnV8avaIf5SvL7pndPvPpndJR9Kuu8fePvuiuhorgWjp7Mf/PRjxcFCPDkW31srioCExivv9lcwKEaHsf/7ow2Fl1T/9RkXgEhYElAoCLFtMArxwivDJJ+bR1HTKJdlEoTELCIqgEwVGSQ+hIm0NbK8WXcTEI0UPoa2NbG4y2K00JEWbZavJXkYaqo9CRHS55FcZTjKEk3NKoCYUnSQ0rWxrZbFKbKIhOKPZe1cJKzZSaQrIyULHDZmV5K4xySsDRKWOruanGtjLJXFEmwaIbDLX0hIPBUQPVFVkQkDoUNfSoDgQGKPekoxeGzA4DUvnn4bxzcZrtJyipKfPNy5w+9lnXwgqsiyHNeSVpemw4bWb9psYeq//uQZBoABQt4yMVxYAIAAAkQoAAAHvYpL5m6AAgAACXDAAAAD59jblTirQe9upFsmZbpMudy7Lz1X1DYsxOOSWpfPqNX2WqktK0DMvuGwlbNj44TleLPQ+Gsfb+GOWOKJoIrWb3cIMeeON6lz2umTqMXV8Mj30yWPpjoSa9ujK8SyeJP5y5mOW1D6hvLepeveEAEDo0mgCRClOEgANv3B9a6fikgUSu/DmAMATrGx7nng5p5iimPNZsfQLYB2sDLIkzRKZOHGAaUyDcpFBSLG9MCQALgAIgQs2YunOszLSAyQYPVC2YdGGeHD2dTdJk1pAHGAWDjnkcLKFymS3RQZTInzySoBwMG0QueC3gMsCEYxUqlrcxK6k1LQQcsmyYeQPdC2YfuGPASCBkcVMQQqpVJshui1tkXQJQV0OXGAZMXSOEEBRirXbVRQW7ugq7IM7rPWSZyDlM3IuNEkxzCOJ0ny2ThNkyRai1b6ev//3dzNGzNb//4uAvHT5sURcZCFcuKLhOFs8mLAAEAt4UWAAIABAAAAAB4qbHo0tIjVkUU//uQZAwABfSFz3ZqQAAAAAngwAAAE1HjMp2qAAAAACZDgAAAD5UkTE1UgZEUExqYynN1qZvqIOREEFmBcJQkwdxiFtw0qEOkGYfRDifBui9MQg4QAHAqWtAWHoCxu1Yf4VfWLPIM2mHDFsbQEVGwyqQoQcwnfHeIkNt9YnkiaS1oizycqJrx4KOQjahZxWbcZgztj2c49nKmkId44S71j0c8eV9yDK6uPRzx5X18eDvjvQ6yKo9ZSS6l//8elePK/Lf//IInrOF/FvDoADYAGBMGb7FtErm5MXMlmPAJQVgWta7Zx2go+8xJ0UiCb8LHHdftWyLJE0QIAIsI+UbXu67dZMjmgDGCGl1H+vpF4NSDckSIkk7Vd+sxEhBQMRU8j/12UIRhzSaUdQ+rQU5kGeFxm+hb1oh6pWWmv3uvmReDl0UnvtapVaIzo1jZbf/pD6ElLqSX+rUmOQNpJFa/r+sa4e/pBlAABoAAAAA3CUgShLdGIxsY7AUABPRrgCABdDuQ5GC7DqPQCgbbJUAoRSUj+NIEig0YfyWUho1VBBBA//uQZB4ABZx5zfMakeAAAAmwAAAAF5F3P0w9GtAAACfAAAAAwLhMDmAYWMgVEG1U0FIGCBgXBXAtfMH10000EEEEEECUBYln03TTTdNBDZopopYvrTTdNa325mImNg3TTPV9q3pmY0xoO6bv3r00y+IDGid/9aaaZTGMuj9mpu9Mpio1dXrr5HERTZSmqU36A3CumzN/9Robv/Xx4v9ijkSRSNLQhAWumap82WRSBUqXStV/YcS+XVLnSS+WLDroqArFkMEsAS+eWmrUzrO0oEmE40RlMZ5+ODIkAyKAGUwZ3mVKmcamcJnMW26MRPgUw6j+LkhyHGVGYjSUUKNpuJUQoOIAyDvEyG8S5yfK6dhZc0Tx1KI/gviKL6qvvFs1+bWtaz58uUNnryq6kt5RzOCkPWlVqVX2a/EEBUdU1KrXLf40GoiiFXK///qpoiDXrOgqDR38JB0bw7SoL+ZB9o1RCkQjQ2CBYZKd/+VJxZRRZlqSkKiws0WFxUyCwsKiMy7hUVFhIaCrNQsKkTIsLivwKKigsj8XYlwt/WKi2N4d//uQRCSAAjURNIHpMZBGYiaQPSYyAAABLAAAAAAAACWAAAAApUF/Mg+0aohSIRobBAsMlO//Kk4soosy1JSFRYWaLC4qZBYWFRGZdwqKiwkNBVmoWFSJkWFxX4FFRQWR+LsS4W/rFRb/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////VEFHAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAU291bmRib3kuZGUAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAMjAwNGh0dHA6Ly93d3cuc291bmRib3kuZGUAAAAAAAAAACU='
        )
      } catch (error) {
        snd = null
        sndok = false
      }
    }
  }
  if (snd) {
    snd.play()
  }
}

function tabletClick() {
  if (window.navigator?.vibrate) {
    window.navigator.vibrate(200)
  }
  // beep(3, 400, 10)
}

const MDIcmd = (value) => {
  tabletClick();
  sendCommand(value);
}

// const MDI = (field) => {
//   MDIcmd(getValue(field))
// }

// const enterFullscreen = () => {
//   try {
//     document.documentElement.requestFullscreen()
//   } catch (exception) {
//     try {
//       document.documentElement.webkitRequestFullscreen()
//     } catch (exception) {
//       return
//     }
//   }
// }
// const exitFullscreen = () => {
//   try {
//     document.exitFullscreen()
//   } catch (exception) {
//     try {
//       document.webkitExitFullscreen()
//     } catch (exception) {
//       return
//     }
//   }
// }

/** This does nothing, but it does get called */
const toggleFullscreen = () => { }

// const inputFocused = () => { isInputFocused = true; };

// const inputBlurred = () => { isInputFocused = false; };

// Define XY Home functions
let xyHomeTimerId = null;
const xyHomeBtnId = "tablettab_set_xy_home";
const xyHomeLabelDefault = "Define XY Home";
const xyHomeLabelInstr = "Press+Hold Tap_x2";
const xyHomeLabelRedefined = "XY Home Redefined";

const getXYHomeBtnText = () => getText(xyHomeBtnId) || "";
const setXYHomeBtnText = (xyText = xyHomeLabelDefault) => { setText(xyHomeBtnId, xyText); };

const clearXYHomeTimer = () => {
  if (xyHomeTimerId) {
    clearTimeout(xyHomeTimerId);
  }
  xyHomeTimerId = null;
  // Reset the button label
  setTimeout(setXYHomeBtnText, 1000);
}

const setXYHome = () => {
  clearXYHomeTimer();

  // Capture initial WCO values before zeroing
  const oldWCO = WCO ? [WCO[0], WCO[1]] : null;

  zeroAxis("X");
  zeroAxis("Y");
  // This changed label will only show for 1 second before being reset
  setXYHomeBtnText(xyHomeLabelRedefined);

  // Set up one-time callback to refresh display when BOTH X and Y WCO values update
  // This is more efficient than polling
  const originalCallback = onWCOUpdateCallback;
  let timeoutId = null;

  onWCOUpdateCallback = (newWCO, prevWCO) => {
    // Check if BOTH X and Y have changed from initial values
    // We need both axes to update before refreshing
    if (oldWCO && newWCO &&
        (newWCO[0] !== oldWCO[0] && newWCO[1] !== oldWCO[1])) {
      // Both X and Y have updated - clear timeout and refresh
      if (timeoutId) {
        clearTimeout(timeoutId);
        timeoutId = null;
      }
      // Restore the original callback
      onWCOUpdateCallback = originalCallback;
      // Refresh the display with new WCO
      refreshGcode();
    }
    // If only one axis updated, keep waiting for the other
  };

  // Fallback timeout in case WCO update doesn't arrive (e.g., communication error)
  timeoutId = setTimeout(() => {
    onWCOUpdateCallback = originalCallback;
    refreshGcode();
  }, 2000); // 2-second timeout
}

const xyHomeTimer = () => {
  const buttonText = getXYHomeBtnText();
  const buttonValue = Number.isNaN(+buttonText) ? 0 : +buttonText;
  if (buttonValue > 1) {
    setXYHomeBtnText(buttonValue - 1);
    xyHomeTimerId = setTimeout(xyHomeTimer, 1000);
  } else if (buttonValue === 1) {
    // We're actually now at 0 in the countdown
    // Note: nanosecond-scale possible race condition here - quite frankly not a major issue user experience wise
    setXYHome();
  } else {
    // The user clicked / tapped once or didn't press+hold for 5 full seconds
    setTimeout(setXYHomeBtnText, 1000);
  }
}

/** Click down starts the xyHomeTimer function and sets the button text to 5 */
const setHomeClickDown = () => {
  setXYHomeBtnText(5);
  xyHomeTimer();
}

/** Click up cancels the xyHomeTimer and cleans up */
const setHomeClickUp = () => {
  if (xyHomeTimerId != null) {
    setXYHomeBtnText(xyHomeLabelInstr);
  }
}

const zeroAxis = (axis) => {
  tabletClick()
  setAxisByValue(axis, 0)
  addMessage(`Home pos set for: ${axis}`);
}

const getUnitInfo = () => {
  const isInchMode = gCodeModal.units === 'G20';
  const mmPerInch = 25.4;
  return {
    unitLabel: isInchMode ? 'in' : 'mm',
    decimals: isInchMode ? 4 : 3,
    toDisplay: (mm) => isInchMode ? mm / mmPerInch : mm,
  };
}

const getWorkAreaBounds = () => {
  const lv = globalThis.loadedValues || {};
  const areaX = parseFloat(lv.workAreaX) || 2440;
  const areaY = parseFloat(lv.workAreaY) || 1220;
  const offX = parseFloat(lv.workAreaCenterOffsetX) || 0;
  const offY = parseFloat(lv.workAreaCenterOffsetY) || 0;
  return {
    minX: offX - areaX / 2,
    maxX: offX + areaX / 2,
    minY: offY - areaY / 2,
    maxY: offY + areaY / 2,
  };
}

const openSetHomePopup = () => {
  tabletClick();
  const bounds = getWorkAreaBounds();
  // Pre-fill with current machine position so jogging to a spot and opening
  // the popup defaults to "set home here" (confirming without changes sets
  // GCode origin at the current machine position)
  const { unitLabel, decimals, toDisplay } = getUnitInfo();
  const dispMinX = toDisplay(bounds.minX);
  const dispMaxX = toDisplay(bounds.maxX);
  const dispMinY = toDisplay(bounds.minY);
  const dispMaxY = toDisplay(bounds.maxY);
  const xInput = id("setHomeX");
  const yInput = id("setHomeY");
  if (xInput) {
    xInput.value = MPOS ? toDisplay(MPOS[0]).toFixed(decimals) : "0";
    xInput.min = dispMinX;
    xInput.max = dispMaxX;
    xInput.title = `X: ${dispMinX.toFixed(decimals)} to ${dispMaxX.toFixed(decimals)} ${unitLabel}`;
  }
  if (yInput) {
    yInput.value = MPOS ? toDisplay(MPOS[1]).toFixed(decimals) : "0";
    yInput.min = dispMinY;
    yInput.max = dispMaxY;
    yInput.title = `Y: ${dispMinY.toFixed(decimals)} to ${dispMaxY.toFixed(decimals)} ${unitLabel}`;
  }
  const xUnit = id("setHomeXUnit");
  if (xUnit) xUnit.textContent = `(${unitLabel})`;
  const yUnit = id("setHomeYUnit");
  if (yUnit) yUnit.textContent = `(${unitLabel})`;
  const homeLabel = id("currentHomePositionLabel");
  if (homeLabel) {
    const hx = (WCO && WCO.length >= 2) ? toDisplay(WCO[0]).toFixed(decimals) : "0";
    const hy = (WCO && WCO.length >= 2) ? toDisplay(WCO[1]).toFixed(decimals) : "0";
    homeLabel.textContent = `Current: (${hx}, ${hy}) ${unitLabel}`;
  }
  openModal("set-home-popup");
}

const confirmSetHome = () => {
  const bounds = getWorkAreaBounds();
  const { toDisplay } = getUnitInfo();

  // Clamp entered values to work area boundary (values are in current display units)
  const rawX = parseFloat(id("setHomeX").value);
  const rawY = parseFloat(id("setHomeY").value);
  const dispMinX = toDisplay(bounds.minX);
  const dispMaxX = toDisplay(bounds.maxX);
  const dispMinY = toDisplay(bounds.minY);
  const dispMaxY = toDisplay(bounds.maxY);
  const xVal = isNaN(rawX) ? 0 : Math.max(dispMinX, Math.min(dispMaxX, rawX));
  const yVal = isNaN(rawY) ? 0 : Math.max(dispMinY, Math.min(dispMaxY, rawY));

  if (xVal !== rawX || yVal !== rawY) {
    addMessage(`Home position clamped to work area: X=${xVal} Y=${yVal}`);
  }

  hideModal("set-home-popup");

  // xVal, yVal are the desired machine coordinates for the GCode origin (WPOS=0).
  // G10 L20 P0 X{v} sets the current machine position as WPOS=v, so WCO = MPOS - v.
  // To place origin at machine (xVal, yVal), we need WCO = (xVal, yVal),
  // which means we set current WPOS = MPOS - xVal.
  // MPOS is always in mm; convert to current display units for the G10 command.
  const mposX = toDisplay(MPOS ? MPOS[0] : 0);
  const mposY = toDisplay(MPOS ? MPOS[1] : 0);
  const cmd = `G10 L20 P0 X${mposX - xVal} Y${mposY - yVal}`;
  sendCommand(cmd);
  addMessage(`Home pos set: X=${xVal} Y=${yVal}`);
  setXYHomeBtnText(xyHomeLabelRedefined);
  setTimeout(setXYHomeBtnText, 1000);

  // Refresh the canvas once firmware confirms the WCO change.
  // The WCO callback in grbl.js fires synchronously inside grblProcessStatus,
  // before MPOS/WPOS are recalculated with the new WCO.  Using setTimeout(fn,0)
  // defers refreshGcode until after grblProcessStatus finishes, ensuring WPOS
  // is already updated when the canvas redraws.
  // A fallback timeout handles slow connections or cases where WCO value is
  // unchanged (callback won't fire).
  const originalCallback = onWCOUpdateCallback;
  let fallbackId = setTimeout(() => {
    onWCOUpdateCallback = originalCallback;
    refreshGcode();
  }, 3000);

  onWCOUpdateCallback = (newWCO, prevWCO) => {
    clearTimeout(fallbackId);
    onWCOUpdateCallback = originalCallback;
    setTimeout(refreshGcode, 0);
  };
}

const toggleUnits = () => {
  tabletClick()
  sendCommand(gCodeModal.units === 'G21' ? 'G20' : 'G21');
  // The button label will be fixed by the response to $G
  sendCommand('$G');
}

// const btnSetDistance = () => {
//   tabletClick()
//   var distance = event.target.innerText
//   setValue('jog-distance', distance)
// }

// const setDistance = (distance) => {
//   tabletClick()
//   setValue('jog-distance', distance)
// }

const goAxisByValue = (axis, coordinate) => {
  tabletClick()
  moveTo(axis + coordinate)
}

const setAxisByValue = (axis, coordinate) => {
  tabletClick();
  const cmd = `G10 L20 P0 ${axis}${coordinate}`;
  sendCommand(cmd);
}

const setAxis = (axis, field) => {
  tabletClick();
  const cmd = `G10 L20 P1 ${axis}${getValue(field)}`;
  sendCommand(cmd);
}

var timeout_id = 0,
  hold_time = 1000

/** Check the parameters used by jog and move commands,
 * and return them as a composite string */
const checkParams = (params = {}) => {
  if (!Object.keys(params).length) {
    addMessage("Could not perform Jog. No jog parameters supplied. Programmer error.");
    return "";
  }

  if (!("Z" in params) && !checkHomed()) {
    addMessage("Could not perform Jog. Belt lengths are unknown.");
    return "";
  }

  const s = [];
  for (key in params) {
    s.push(`${key}${params[key]}`);
  }
  return s.join("");
}

/** Perform a jog command */
const jog = (params = {}) => {
  const axisAndDistance = checkParams(params);
  if (!axisAndDistance) {
    return;
  }

  jogTo(axisAndDistance);
}

const jogTo = (axisAndDistance) => {
  // Always force G90 mode because synchronization of gCodeModal reports is unreliable
  // JogFeedRate is defined in controls.js
  let feedrate = JogFeedrate(axisAndDistance);
  if (gCodeModal.units === "G20") {
    feedrate /= 25.4;
    feedrate = feedrate.toFixed(2);
  }

  // For safety, always ensure firmware units match UI expectations before jogging
  jogWithUnitsSafeguard(feedrate, axisAndDistance);
}

/** 
 * Safely execute a jog command with firmware units synchronized to UI units.
 * This prevents issues where UI is in mm but firmware is in inches (or vice versa),
 * which could cause dangerous oversized movements.
 * 
 * Strategy: Always force firmware to match UI units before jogging, then restore.
 * This is simpler and more reliable than trying to query and conditionally synchronize.
 */
const jogWithUnitsSafeguard = (feedrate, axisAndDistance) => {
  // Store what units the UI is currently displaying (what user expects)
  const uiExpectedUnits = gCodeModal.units;
  
  // Force firmware to use UI units, execute jog, then query to restore original state
  // This ensures the jog distance is always interpreted correctly
  sendCommand(uiExpectedUnits);
  
  // Small delay to ensure units command is processed
  setTimeout(() => {
    const cmd = `$J=G91F${feedrate}${axisAndDistance}`;
    const unitsLabel = uiExpectedUnits === 'G20' ? 'inch' : 'mm';
    addMessage(`JogTo: ${cmd} (${unitsLabel})`);
    sendCommand(cmd + '\n');
    
    // After jog command, query current state to restore if needed
    // The $G response will be handled by grblGetModal and update the UI automatically
    setTimeout(() => {
      sendCommand('$G');
    }, 200);
  }, 100);
}

/** Peform a move command */
const move = (params = {}) => {
  const location = checkParams(params);
  if (!location) {
    return;
  }

  moveTo(location);
}

const moveTo = (location) => {
  // Always force G90 mode because synchronization of gCodeModal reports is unreliable
  const cmd = `G90 G0 ${location}`;
  sendCommand(cmd);
}

/** Perform jog or move commands based on the supplied command */
const sendMove = (cmd) => {
  tabletClick();

  const distance = cmd.includes('Z')
    ? Number(getText('disZ')) || 0
    : Number(getText('disM')) || 0;

  const jogMoveFnList = {
    G28: () => sendCommand('G28'),
    G30: () => sendCommand('G30'),
    X0Y0Z0: () => move({ X: 0, Y: 0, Z: 0 }),
    X0: () => move({ X: 0 }),
    Y0: () => move({ Y: 0 }),
    Z0: () => move({ Z: 0 }),
    'X-Y+': () => jog({ X: -distance, Y: distance }),
    'X+Y+': () => jog({ X: distance, Y: distance }),
    'X-Y-': () => jog({ X: -distance, Y: -distance }),
    'X+Y-': () => jog({ X: distance, Y: -distance }),
    'X-': () => jog({ X: -distance }),
    'X+': () => jog({ X: distance }),
    'Y-': () => jog({ Y: -distance }),
    'Y+': () => jog({ Y: distance }),
    'Z-': () => jog({ Z: -distance }),
    'Z+': () => jog({ Z: distance }),
    'Z_TOP': () => {
      // She's got legs ♫
      move({ Z: 70 });
    },
  };

  if (cmd in jogMoveFnList) {
    jogMoveFnList[cmd]();
  } else {
    addMessage(`Invalid jog/move command: ${cmd}`);
  }
}

const moveHome = () => {
  if (!checkHomed()) {
    return;
  }

  move({ X: 0, Y: 0 });
}

function saveSerialMessages() {
  // save off the serial messages
  const msgs = getValue('messages') || "";
  const link = document.createElement('a');
  link.setAttribute('href', `data:text/plain;charset=utf-8,${encodeURI(msgs)}`);
  link.setAttribute('download', "Maslow-serial.log");
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
}

/** Loaded Values of the maslow config, this can be a const because we only change the fields within it */
const loaded_values = {};
/** Work with the maslow config loaded values.
 * If `fieldName` is undefined, or `value` is undefined and `fieldname` is not in the values, then return the values we have.
 * If `value` is undefined, but `fieldname` exists, just return the value for `fieldname`
 * Otherwise set `fieldname` to the `value` and return it
 */
const loadedValues = (fieldName, value) => {
  if (typeof fieldName === "undefined") {
    return loaded_values;
  }
  if (typeof value === "undefined") {
    return !(fieldName in loaded_values)
      ? loaded_values
      : loaded_values[fieldName];
  }
  loaded_values[fieldName] = value;
  return loaded_values[fieldName];
};

function tabletShowMessage(msg, collecting) {
  if (collecting || !msg) {
    return;
  }
  if (valueStartsWith(msg, ["<", "ok", "\n", "\r"])) {
    return;
  }

  if (maslowInfoMsgHandling(msg)) {
    return;
  }

  if (valueStartsWith(msg, ["[GC"])) {
    return;
  }

  let errMsg = "";

  //Hide kinematics commands from being displayed in the user log
  if (valueStartsWith(msg, ["$/kinematics"])) {
    return; //We don't want to display these messages
  }

  //These are used for populating the Maslow configuration popup
  if (valueStartsWith(msg, ["$/Maslow_", "$/maslow_"])) {
    errMsg = maslowMsgHandling(msg.substring(9));
    return; //We don't want to display these messages
  }

  // Filter out motor current messages from console display (they're still processed for debugging)
  if (/\[MSG:INFO:\s*TLC:\s*[\d.]+\s*TRC:\s*[\d.]+\s*BLC:\s*[\d.]+\s*BRC:\s*[\d.]+\]/.test(msg)) {
    return; //We don't want to display these messages
  }

  addMessage(`${maslowErrorMsgHandling(msg) || msg}`);
}

function tabletShowResponse(response) { }

function clearAlarm() {
  if (getText('systemStatus') === 'Alarm') {
    id('systemStatus').classList.remove('system-status-alarm')
    SendPrinterCommand('$X', true, null, null, 114, 1)
  }
}

function setJogSelector(units) {
  let buttonDistances = [];
  let menuDistances = [];
  let selected = 0;
  if (units === "G20") {
    // Inches
    buttonDistances = [0.001, 0.01, 0.1, 1, 0.003, 0.03, 0.3, 3, 0.005, 0.05, 0.5, 5];
    menuDistances = [0.00025, 0.0005, 0.001, 0.003, 0.005, 0.01, 0.03, 0.05, 0.1, 0.3, 0.5, 1, 3, 5, 10, 30];
    selected = "1";
  } else {
    // millimeters
    buttonDistances = [0.1, 1, 10, 100, 0.3, 3, 30, 300, 0.5, 5, 50, 500];
    menuDistances = [0.005, 0.01, 0.03, 0.05, 0.1, 0.3, 0.5, 1, 3, 5, 10, 30, 50, 100, 300, 500, 1000];
    selected = "10";
  }
  // const buttonNames = [
  //   'jog00',
  //   'jog01',
  //   'jog02',
  //   'jog03',
  //   'jog10',
  //   'jog11',
  //   'jog12',
  //   'jog13',
  //   'jog20',
  //   'jog21',
  //   'jog22',
  //   'jog23',
  // ]
  //buttonNames.forEach( function(n, i) { setHTML(n, buttonDistances[i]); } );

  // var selector = id('jog-distance');
  // selector.length = 0;
  // selector.innerText = null;
  // menuDistances.forEach(function(v) {
  //     var option = document.createElement("option");
  //     option.textContent=v;
  //     option.selected = (v == selected);
  //     selector.appendChild(option);
  // });
}
function removeJogDistance(option, oldIndex) {
  //selector = id('jog-distance');
  //selector.removeChild(option);
  //selector.selectedIndex = oldIndex;
}
function addJogDistance(distance) {
  //selector = id('jog-distance');
  //var option = document.createElement("option");
  //option.textContent=distance;
  //option.selected = true;
  //return selector.appendChild(option);
}

var runTime = 0

function setButton(name, isEnabled, color, text) {
  const button = id(name);
  if (!button) {
    return;
  }
  button.disabled = !isEnabled;
  button.style.backgroundColor = color;
  button.innerText = text;
}

var playButtonHandler
function setPlayButton(isEnabled, color, text, click) {
  setButton('playBtn', isEnabled, color, text);
  playButtonHandler = click;
}
function doPlayButton() {
  if (playButtonHandler) {
    playButtonHandler()
  }

  const filelist = id('filelist');
  if (filelist && filelist.options.length > 0) {
    addMessage(`Starting File: ${filelist.options[filelist.selectedIndex].text}`);
  }
}

// var pauseButtonHandler
// function setPauseButton(isEnabled, color, text, click) {
//   setButton('pauseBtn', isEnabled, color, text);
//   pauseButtonHandler = click
// }
// function doPauseButton() {
//   if (pauseButtonHandler) {
//     pauseButtonHandler()
//   }
// }

const green = "#86f686";
const red = "#f64646";
const gray = "#f6f6f6";
const orange = "#ff9500";
const stopRed = "#ce654c";

function setRunControls() {
  if (gCodeLoaded) {
    // A GCode file is ready to go
    setPlayButton(true, green, 'Start', runGCode)
    //setPauseButton(false, gray, 'Pause', null)
  } else {
    // Can't start because no GCode to run
    setPlayButton(false, gray, 'Start', null)
    //setPauseButton(false, gray, 'Pause', null)
  }
}

var grblReportingUnits = 0
var startTime = 0

var spindleDirection = ''
var spindleSpeed = ''

function stopAndRecover() {
  stopGCode()
  // To stop GRBL you send a reset character, which causes some modes
  // be reset to their default values.  In particular, it sets G21 mode,
  // which affects the coordinate display and the jog distances.
  requestModes()
}

var oldCannotClick = null

function scaleUnits(target) {
  //Scale the units to move when jogging down or up by 25.4 to keep them reasonable
  const distanceElement = id(target);
  const currentValue = Number(distanceElement.innerText);

  if (!Number.isNaN(currentValue)) {
    // When converting to inches, round to 3 decimal places for display
    if (gCodeModal.units == 'G20') {
      distanceElement.innerText = (currentValue / 25.4).toFixed(3);
    } else {
      // When converting to mm, round to 2 decimal places for display
      distanceElement.innerText = (currentValue * 25.4).toFixed(2);
    }
  } else {
    console.error('Invalid number in disM element');
  }
}

function tabletUpdateModal() {
  const newUnits = gCodeModal.units === "G21" ? "mm" : "Inch";
  const isInch = gCodeModal.units === "G20";
  id("tablettab_toggle_units").style.backgroundColor = isInch ? "#e6c800" : "#f2f0e4";

  if (getValue("tablettab_toggle_units") === newUnits) {
    return;
  }

  setText("tablettab_toggle_units", newUnits);
  setJogSelector(gCodeModal.units);
  scaleUnits("disM");
  scaleUnits("disZ");
}

function tabletGrblState(grbl, response) {
  // tabletShowResponse(response)
  const stateName = grbl.stateName;

  // Unit conversion factor - depends on both $13 setting and parser units
  let factor = 1.0;

  //  spindleSpeed = grbl.spindleSpeed;
  //  spindleDirection = grbl.spindle;
  //
  //  feedOverride = OVR.feed/100.0;
  //  rapidOverride = OVR.rapid/100.0;
  //  spindleOverride = OVR.spindle/100.0;

  const mmPerInch = 25.4;
  switch (gCodeModal.units) {
    case 'G20':
      factor = grblReportingUnits === 0 ? 1 / mmPerInch : 1.0
      break
    case 'G21':
      factor = grblReportingUnits === 0 ? 1.0 : mmPerInch
      break
  }

  const cannotClick = stateName === 'Run' || stateName === 'Hold'
  // Recompute the layout only when the state changes
  if (oldCannotClick !== cannotClick) {
    setDisabled('.dropdown-toggle', cannotClick)
    setDisabled('.axis-position .position', cannotClick)
    setDisabled('.axis-position .form-control', cannotClick)
    setDisabled('.axis-position .btn', cannotClick)
    setDisabled('.axis-position .position', cannotClick)
    // if (!cannotClick) {
    //     contractVisualizer();
    // }
  }
  oldCannotClick = cannotClick

  tabletUpdateModal()

  // When a stop was requested and the machine is now Idle or Alarm, cancel
  // further retries.  Idle = normal stop; Alarm = stop triggered an alarm
  // (e.g. watchdog fired mid-stop).  Either way the machine is no longer
  // running, so continuing to send $STOP would be harmful.
  if (_stopPending && (stateName === 'Idle' || stateName === 'Alarm')) {
    _stopPending = false;
  }

  switch (stateName) {
    case 'Sleep':
    case 'Alarm':
      setPlayButton(true, gray, 'Start', null)
      //setPauseButton(false, gray, 'Pause', null)
      break
    case 'Idle':
      setRunControls()
      break
    case 'Hold':
      setPlayButton(true, green, 'Resume', resumeGCode)
      //setPauseButton(true, red, 'Stop', stopAndRecover)
      break
    case 'Jog':
    case 'Home':
      setPlayButton(false, gray, 'Start', null)
      break
    case 'Run':
      setPlayButton(true, orange, 'Pause', pauseGCode)
      break
    case 'Check':
      setPlayButton(true, gray, 'Start', null)
      //setPauseButton(true, red, 'Stop', stopAndRecover)
      break
  }

  if (grbl.spindleDirection) {
    switch (grbl.spindleDirection) {
      case "M3": spindleDirection = "CW"; break;
      case "M4": spindleDirection = "CCW"; break;
      case "M5": spindleDirection = "Off"; break;
      default: spindleDirection = ""; break;
    }
  }

  //setText('spindle-direction', spindleDirection);

  spindleSpeed = grbl.spindleSpeed ? Number(grbl.spindleSpeed) : "";
  //setText('spindle-speed', spindleSpeed);

  const now = new Date();
  //setText('time-of-day', now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0'));
  if (stateName === 'Run') {
    let elapsed = now.getTime() - startTime;
    if (elapsed < 0) {
      elapsed = 0;
    }
    let seconds = Math.floor(elapsed / 1000);
    const minutes = Math.floor(seconds / 60);
    seconds = seconds % 60;
    if (seconds < 10) {
      seconds = `0${seconds}`;
    };
    runTime = `${minutes}:${seconds}`;
  } else {
    startTime = now.getTime();
  }

  //setText('runtime', runTime);

  //setText('wpos-label', gCodeModal.wcs);
  const distanceText = gCodeModal.distance === 'G90' ? gCodeModal.distance : `<div style='color:red'>${gCodeModal.distance}</div>`;
  //setHTML('distance', distanceText);

  let stateText = '';
  if (stateName === 'Run') {
    const rateNumber = gCodeModal.units === 'G21'
      ? Number(grbl.feedrate).toFixed(0)
      : Number(grbl.feedrate / 25.4).toFixed(2)

    const rateText = rateNumber + (gCodeModal.units === 'G21' ? ' mm/min' : ' in/min')

    stateText = `${rateText} ${spindleSpeed} ${spindleDirection}`;
  } else {
    // var stateText = errorText == 'Error' ? "Error: " + errorMessage : stateName;
    stateText = stateName;
  }
  //setText('active-state', stateText);

  // var modeText = `${gCodeModal.distance} ${gCodeModal.wcs} ${gCodeModal.units} T${gCodeModal.tool} F${gCodeModal.feedrate} S${gCodeModal.spindle}`;

  if (grbl.sdLineNumber && ["Run", "Hold"].includes(stateName)) {
    if (gCodeDisplayable) {
      scrollToLine(grbl.sdLineNumber);
    }
  }
  // Always update tool position, even without GCode loaded
  tpDisplayer().reDrawTool(gCodeModal, arrayToXYZ(WPOS));

  const digits = gCodeModal.units === 'G20' ? 4 : 2;

  if (WPOS) {
    WPOS.forEach((pos, index) => {
      setTextContent(`wpos-${axisNames[index]}`, Number(pos * factor).toFixed(index > 2 ? 2 : digits));
    })
  }

  if (MPOS) {
    MPOS.forEach((pos, index) => {
      const axisName = axisNames[index].toUpperCase();
      setTextContent(`mpos-${axisNames[index]}`, `|${axisName}m: ${Number(pos * factor).toFixed(index > 2 ? 2 : digits)}|`);
    })
  }
}

let gCodeFilename = '';

// Flag to prevent concurrent GCode state restoration attempts
let restoringGCodeState = false;

// GCode state persistence functions
const saveGCodeState = () => {
  if (gCodeFilename && gCodeLoaded) {
    store_localdata('gCodeFilename', gCodeFilename);
    store_localdata('gCodeLoaded', 'true');
    console.log(`GCode state saved: ${gCodeFilename}`);
  }
  // Note: We don't automatically clear state here if conditions aren't met
  // State should only be cleared explicitly via clearGCodeState()
};

const clearGCodeState = () => {
  delete_localdata('gCodeFilename');
  delete_localdata('gCodeLoaded');
  console.log('GCode state cleared');
};

const restoreGCodeState = () => {
  // Prevent concurrent restoration attempts
  if (restoringGCodeState) {
    console.log('GCode restoration already in progress, skipping');
    return;
  }

  const savedFilename = get_localdata('gCodeFilename');
  const savedLoaded = get_localdata('gCodeLoaded');

  if (savedFilename && savedLoaded === 'true') {
    console.log(`Restoring GCode state: ${savedFilename}`);
    restoringGCodeState = true;

    // Check if the file still exists by trying to load it
    // Note: encodeURIComponent encodes the entire SD path, matching the pattern used in
    // tabletLoadGCodeFile (lines 1105, 1130) for consistency with existing code
    fetch(encodeURIComponent(`SD${savedFilename}`), { method: 'HEAD' })
      .then((response) => {
        if (response.ok) {
          // File exists, load it
          const contentLength = response.headers.get('Content-Length');
          const size = contentLength ? parseInt(contentLength, 10) : 0;
          tabletLoadGCodeFile(savedFilename, size);
        } else {
          // File doesn't exist anymore, clear state
          console.log('Saved GCode file no longer exists, clearing state');
          clearGCodeState();
        }
      })
      .catch((error) => {
        console.log('Error checking GCode file, clearing state:', error);
        clearGCodeState();
      })
      .finally(() => {
        restoringGCodeState = false;
      });
  }
};

const tabletDOMActivate = () => {
  fullscreenIfMobile();
  setBottomHeight();
  // Restore GCode state when tablet tab is activated
  // This handles the case where the page was loaded but user navigates to tablet tab later
  // Only attempt restoration if no file is currently loaded and not already restoring
  if (!gCodeFilename && !restoringGCodeState) {
    // Delay is needed to ensure file list has been populated by files_refreshFiles()
    setTimeout(() => {
      // Check again after delay in case state changed
      if (!gCodeFilename) {
        restoreGCodeState();
      }
    }, FILE_LIST_LOAD_DELAY_MS);
  }
}

// Button event handlers - First Row
const tabletMoveZUp = () => sendMove("Z+");
const tabletMoveTopLeft = () => sendMove("X-Y+");
const tabletMoveTop = () => sendMove("Y+");
const tabletMoveTopRight = () => sendMove("X+Y+");
const tabletCalibrationOpen = () => {
  loadCornerValues();
  openModal("calibration-popup");
}
// Button event handlers - Second Row
const tabletMoveLeft = () => sendMove("X-");
const tabletMoveRight = () => sendMove("X+");
// Button event handlers - Third Row
const tabletMoveZDown = () => sendMove("Z-");
const tabletMoveBottomLeft = () => sendMove("X-Y-");
const tabletMoveBottom = () => sendMove("Y-");
const tabletMoveBottomRight = () => sendMove("X+Y-");
// Button event handlers - Fourth Row
const tabletSetZHomeMDown = () => zeroAxis("Z");
const tabletSetZHomeMUp = () => refreshGcode();
// Button event handlers - Fifth Row - nothing special here, move on

// Send a command directly via WebSocket to bypass PAGEID routing.
// Returns true if the command was sent, false if the WebSocket is not open.
const sendViaWS = (cmd) => {
  if (ws_source && ws_source.readyState === WebSocket.OPEN) {
    try {
      ws_source.send(cmd);
      return true;
    } catch (e) {
      console.warn("WebSocket send failed:", e);
    }
  }
  return false;
};

// True when a stop has been requested but not yet confirmed delivered.
// onWSOpenCallback sends $STOP on every WebSocket (re)connect while this
// flag is set, so the command reaches the firmware before any auto-reports
// start flowing.
let _stopPending = false;

// Called from ws_source.onopen (socket.js) on every WebSocket (re)connect.
// Does NOT clear _stopPending — tabletGrblState clears it once the firmware
// confirms the machine is no longer running (stateName === 'Idle').
const onWSOpenCallback = () => {
  if (_stopPending) {
    try {
      ws_source.send("$STOP\n");
    } catch (e) {
      console.warn("Failed to send pending $STOP on connect:", e);
    }
  }
};

// Send $STOP directly via WebSocket to bypass PAGEID routing.
// Sets _stopPending and retries every 300 ms for up to ~10 seconds.
// _stopPending is cleared by tabletGrblState when the firmware confirms
// the machine is Idle, or by the timeout.  onWSOpenCallback also sends
// $STOP as the very first message on every WebSocket (re)connect while
// the flag is set, so the command survives a TCP drop between send and
// firmware processing.
const sendStopCommand = () => {
  const RETRY_INTERVAL_MS = 300;
  const MAX_RETRY_ATTEMPTS = 33; // 33 * 300ms ≈ 10 seconds
  _stopPending = true;
  sendViaWS("$STOP\n"); // Try immediately; keep _stopPending for retries
  let attempts = 0;
  const retryTimer = setInterval(() => {
    if (!_stopPending) {
      clearInterval(retryTimer);
      return;
    }
    sendViaWS("$STOP\n");
    if (++attempts >= MAX_RETRY_ATTEMPTS) {
      _stopPending = false;
      clearInterval(retryTimer);
      console.warn("$STOP retry limit reached without firmware confirmation; machine may not have stopped");
    }
  }, RETRY_INTERVAL_MS);
  scheduleCallback(() => {
    if (!sendViaWS("$MINFO\n")) {
      sendCommand('$MINFO');
    }
  }, 1000);
};

// Button event handlers - Sixth Row
const tabletGCodeStop = () => {
  const stopBtn = id("tablettab_gcode_stop");
  if (stopBtn) {
    stopBtn.style.backgroundColor = orange;
  }
  addMessage("Stop Maslow and Gcode");
  sendStopCommand();
};

const resetStopButtonColors = () => {
  // tablettab_gcode_stop uses an inline style so we must set it explicitly
  const gcodeStopBtn = id("tablettab_gcode_stop");
  if (gcodeStopBtn) {
    gcodeStopBtn.style.backgroundColor = stopRed;
  }
  // tablettab_cal_stop uses the .stop-button CSS class with !important, so
  // removing the inline style lets the class rule take effect again
  const calStopBtn = id("tablettab_cal_stop");
  if (calStopBtn) {
    calStopBtn.style.removeProperty('background-color');
  }
};
// Control event handlers - Calibration Popup
const tabletCalPopupHide = () => hideModal("calibration-popup");

// Helper function to set focus back to tablet view
const returnFocusToTablet = () => {
  const tabletListener = id("tablet-listener");
  if (tabletListener) {
    tabletListener.focus();
  }
};

const tabletCalRetract = () => {
  onCalibrationButtonsClick("$ALL", "Retract All");
  returnFocusToTablet();
};
const tabletCalExtend = () => {
  onCalibrationButtonsClick("$EXT", "Extend All");
  returnFocusToTablet();
};
const tabletCalCalibrate = () => {
  onCalibrationButtonsClick("$CAL", "Find Anchors");
  scheduleCallback(() => { hideModal("calibration-popup"); }, 1000);
};
const tabletCalTense = () => {
  onCalibrationButtonsClick("$TKSLK", "Apply Tension");
  scheduleCallback(() => { hideModal("calibration-popup"); }, 1000);
};
// const tabletCalZHome = () => onCalibrationButtonsClick("$TKSLK", "Home Z");
const tabletCalOpenConfig = () => {
  loadConfigValues();
  openModal("configuration-popup");
};
const tabletCalStop = () => {
  const stopBtn = id("tablettab_cal_stop");
  if (stopBtn) {
    stopBtn.style.setProperty('background-color', orange, 'important');
  }
  addMessage("Stop");
  sendStopCommand();
  returnFocusToTablet();
};
const tabletCalSetZStop = () => {
  onCalibrationButtonsClick("$SETZSTOP", "Set Z-Stop");
  returnFocusToTablet();
};
const tabletCalTest = () => {
  onCalibrationButtonsClick("$TEST", "Test");
  scheduleCallback(() => { hideModal("calibration-popup"); }, 1000);
};
const tabletCalRelax = () => {
  onCalibrationButtonsClick("$CMP", "Release Tension");
  returnFocusToTablet();
};

// Handler for the new Maslow action button (below Setup button)
const handleMaslowActionButtonClick = () => {
  if (typeof maslowStatus === 'undefined') {
    return;
  }
  
  // Execute action based on Maslow state
  switch (maslowStatus.state) {
    case 0: // UNKNOWN - Retract
      tabletCalRetract();
      break;
    case 2: // RETRACTED - Extend
      tabletCalExtend();
      break;
    case 4: // EXTENDEDOUT - Apply Tension
      tabletCalTense();
      break;
    // State 7 (READY_TO_CUT) and others don't need a click action
  }
};

// Control event handlers - Configuration Popup
const tabletConfigPopupHide = () => hideModal("configuration-popup");
// Control event handlers - Common
const tabletPopupStopProp = (event) => event.stopPropagation();

function tabletInit() {
  // put in a timeout to allow things to settle. when they were here at startup ui froze from time to time.
  setTimeout(() => {
    showVersionNumber();

    // get grbl status
    SendRealtimeCmd(0x3f); // ?
    // print startup messages in serial
    SendPrinterCommand("$SS");
    // get maslow info
    SendPrinterCommand("$MINFO");
    files_refreshFiles("/");
    requestModes();
    loadConfigValues();
    loadCornerValues();

    SendPrinterCommand("$GSTATE");

    numpad.attach({ target: "disM", axis: "D" });
    numpad.attach({ target: "disZ", axis: "Z" });
    //numpad.attach({target: "wpos-y", axis: "Y"});
    //numpad.attach({target: "wpos-z", axis: "Z"});
    //numpad.attach({target: "wpos-a", axis: "A"});

    setJogSelector('mm');
    loadJogDists();

    // Set WiFi SSID pattern validation dynamically
    const wifiSSIDInput = id("wifiSSID");
    if (wifiSSIDInput) {
      wifiSSIDInput.setAttribute("pattern", SSID_PATTERN);
      wifiSSIDInput.setAttribute("title", SSID_PATTERN_TITLE);
      
      // Add input filter to restrict characters in real-time
      wifiSSIDInput.addEventListener("input", function(e) {
        const input = e.target;
        const cursorPosition = input.selectionStart;
        const oldValue = input.value;
        
        // If the current value doesn't match the pattern, filter it
        if (!getSSIDFullPatternRegex().test(oldValue)) {
          // Remove invalid characters using the shared char pattern
          const newValue = oldValue.split('').filter(char => {
            return getSSIDCharPatternRegex().test(char);
          }).join('');
          
          input.value = newValue;
          // Restore cursor position (adjusted for removed characters)
          const removedCount = oldValue.length - newValue.length;
          input.setSelectionRange(cursorPosition - removedCount, cursorPosition - removedCount);
        }
      });
    }

    id("tablettablink").addEventListener("DOMActivate", tabletDOMActivate, false);

    // Buttons - First Row
    id("tablettab_zUp").addEventListener("click", tabletMoveZUp);
    id("tablettab_topLeft").addEventListener("click", tabletMoveTopLeft);
    id("tablettab_top").addEventListener("click", tabletMoveTop);
    id("tablettab_topRight").addEventListener("click", tabletMoveTopRight);
    id("calibrationBTN").addEventListener("click", tabletCalibrationOpen);

    // Buttons - Second Row
    id("tablettab_left").addEventListener("click", tabletMoveLeft);
    id("tablettab_right").addEventListener("click", tabletMoveRight);

    // Buttons - Third Row
    id("tablettab_zDown").addEventListener("click", tabletMoveZDown);
    id("tablettab_bottomLeft").addEventListener("click", tabletMoveBottomLeft);
    id("tablettab_bottom").addEventListener("click", tabletMoveBottom);
    id("tablettab_bottomRight").addEventListener("click", tabletMoveBottomRight);

    // Buttons - Fourth Row
    id("tablettab_set_z_home").addEventListener("mousedown", tabletSetZHomeMDown);
    id("tablettab_set_z_home").addEventListener("mouseup", tabletSetZHomeMUp);
    id("tablettab_move_to_xy_home").addEventListener("click", moveHome);
    id("tablettab_toggle_units").addEventListener("click", toggleUnits);
    id("tablettab_set_xy_home").addEventListener("click", openSetHomePopup);

    // Buttons - Set Home Pop-up
    id("set-home-popup").addEventListener("click", () => hideModal("set-home-popup"));
    id("set_home_popup_content").addEventListener("click", tabletPopupStopProp);
    id("tablettab_set_home_cancel").addEventListener("click", () => hideModal("set-home-popup"));
    id("tablettab_set_home_confirm").addEventListener("click", confirmSetHome);

    // Controls - Fifth Row
    id("filelist").addEventListener("change", selectFile);
    id("tablettab_gcode_upload").addEventListener("click", files_select_upload);
    id("tablettab_gcode_delete").addEventListener("click", tabletDeleteGCodeFile);

    // Buttons - Sixth Row
    id("tablettab_gcode_play").addEventListener("click", doPlayButton);
    // id("tablettab_gcode_pause").addEventListener("click", doPauseButton);
    id("tablettab_gcode_stop").addEventListener("click", tabletGCodeStop);
    id("systemStatus").addEventListener("click", clearAlarm);
    
    // New Maslow action button (below Setup)
    id("maslowActionButton").addEventListener("click", handleMaslowActionButtonClick);

    id("tablettab_save_serial_msg").addEventListener("click", saveSerialMessages);
    
    // Trace boundary button
    id("tablettab_trace_boundary").addEventListener("click", traceBoundary);

    // Buttons - Calibration Pop-up
    id("calibration-popup").addEventListener("click", tabletCalPopupHide);
    id("calibration_popup_content").addEventListener("click", tabletPopupStopProp);
    id("tablettab_cal_retract").addEventListener("click", tabletCalRetract);
    id("tablettab_cal_extend").addEventListener("click", tabletCalExtend);
    id("tablettab_cal_calibrate").addEventListener("click", tabletCalCalibrate);
    id("tablettab_cal_tense").addEventListener("click", tabletCalTense);
    // id("tablettab_cal_homez").addEventListener("click", tabletCalZHome);
    id("tablettab_cal_config").addEventListener("click", tabletCalOpenConfig);
    id("tablettab_cal_stop").addEventListener("click", tabletCalStop);
    id("tablettab_cal_zstop").addEventListener("click", tabletCalSetZStop);
    id("tablettab_cal_test").addEventListener("click", tabletCalTest);
    id("tablettab_cal_relax").addEventListener("click", tabletCalRelax);

    // Buttons - Configuration Pop-up
    id("configuration-popup").addEventListener("click", tabletConfigPopupHide);
    id("configuration_popup_content").addEventListener("click", tabletPopupStopProp);
    id("tablettab_config_save").addEventListener("click", saveConfigValues);

  }, 1000);
}

const showGCode = (gcode, append = false, updateToolpath = true) => {
  gCodeLoaded = gcode !== "";
  if (!gCodeLoaded) {
    _gcodeRaw = "";
    setValue("tablettab_gcode", "(No GCode loaded)");
    tpDisplayer().clear();
  } else {
    let startLine;
    if (append) {
      startLine = (_gcodeRaw.match(/\n/g) || []).length + 1;
      _gcodeRaw += gcode;
    } else {
      startLine = 1;
      _gcodeRaw = gcode;
    }
    const lines = gcode.split("\n");
    const endsWithNewline = lines.length > 0 && lines[lines.length - 1] === "";
    if (endsWithNewline) lines.pop();
    const numbered = lines.map((line, i) => `(${startLine + i}) ${line}`).join("\n") + (endsWithNewline ? "\n" : "");
    if (append) {
      setValue("tablettab_gcode", (getValue("tablettab_gcode") || "") + numbered);
    } else {
      setValue("tablettab_gcode", numbered);
    }
    if (gCodeDisplayable && updateToolpath) {
      tpDisplayer().showToolpath(_gcodeRaw, gCodeModal, arrayToXYZ(WPOS));
      updateJobBoundsDisplay();
    }
  }

  // TODO: this needs to take into account error states
  setRunControls();
}

function nthLineEnd(str, n) {
  if (n <= 0) {
    return 0;
  }
  const L = str.length;
  let i = -1;
  let count = n;
  while (count-- && i++ < L) {
    i = str.indexOf('\n', i);
    if (i < 0) {
      break;
    }
  }
  return i;
}

function scrollToLine(lineNumber) {
  const gCodeLines = id("tablettab_gcode");
  const lineHeight = Number.parseFloat(getComputedStyle(gCodeLines).getPropertyValue('line-height'));
  const gCodeText = gCodeLines.value;

  gCodeLines.scrollTop = Math.max(0, (lineNumber - 1) * lineHeight - (gCodeLines.clientHeight / 2) + (lineHeight / 2))

  let start;
  let end;
  if (lineNumber <= 0) {
    start = 0;
    end = 1;
  } else {
    start = lineNumber <= 1 ? 0 : nthLineEnd(gCodeText, lineNumber - 1) + 1;
    end = gCodeText.indexOf("\n", start);
  }

  gCodeLines.select();
  gCodeLines.setSelectionRange(start, end);
}

function runGCode() {
  if (gCodeFilename) {
    const cmd = `$sd/run=${gCodeFilename}`;
    sendCommand(cmd);
    // Immediately disable the Start button to prevent double-clicks or stale Idle
    // status reports from re-enabling it before the firmware transitions to Run state.
    gCodeLoaded = false;
    setRunControls();
    setTimeout(() => { SendRealtimeCmd(0x7e); }, 1500);
  }
}

function tabletLoadGCodeFile(path, size) {
  gCodeFilename = path
  if ((Number.isNaN(size) && size.endsWith('GB')) || size > 10000000) {
    showGCode('GCode file too large to display (> 1MB)');
    gCodeDisplayable = false;
    tpDisplayer().clear();
  } else {
    gCodeDisplayable = true;
    
    // Log loading start
    Monitor_output_Update(`[Preview] Loading GCode file: ${path}\n`);
    
    // Disable ping monitoring during GCode loading and preview rendering
    disablePingForUpload();
    
    // Use sequential loading for files larger than 10KB for better user experience
    if (size > 10000) {
      tabletLoadGCodeFileSequentially(path);
    } else {
      fetch(encodeURIComponent(`SD${gCodeFilename}`))
        .then((response) => response.text())
        .then((gcode) => {
          showGCode(gcode);
          // Save GCode state after successful load
          saveGCodeState();
          // Restore ping monitoring after preview completes
          restorePingAfterUpload();
          Monitor_output_Update("[Preview] GCode preview loaded successfully\n");
        })
        .catch((error) => {
          // Restore ping monitoring on error
          restorePingAfterUpload();
          Monitor_output_Update(`[Preview] Failed to load GCode: ${error.message}\n`);
          console.error('Error loading GCode file:', error);
        });
    }
  }
}

async function tabletLoadGCodeFileSequentially(path) {
  try {
    // Clear existing content and show loading message
    showGCode("Loading GCode file...", false, false);
    
    const response = await fetch(encodeURIComponent(`SD${path}`));
    if (!response.ok) {
      throw new Error(`HTTP error! status: ${response.status}`);
    }
    
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let buffer = "";
    let lineCount = 0;
    let chunkCount = 0;
    let isFirstChunk = true;
    const TOOLPATH_UPDATE_INTERVAL = 10; // Update toolpath every 10 chunks for progressive display
    
    while (true) {
      const { done, value } = await reader.read();
      
      if (done) {
        // Process any remaining content in buffer
        if (buffer.trim()) {
          if (isFirstChunk) {
            showGCode(buffer, false, true);
            isFirstChunk = false;
          } else {
            showGCode(buffer, true, true);
          }
        }
        break;
      }
      
      // Decode the chunk and add to buffer
      const chunk = decoder.decode(value, { stream: true });
      buffer += chunk;
      
      // Process complete lines
      const lines = buffer.split('\n');
      // Keep the last incomplete line in buffer
      buffer = lines.pop() || "";
      
      if (lines.length > 0) {
        lineCount += lines.length;
        chunkCount++;
        const content = lines.join('\n') + '\n';
        
        // Determine if we should update toolpath for this chunk
        const shouldUpdateToolpath = (chunkCount % TOOLPATH_UPDATE_INTERVAL === 0);
        
        if (isFirstChunk) {
          // Replace loading message with first chunk and show initial toolpath
          showGCode(content, false, true);
          isFirstChunk = false;
        } else {
          // Append subsequent chunks with periodic toolpath updates
          showGCode(content, true, shouldUpdateToolpath);
        }
        
        // Process in chunks of approximately 1000 lines for better UX
        if (lineCount % 1000 === 0) {
          // Add a small delay to allow UI to update and prevent blocking
          await yieldToEventLoop();
        }
      }
    }
    
    // Final toolpath update to ensure everything is displayed
    if (gCodeDisplayable) {
      tpDisplayer().showToolpath(_gcodeRaw, gCodeModal, arrayToXYZ(WPOS));
      updateJobBoundsDisplay();
    }

    // Save GCode state after successful load
    saveGCodeState();
    // Restore ping monitoring after preview completes
    restorePingAfterUpload();
    Monitor_output_Update("[Preview] GCode preview loaded successfully\n");
    
  } catch (error) {
    console.error('Error loading GCode file:', error);
    showGCode(`Error loading GCode file: ${error.message}`);
    
    // Restore ping monitoring on error
    restorePingAfterUpload();
    Monitor_output_Update(`[Preview] Failed to load GCode: ${error.message}\n`);
  }
}

function selectFile() {
  tabletClick()
  const filelist = id("filelist");
  const index = Number(filelist.options[filelist.selectedIndex].value);
  if (index === -3) {
    // No files
    updateDeleteButtonState();
    return;
  }
  if (index === -2) {
    // Blank entry selected
    updateDeleteButtonState();
    return;
  }
  if (index === -4) {
    // Clear GCode from memory
    gCodeFilename = "";
    gCodeDisplayable = false;
    showGCode("");
    clearGCodeState();
    // Reset dropdown to the first option (legend)
    filelist.selectedIndex = 0;
    updateDeleteButtonState();
    addMessage("GCode cleared from memory");
    return;
  }
  if (index === -1) {
    // Go up
    gCodeFilename = "";
    clearGCodeState();
    files_go_levelup()
    updateDeleteButtonState();
    return
  }
  const file = files_file_list[index];
  const filename = file.name;
  if (file.isdir) {
    gCodeFilename = "";
    clearGCodeState();
    files_enter_dir(filename);
  } else {
    tabletLoadGCodeFile(`${files_currentPath()}${filename}`, file.size);
  }
  updateDeleteButtonState();
}
// function toggleDropdown() {
//   id("tablet-dropdown-menu").classList.toggle("show");
// }
// function hideMenu() {
//   toggleDropdown();
// }
// function menuFullscreen() {
//   toggleFullscreen();
//   hideMenu();
// }
// function menuReset() {
//   stopAndRecover();
//   hideMenu();
// }
// function menuUnlock() {
//   sendCommand("$X");
//   hideMenu();
// }
// function menuHomeAll() {
//   sendCommand("$H");
//   hideMenu();
// }
// function menuHomeA() {
//   sendCommand("$HA");
//   hideMenu();
// }
// function menuSpindleOff() {
//   sendCommand("M5");
//   hideMenu();
// }

function requestModes() {
  sendCommand("$G");
}

const cycleDistance = (up) => {
  //var sel = id('jog-distance');
  //var newIndex = sel.selectedIndex + (up ? 1 : -1);
  //if (newIndex >= 0 && newIndex < sel.length) {
  //    tabletClick();
  //    sel.selectedIndex = newIndex;
  //}
}

/** "Click" on the named button/element */
const clickon = (name) => {
  //    $('[data-route="workspace"] .btn').removeClass('active');
  const button = id(name);
  button.classList.add("active");
  button.dispatchEvent(new Event("click"));
}
let ctrlDown = false;
let oldIndex = null;
let newChild = null;

function shiftUp() {
  if (!newChild) {
    return;
  }
  removeJogDistance(newChild, oldIndex);
  newChild = null;
}
function altUp() {
  if (!newChild) {
    return;
  }
  removeJogDistance(newChild, oldIndex);
  newChild = null;
}

function shiftDown() {
  if (newChild) {
    return;
  }
  const sel = id('jog-distance');
  const distance = sel.value;
  oldIndex = sel.selectedIndex;
  newChild = addJogDistance(distance * 10);
}
function altDown() {
  if (newChild) {
    return;
  }
  const sel = id('jog-distance');
  const distance = sel.value;
  oldIndex = sel.selectedIndex;
  newChild = addJogDistance(distance / 10);
}

/** Reports whether a text input box has focus - see the next comment.
 * TODO: Currently this is always false. Maybe we should remove all usages of it
 */
var isInputFocused = false
function tabletIsActive() {
  const elem = id("tablettab");
  return !elem ? false : elem.style.display !== "none";
}

function handleKeyDown(event) {
  // When we are in a gCodeModal input field like the MDI text boxes
  // or the numeric entry boxes, disable keyboard jogging so those
  // keys can be used for text editing.
  if (!tabletIsActive()) {
    return;
  }
  
  // Check if an input or textarea element has focus
  const activeElement = document.activeElement;
  if (activeElement && (activeElement.tagName === 'INPUT' || activeElement.tagName === 'TEXTAREA')) {
    return;
  }
  
  if (isInputFocused) {
    return;
  }

  const dirKeyToBtnId = {
    'ArrowRight': 'jog-x-plus',
    'ArrowLeft': 'jog-x-minus',
    'ArrowUp': 'jog-y-plus',
    'ArrowDown': 'jog-y-minus',
    'PageUp': 'jog-z-plus',
    'PageDown': 'jog-z-minus',
  }
  if (event.key in dirKeyToBtnId) {
    clickon(dirKeyToBtnId[event.key]);
    event.preventDefault();
    return;
  }

  const mathKeyToDir = {
    '=': true,
    '+': true,
    '-': false,
  }
  if (event.key in mathKeyToDir) {
    cycleDistance(mathKeyToDir[event.key]);
    event.preventDefault();
    return;
  }

  switch (event.key) {
    case 'Escape':
    case 'Pause':
      //clickon('pauseBtn')
      break
    case 'Shift':
      shiftDown()
      break
    case 'Control':
      ctrlDown = true
      break
    case 'Alt':
      altDown()
      break
    default:
      console.warn(`Received an unmatched keydown event for ${event.key}`);
  }
}

function handleKeyUp(event) {
  if (!tabletIsActive()) {
    return;
  }
  if (isInputFocused) {
    return;
  }
  switch (event.key) {
    case 'Shift': shiftUp(); break;
    case 'Control': ctrlDown = false; break;
    case 'Alt': altUp(); break;
  }
}

function mdiEnterKey(event) {
  if (event.key === "Enter") {
    MDIcmd(event.target.value);
    event.target.blur();
  }
}

// The listener could be added to the tablettab element by setting tablettab's
// contentEditable property.  The problem is that it is too easy for tablettab
// to lose focus, in which case it does not receive keys.  The solution is to
// delegate the event to window and then have the handler check to see if the
// tablet is active.
window.addEventListener("keydown", handleKeyDown);
window.addEventListener("keyup", handleKeyUp);

function saveJogDists() {
  localStorage.setItem("disM", getText("disM"));
  localStorage.setItem("disZ", getText("disZ"));
}

function loadJogDists() {
  const disM = localStorage.getItem("disM");
  if (disM != null) {
    setText("disM", disM);
  }
  const disZ = localStorage.getItem("disZ");
  if (disZ != null) {
    setText("disZ", disZ);
  }
}

function fullscreenIfMobile() {
  if (/Mobi|Android/i.test(navigator.userAgent)) {
    toggleFullscreen();
  }
}

// setMessageHeight(), with these helper functions, adjusts the size of the message
// window to fill the height of the screen.  It would be nice if we could do that
// solely with CSS, but I did not find a way to do that.  Everything I tried either
// a) required setting a fixed message window height, or
// b) the message window would extend past the screen bottom when messages were added
function height(element) {
  return element?.getBoundingClientRect()?.height;
}
function heightId(eid) {
  return height(id(eid));
}
function bodyHeight() {
  return height(document.body);
}
function controlHeight() {
  return heightId('nav-panel') + heightId('axis-position') + heightId('setAxis');
}
function setBottomHeight() {
  if (!tabletIsActive()) {
    return;
  }
  const residue = bodyHeight() - heightId('navbar') - controlHeight();
  const tStyle = getComputedStyle(id('tablettab'));
  let tPad = Number.parseFloat(tStyle.paddingTop) + Number.parseFloat(tStyle.paddingBottom);
  tPad += 20;
}
window.onresize = setBottomHeight

const tabletDocumentClick = (event) => {
  const elemIdsToTest = ["calibration-popup", "calibrationBTN", "numPad"];
  const turnOffCalPopup = elemIdsToTest.every((elemId) => {
    const elem = document.getElementById(elemId);
    return !elem || !elem.contains(event.target);
  });
  if (turnOffCalPopup) {
    document.getElementById("calibration-popup").style.display = "none";
  }
};

document.addEventListener('click', tabletDocumentClick);

/* Calibration modal */

const openModal = (modalId) => {
  const modal = document.getElementById(modalId);

  if (modal) {
    modal.style.display = "flex";
  }
};

const hideModal = (modalId) => {
  const modal = document.getElementById(modalId);

  if (modal) {
    modal.style.display = "none";
  }
};

const onCalibrationButtonsClick = async (command, msg = "") => {
  if (msg) {
    addMessage(msg);
  }
  sendCommand(command);

  //Prints out the index.html version number when test is pressed
  if (command === '$TEST') {
    addMessage(`Index.html Version: ${versionNumber}`);
  }

  if (command !== '$MINFO') {
    scheduleCallback(() => { sendCommand('$MINFO'); }, 1000);
  }
}

/* Calibration modal END */

// File deletion functionality
function tabletDeleteGCodeFile() {
  const filelist = id("filelist");
  const selectedIndex = filelist.selectedIndex;
  
  if (selectedIndex <= 0 || !gCodeFilename) {
    return; // No file selected or invalid selection
  }
  
  const selectedOption = filelist.options[selectedIndex];
  const filename = selectedOption.text;
  
  // Show confirmation dialog (using the same pattern as SPIFFS dialog)
  confirmdlg(
    translate_text_item("Please Confirm"), 
    translate_text_item("Confirm deletion of file: ") + filename, 
    processTabletFileDelete
  );
}

function processTabletFileDelete(answer) {
  if (answer !== "yes") {
    return;
  }
  
  if (!gCodeFilename) {
    return;
  }
  
  // Disable the delete button immediately to prevent multiple clicks
  const deleteBtn = id("tablettab_gcode_delete");
  if (deleteBtn) {
    deleteBtn.style.opacity = "0.5";
    deleteBtn.style.pointerEvents = "none";
    deleteBtn.setAttribute("disabled", "true");
  }
  
  // Build the delete command using the same pattern as files.js
  const cmd = buildHttpFileCmd({ 
    action: "delete", 
    filename: gCodeFilename.split('/').pop() // Get just the filename without path
  });
  
  SendGetHttp(cmd, tabletFileDeleteSuccess, tabletFileDeleteFailed);
}

function tabletFileDeleteSuccess(response) {
  // Remember the deleted file name for logging
  const deletedFile = gCodeFilename;

  // Clear the selected file and reset dropdown
  gCodeFilename = "";
  showGCode(""); // Clear the GCode display

  // Clear the saved GCode state
  clearGCodeState();

  // Reset the dropdown to the first option immediately
  const filelist = id("filelist");
  if (filelist) {
    filelist.selectedIndex = 0;
  }
  
  addMessage("File deleted successfully: " + deletedFile.split('/').pop());
  
  // Wait a short moment for server-side cleanup, then refresh the file list
  setTimeout(() => {
    files_refreshFiles(files_currentPath());
  }, 500); // 500ms delay to ensure server-side delete completes
}

function tabletFileDeleteFailed(error_code, response) {
  // Re-enable the delete button
  const deleteBtn = id("tablettab_gcode_delete");
  if (deleteBtn && gCodeFilename) {
    deleteBtn.style.opacity = "1";
    deleteBtn.style.pointerEvents = "auto";
    deleteBtn.removeAttribute("disabled");
  }
  
  addMessage("Failed to delete file: " + (response || "Unknown error"));
}

function updateDeleteButtonState() {
  const filelist = id("filelist");
  const deleteBtn = id("tablettab_gcode_delete");
  const selectedIndex = filelist.selectedIndex;
  
  // Enable delete button only if a valid file is selected (not "Load File..." or directory)
  if (selectedIndex > 0 && gCodeFilename) {
    deleteBtn.style.opacity = "1";
    deleteBtn.style.pointerEvents = "auto";
    deleteBtn.removeAttribute("disabled");
  } else {
    deleteBtn.style.opacity = "0.5";
    deleteBtn.style.pointerEvents = "none";
    deleteBtn.setAttribute("disabled", "true");
  }
}
