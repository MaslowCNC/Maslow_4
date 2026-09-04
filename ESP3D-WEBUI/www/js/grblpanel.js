// import - id, opentab, SendPrinterCommand, grbl_reset, reportNone, tryAutoReport, reportPolled, onAutoReportIntervalChange, onstatusIntervalChange, onprobemaxtravelChange, onprobefeedrateChange, onproberetractChange, onprobetouchplatethicknessChange, SendRealtimeCmd, StartProbeProcess

const grblPanelClearStatus = () => {
  SendPrinterCommand("$X", true, showAlarmRecoveryWarning, null, 114, 1);
};
const grblPanelPause = () => SendRealtimeCmd(0x21);
const grblPanelResume = () => SendRealtimeCmd(0x7e);

const grblPanelF10Minus = () => SendRealtimeCmd(0x92);
const grblPanelF1Minus = () => SendRealtimeCmd(0x94);
const grblPanelF0 = () => SendRealtimeCmd(0x90);
const grblPanelF1Plus = () => SendRealtimeCmd(0x93);
const grblPanelF10Plus = () => SendRealtimeCmd(0x91);

const grblPanelS10Minus = () => SendRealtimeCmd(0x9b);
const grblPanelS1Minus = () => SendRealtimeCmd(0x9d);
const grblPanelS0 = () => SendRealtimeCmd(0x99);
const grblPanelS1Plus = () => SendRealtimeCmd(0x9c);
const grblPanelS10Plus = () => SendRealtimeCmd(0x9a);

const grblPanelSpindle = () => SendRealtimeCmd(0x9e);
const grblPanelFlood = () => SendRealtimeCmd(0xa0);
const grblPanelMist = () => SendRealtimeCmd(0xa1);

const grblPanelSpindleFwd = () => SendPrinterCommand(`M3 S${spindleTabSpindleSpeed}`, false, null, null, 1, 1);
const grblPanelSpindleRew = () => SendPrinterCommand(`M4 S${spindleTabSpindleSpeed}`, false, null, null, 1, 1);
const grblPanelSpindleOff = () => SendPrinterCommand("M5 S0", false, null, null, 1, 1);
const grblPanelSpindleRpm = (event) => setSpindleSpeed(event.value);

const grblPanelControlTabLink = (event) => opentab(event, "grblcontroltab", "grbluitabscontent", "grbluitablinks");
const grblPanelSpindleTabLink = (event) => opentab(event, "grblspindletab", "grbluitabscontent", "grbluitablinks");
const grblPanelProbeTabLink = (event) => opentab(event, "grblprobetab", "grbluitabscontent", "grbluitablinks");

/** Set up the event handlers for the grblpanel */
const grblpanel = () => {
    // GRBL reporting
    id("report_none").addEventListener("change", onReportType);
    id("report_auto").addEventListener("change", onReportType);
    id("grblpanel_autoreport_interval").addEventListener("change", onAutoReportIntervalChange);
    id("report_poll").addEventListener("change", onReportType);
    id("grblpanel_interval_status").addEventListener("change", onstatusIntervalChange);

    id("clear_status_btn").addEventListener("click", grblPanelClearStatus);
    // sd_pause_btn and sd_resume_btn event handlers are managed dynamically by updateUnifiedPlayPauseButton
    id("sd_reset_btn").addEventListener("click", grbl_reset);

    id("grblpanel_F10_minus").addEventListener("click", grblPanelF10Minus);
    id("grblpanel_F1_minus").addEventListener("click", grblPanelF1Minus);
    id("grblpanel_F0").addEventListener("click", grblPanelF0);
    id("grblpanel_F1_plus").addEventListener("click", grblPanelF1Plus);
    id("grblpanel_F10_plus").addEventListener("click", grblPanelF10Plus);

    id("grblpanel_S10_minus").addEventListener("click", grblPanelS10Minus);
    id("grblpanel_S1_minus").addEventListener("click", grblPanelS1Minus);
    id("grblpanel_S0").addEventListener("click", grblPanelS0);
    id("grblpanel_S1_plus").addEventListener("click", grblPanelS1Plus);
    id("grblpanel_S10_plus").addEventListener("click", grblPanelS10Plus);

    id("grblpanel_spindle").addEventListener("click", grblPanelSpindle);
    id("grblpanel_flood").addEventListener("click", grblPanelFlood);
    id("grblpanel_mist").addEventListener("click", grblPanelMist);

    id("grblspindle_fwd").addEventListener("click", grblPanelSpindleFwd);
    id("grblspindle_rew").addEventListener("click", grblPanelSpindleRew);
    id("grblspindle_off").addEventListener("click", grblPanelSpindleOff);
    id("grblspindle_rpm").addEventListener("change", grblPanelSpindleRpm);
    id("grblspindle_rpm").addEventListener("keyup", grblPanelSpindleRpm);

    id("grblpanel_probemaxtravel").addEventListener("change", onprobemaxtravelChange);
    id("grblpanel_probefeedrate").addEventListener("change", onprobefeedrateChange);
    id("grblpanel_proberetract").addEventListener("change", onproberetractChange);
    id("grblpanel_probetouchplatethickness").addEventListener("change", onprobetouchplatethicknessChange);

    id("probingbtn").addEventListener("click", StartProbeProcess);

    id("grblcontroltablink").addEventListener("click", grblPanelControlTabLink);
    id("grblspindletablink").addEventListener("click", grblPanelSpindleTabLink);
    id("grblprobetablink").addEventListener("click", grblPanelProbeTabLink);

    id("global_reset_btn").addEventListener("click", grbl_reset);
};

const onReportType = (e) => {
    switch (e.value) {
        case "none": reportNone(); break;
        case "auto": tryAutoReport(); break;
        case "poll": reportPolled(); break;
    }
};
