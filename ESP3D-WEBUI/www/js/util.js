/**
 * Creates a task scheduler that works even when the browser tab is inactive.
 * Uses MessageChannel API which is not throttled in background tabs, unlike setTimeout.
 * Falls back to Promise.resolve() for immediate execution if MessageChannel is unavailable.
 * @returns {Function} - A function that schedules a callback to run asynchronously.
 */
function createBackgroundTaskScheduler() {
  // Try to use MessageChannel for immediate task scheduling (not throttled in background)
  if (typeof MessageChannel !== 'undefined') {
    const channel = new MessageChannel();
    const taskQueue = [];

    // Set up the message handler once
    channel.port1.onmessage = () => {
      if (taskQueue.length > 0) {
        const callback = taskQueue.shift();
        try {
          callback();
        } catch (error) {
          console.error('Error executing scheduled task:', error);
        }
      }
    };

    // Return a function that enqueues tasks and triggers execution
    return function(callback) {
      taskQueue.push(callback);
      channel.port2.postMessage(null);
    };
  }
  // Fallback to Promise.resolve() which also executes immediately
  return function(callback) {
    Promise.resolve().then(() => {
      try {
        callback();
      } catch (error) {
        console.error('Error executing scheduled task:', error);
      }
    });
  };
}

// Create the scheduler once at module load time
const scheduleTask = createBackgroundTaskScheduler();

/**
 * Yields control to the browser event loop without being throttled in background tabs.
 * This replaces setTimeout(..., 0) which is throttled to ~1 second in inactive tabs.
 * @returns {Promise} - A promise that resolves on the next event loop tick.
 */
function yieldToEventLoop() {
  return new Promise(resolve => scheduleTask(resolve));
}

/**
 * Schedules a callback to run after a minimum delay without being throttled in background tabs.
 * Uses requestAnimationFrame polling for delays to avoid browser throttling.
 * For delay = 0, uses the background-safe MessageChannel scheduler.
 * @param {Function} callback - The function to call
 * @param {number} delay - Minimum delay in milliseconds (0 for immediate)
 * @returns {Function|undefined} - Cancel function for delays > 0, undefined for immediate execution
 */
function scheduleCallback(callback, delay = 0) {
  if (typeof callback !== 'function') {
    console.error('scheduleCallback: callback must be a function');
    return undefined;
  }
  if (typeof delay !== 'number' || delay < 0) {
    console.error('scheduleCallback: delay must be a non-negative number');
    return undefined;
  }
  
  if (delay === 0) {
    scheduleTask(callback);
    return undefined;
  } else {
    // Use requestAnimationFrame polling to avoid setTimeout throttling in background tabs
    const startTime = performance.now();
    let rafId = null;
    
    const poll = () => {
      const now = performance.now();
      const elapsed = now - startTime;
      
      if (elapsed >= delay) {
        try {
          callback();
        } catch (error) {
          console.error('Error executing scheduled callback:', error);
        }
      } else {
        rafId = requestAnimationFrame(poll);
      }
    };
    
    rafId = requestAnimationFrame(poll);
    
    // Return a cancel function for cleanup if needed
    return () => {
      if (rafId !== null) {
        cancelAnimationFrame(rafId);
        rafId = null;
      }
    };
  }
}

/** Get the element identified with the supplied name */
const id = (name) => document.getElementById(name);

/** Returns an array of elements with the supplied class name, which can be use with forEach() or for ... of */
const elemsByClass = (name) => Array.from(document.getElementsByClassName(name));

/** Set an element's `value` value (if the element exists) */
const setValue = (name, val) => {
  const elem = id(name);
  if (elem) {
    elem.value = val;
  }
}

/** Return an element's `value` value, or its `innerText` value.
 * If the element does not exist or does not have a `value` or `innerText` value `undefined` is returned.
 * This does the opposite of getText, which checks the innerText value first. */
const getValue = (name) => id(name)?.value || id(name)?.innerText;

/** Gets an element's `value` value, or its `innerText` value.
 * Ensures that it is a string, and trims it.
 * If the element does not exist or does not have a `value` or `innerText` value an empty string is returned
 */
const getValueTrimmed = (name) => String(getValue(name) || "").trim();

/** Gets an element's `value` value, or its `innerText` value.
 * And then tries to treat it as a float.
 * 
 * Returns a 'NaN' if the element:
 * * does not exist,
 * * does not have a `value` or `innerText` value or
 * * can't be converted to a float
 */
const getValueFloat = (name) => Number.parseFloat(getValue(name) || "");

