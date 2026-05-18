// Motor Current Debugging functionality

let motorCurrentData = {
    TLC: 0,
    TRC: 0,
    BLC: 0,
    BRC: 0,
    lastUpdate: null
};

let debugTabVisible = false; // Track if debug tab has been shown
let selectedMotorInfo = null;
let motorInfoPollingTimer = null;

const MAX_CURRENT = 2200; // Maximum current value for gauge scaling in mA (at max ADC)
const MOTOR_INFO_REFRESH_DELAY_MS = 250;

/**
 * Convert ADC value (0-4095) to milliamps using the formula: I(mA) = 1000*((3.3*(ADC/4095))/1.5)
 * @param {number} adcValue - ADC value from 0 to 4095
 * @returns {number} Current in milliamps
 */
const convertAdcToMilliamps = (adcValue) => {
    return 1000 * ((3.3 * (adcValue / 4095)) / 1.5);
};

/**
 * Show the debug tab by removing the hide_it class
 */
const showDebugTab = () => {
    if (!debugTabVisible) {
        const debugTabLink = id('debuggingtablink');
        if (debugTabLink) {
            debugTabLink.classList.remove('hide_it');
            debugTabVisible = true;
        }
    }
};

const selectedMotorCode = () => {
    const select = id('motor-test-select');
    return select ? select.value : 'TL';
};

const requestSelectedMotorInfo = () => {
    if (typeof sendCommand !== 'function') {
        return;
    }
    const debugTab = id('debuggingtab');
    if (debugTab && debugTab.style.display === 'none') {
        return;
    }
    sendCommand(`$MOTORINFO=${selectedMotorCode()}`);
};

const manualMotorMove = (direction) => {
    if (typeof sendCommand !== 'function') {
        return;
    }
    const moveDirection = direction === 'in' ? 'IN' : 'OUT';
    sendCommand(`$MOTORTEST=${selectedMotorCode()},${moveDirection},200`);
    setTimeout(requestSelectedMotorInfo, MOTOR_INFO_REFRESH_DELAY_MS);
};

const manualMotorMoveIn = () => manualMotorMove('in');
const manualMotorMoveOut = () => manualMotorMove('out');

const updateMotorTestDisplay = () => {
    const infoElement = id('motor-test-info');
    if (!infoElement) {
        return;
    }

    if (!selectedMotorInfo) {
        infoElement.textContent = 'No motor info received yet.';
        return;
    }

    const infoLines = [
        `Motor: ${selectedMotorInfo.label || selectedMotorInfo.motor || '-'}`,
        `Position (mm): ${selectedMotorInfo.position}`,
        `Target (mm): ${selectedMotorInfo.target}`,
        `Position Error (mm): ${selectedMotorInfo.positionError}`,
        `Raw Encoder Angle: ${selectedMotorInfo.rawEncoderAngle}`,
        `Instant Current (ADC): ${selectedMotorInfo.current}`,
        `Average Current (ADC): ${selectedMotorInfo.averageCurrent}`,
        `Power (PWM): ${selectedMotorInfo.power}`,
        `Speed (mm/s): ${selectedMotorInfo.speed}`,
        `Extended: ${selectedMotorInfo.extended}`,
        `Axis Homed: ${selectedMotorInfo.axisHomed}`,
        `All Homed: ${selectedMotorInfo.allHomed}`,
        `Calibration State: ${selectedMotorInfo.calibrationState}`,
        `System State: ${selectedMotorInfo.systemState}`,
        `Calibration In Progress: ${selectedMotorInfo.calibrationInProgress}`,
        `Any Override Active: ${selectedMotorInfo.overrideActive}`,
    ];
    infoElement.textContent = infoLines.join('\n');
};

const handleMotorInfoMessage = (motorInfo) => {
    selectedMotorInfo = motorInfo;
    updateMotorTestDisplay();
};

/**
 * Parse motor current message in format: [MSG:INFO: TLC: 0.000 TRC: 0.000 BLC: 0.000 BRC: 0.000]
 * @param {string} message - The motor current message to parse
 */
