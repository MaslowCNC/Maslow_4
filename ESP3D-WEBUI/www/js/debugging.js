// Motor Current Debugging functionality

let motorCurrentData = {
    TLC: 0,
    TRC: 0,
    BLC: 0,
    BRC: 0,
    lastUpdate: null
};

let debugTabVisible = false; // Track if debug tab has been shown

const MAX_CURRENT = 2200; // Maximum current value for gauge scaling in mA (at max ADC)

/**
 * Convert ADC value (0-4095) to milliamps using the formula: I(mA) = 1000*((3.3*(ADC/4095))/1.5)
 * @param {number} adcValue - ADC value from 0 to 4095
 * @returns {number} Current in milliamps
 */
const convertAdcToMilliamps = (adcValue) => {
    return 1000 * ((3.3 * (adcValue / 4095)) / 1.5);
};

/** Gauge colour for a current as a percentage of MAX_CURRENT (also used by the Maslow tab's dials). */
const currentGaugeColor = (percentage) => {
    if (percentage > 75) {
        return '#F44336'; // Red for high current
    }
    if (percentage > 50) {
        return '#FF9800'; // Orange for medium current
    }
    if (percentage > 25) {
        return '#2196F3'; // Blue for low-medium current
    }
    return '#4CAF50'; // Green for low current
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
            gaugeElement.style.stroke = currentGaugeColor(percentage);
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
    // Set initial gauge states
    updateMotorCurrentDisplay();
    
    // Add event listener for tab activation to refresh display
    const debugTab = id('debuggingtab');
    if (debugTab) {
        debugTab.addEventListener('activate', () => {
            // Refresh display when tab becomes active
            updateMotorCurrentDisplay();
        });
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