/** Gets an element's `value` value, or its `innerText` value.
 * And then tries to treat it as an integer.
 * 
 * Returns a 'NaN' if the element:
 * * does not exist,
 * * does not have a `value` or `innerText` value or
 * * can't be converted to an integer
 */
const getValueInt = (name) => Number.parseInt(getValue(name) || "");

/** Return an element's `innerText` value, or its `value` value.
 * If the element does not exist or does not have a `value` or `innerText` value `undefined` is returned.
 * This does the opposite of getValue, which checks the `value` value first. */
const getText = (name) => id(name)?.innerText || id(name)?.value;

/** Set the textContent of the element with an id matching the supplied name.
 * If the element cannot be found - nothing happens */
const setTextContent = (name, val) => {
  const elem = id(name);
  if (elem) {
    elem.textContent = val;
  }
}

/** Set an element's `innerHTML` value (if the element exists) */
const setHTML = (name, val) => {
  const elem = id(name);
  if (elem) {
    elem.innerHTML = val;
  }
}

/** Set the innerText of the element with an id matching the supplied name.
 * If the element cannot be found - nothing happens */
const setText = (name, val) => {
  const elem = id(name);
  if (elem) {
    elem.innerText = val;
  }
}

/** Set the display style of the element identified by name to the supplied value */
const setDisplay = (name, val) => {
  const elem = id(name);
  if (!elem) {
    return;
  }
  elem.style.display = val;
}

/** Set the display style of the element identified by name to 'none' */
const displayNone = (name) => setDisplay(name, 'none');

/** Set the display style of the element identified by name to 'block' */
const displayBlock = (name) => setDisplay(name, 'block');

const disable_items = (item, state) => {
  if (!item) {
    return;
  }
  const liste = item.getElementsByTagName('*');
  for (let i = 0; i < liste.length; i++) {
    liste[i].disabled = state;
  }
}

function displayFlex(name) {
  setDisplay(name, 'flex')
}
function displayTable(name) {
  setDisplay(name, 'table-row')
}
function displayInline(name) {
  setDisplay(name, 'inline')
}
function displayInitial(name) {
  setDisplay(name, 'initial')
}
function displayUndoNone(name) {
  setDisplay(name, '')
}

/** Set the disabled value for the elements matching the selector */
function setDisabled(selector, value) {
  for ((element) of document.querySelectorAll(selector)) {
    element.disabled = value;
  }
}

/** Set a checkbox element's default `value`, its `checked` field (if the element exists) */
const setCheckedDefault = (name, val, setBoth = true) => {
  const checkBox = id(name);
  if (checkBox) {
    checkBox.checked = String(val).toLowerCase() === "true";
    if (setBoth) {
      checkBox.value = String(val);
    }
  }
}

/** Set a checkbox element's `value` (if the element exists) */
const setChecked = (name, val) => {
  const checkBox = id(name);
  if (checkBox) {
    checkBox.value = String(val).toLowerCase();
  }
}

/** Return a checkbox element's `value`.
 * Note that this is a string.
 * If the element does not exist a "false" string is returned */
const getChecked = (name) => {
  const checkBox = id(name);
  return checkBox?.value || "false";
}

const sleep = (delay) => new Promise((resolve) => setTimeout(resolve, delay));

/** Build a 'standard' format error message */
const stdErrMsg = (error_code, response = "", error_prefix = "Error") => `${error_prefix} ${error_code} : ${response}`;
/** Use `console.error` to report an error
 * If the response message is falsey, and the error_prefix is the default, we assume that we've been supplied with a stdErrMsg
 * Otherwise, we build a stdErrMsg with what was supplied
 */
const conErr = (error_code, response, error_prefix = "Error") => {
  const errMsg = (!response && error_prefix === "Error")
      ? error_code
      : stdErrMsg(error_code, response || "", error_prefix);
  console.error(errMsg);
}

/** HTML encode the supplied string */
const HTMLEncode = (value) => {
  const valChars = [...value];
  const aRet = valChars.map((vc) => {
    let iC = vc.charCodeAt();
    if (iC < 65 || iC > 127 || (iC > 90 && iC < 97)) {
      if (iC === 65533) {
        iC = 176;
      }
      return `&#${iC};`;
    }
    return vc;
  });

  return aRet.join('');
}

/** Decode an HTML encoded string */
const HTMLDecode = (value) => {
  const tmpelement = document.createElement('div');
  tmpelement.innerHTML = value;
  value = tmpelement.textContent;
  tmpelement.textContent = '';
  return value;
}