const parseMotorCurrentMessage = (message) => {
    const motorCurrentRegex = /\[MSG:INFO:\s*TLC:\s*([\d.]+)\s*TRC:\s*([\d.]+)\s*BLC:\s*([\d.]+)\s*BRC:\s*([\d.]+)\]/;
    const match = message.match(motorCurrentRegex);
    
    if (match) {
        // Show debug tab when motor current messages are received
        showDebugTab();
        
        // Convert ADC values to milliamps
        motorCurrentData.TLC = convertAdcToMilliamps(Number.parseFloat(match[1]));
        motorCurrentData.TRC = convertAdcToMilliamps(Number.parseFloat(match[2]));
        motorCurrentData.BLC = convertAdcToMilliamps(Number.parseFloat(match[3]));
        motorCurrentData.BRC = convertAdcToMilliamps(Number.parseFloat(match[4]));
        motorCurrentData.lastUpdate = new Date();
        
        updateMotorCurrentDisplay();
        return true;
    }
    return false;
};

/**
 * Update the gauge display with current motor current values
 */
const updateMotorCurrentDisplay = () => {
    const motors = ['TLC', 'TRC', 'BLC', 'BRC'];
    
    motors.forEach(motor => {
        const value = motorCurrentData[motor];
        const percentage = Math.min((value / MAX_CURRENT) * 100, 100);
        const circumference = 2 * Math.PI * 60; // radius = 60
        const offset = circumference - (percentage / 100) * circumference;
        
        // Update gauge progress
        const gaugeElement = id(`${motor.toLowerCase()}-gauge-progress`);
        if (gaugeElement) {
            gaugeElement.style.strokeDashoffset = offset;
            
            // Change color based on current level
            let color = '#4CAF50'; // Green for low current
            if (percentage > 75) {
                color = '#F44336'; // Red for high current
            } else if (percentage > 50) {
                color = '#FF9800'; // Orange for medium current
            } else if (percentage > 25) {
                color = '#2196F3'; // Blue for low-medium current
            }
            gaugeElement.style.stroke = color;
        }
        
        // Update gauge value text
        const valueElement = id(`${motor.toLowerCase()}-value`);
        if (valueElement) {
            valueElement.textContent = Math.round(value);
        }
    });
    
    // Update last update timestamp
    const lastUpdateElement = id('last-update');
    if (lastUpdateElement && motorCurrentData.lastUpdate) {
        lastUpdateElement.textContent = motorCurrentData.lastUpdate.toLocaleTimeString();
    }
};

/**
 * Initialize the debugging tab functionality
 */
const initDebuggingTab = () => {
    showDebugTab();
    // Set initial gauge states
    updateMotorCurrentDisplay();
    updateMotorTestDisplay();
    
    // Add event listener for tab activation to refresh display
    const debugTab = id('debuggingtab');
    if (debugTab) {
        debugTab.addEventListener('activate', () => {
            // Refresh display when tab becomes active
            updateMotorCurrentDisplay();
            updateMotorTestDisplay();
        });
    }

    if (!motorInfoPollingTimer) {
        motorInfoPollingTimer = setInterval(requestSelectedMotorInfo, 1000);
    }
};

/**
 * Reset motor current data (useful for testing or connection reset)
 */
const resetMotorCurrentData = () => {
    motorCurrentData = {
        TLC: 0,
        TRC: 0,
        BLC: 0,
        BRC: 0,
        lastUpdate: null
    };
    updateMotorCurrentDisplay();
    
    const lastUpdateElement = id('last-update');
    if (lastUpdateElement) {
        lastUpdateElement.textContent = translate_text_item('No data received');
    }
};

// Export functions for use in other modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        parseMotorCurrentMessage,
        updateMotorCurrentDisplay,
        initDebuggingTab,
        resetMotorCurrentData,
        convertAdcToMilliamps,
        motorCurrentData
    };
}

globalThis.manualMotorMoveIn = manualMotorMoveIn;
globalThis.manualMotorMoveOut = manualMotorMoveOut;
globalThis.requestSelectedMotorInfo = requestSelectedMotorInfo;
globalThis.handleMotorInfoMessage = handleMotorInfoMessage;

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initDebuggingTab);
} else {
    initDebuggingTab();
}

window.addEventListener('beforeunload', () => {
    if (motorInfoPollingTimer) {
        clearInterval(motorInfoPollingTimer);
        motorInfoPollingTimer = null;
    }
});
