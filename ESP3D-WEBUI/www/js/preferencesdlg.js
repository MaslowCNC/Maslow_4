//Preferences dialog
var language_save = language;
var config_filename_save = "";

var preferences_file_name = 'preferences.json';

function initpreferences() {
    displayNone('DHT_pref_panel');
    displayBlock('grbl_pref_panel');
    displayTable('has_tft_sd');
    displayTable('has_tft_usb');

    id("preferencesdlg.html").addEventListener("click", clear_drop_menu);
    id("preferencesDlgClose").addEventListener("click", closePreferencesDialog);
    id("preferencesDlgCancel").addEventListener("click", closePreferencesDialog);
    id("preferencesDlgSave").addEventListener("click", savingPreferences);

    const checkBoxes = Array.from(document.getElementsByTagName("input")).filter((inpElem) => inpElem.type === "checkbox" && inpElem.disabled !== true);
    for (const checkBox of checkBoxes) {
        const chkId = checkBox.id;
        if (chkId in checkBlocks) {
            id(chkId).addEventListener("click", toggleCheckBlock);
        } else {
            id(chkId).addEventListener("click", toggleCheckBox);
        }
    }
}

const savingPreferences = (dispatchEvent) => {SavePreferences(false)};

function ontogglePing(forcevalue) {
    if (typeof forcevalue !== 'undefined') enable_ping = forcevalue
    else enable_ping = !enable_ping
    if (enable_ping) {
        if (interval_ping !== -1) clearInterval(interval_ping)
        last_ping = Date.now()
        interval_ping = setInterval(() => { check_ping() }, 10 * 1000)
        console.log('enable ping')
    } else {
        if (interval_ping !== -1) clearInterval(interval_ping)
        console.log('disable ping')
    }
}

/** Apply the preferences we have to the dialog */
function applypreferenceslist() {
    // Ensure preferences list is loaded before applying
    if (!preferenceslist || !preferenceslist[0]) {
        console.warn("Preferences list not loaded yet, using defaults");
        preferenceslist = default_preferenceslist;
    }
    
    //Assign each control state
    translate_text(preferenceslist[0].language);
    build_HTML_setting_list(current_setting_filter);
    if (id('camtab')) {
        let camoutput = false;
        if (typeof (preferenceslist[0].enable_camera) !== 'undefined') {
            if (preferenceslist[0].enable_camera === 'true') {
                displayBlock('camtablink');
                camera_GetAddress();
                if (typeof (preferenceslist[0].auto_load_camera) !== 'undefined') {
                    if (preferenceslist[0].auto_load_camera === 'true') {
                        // const saddress = getValue('camera_webaddress');
                        camera_loadframe();
                        camoutput = true;
                    }
                }
            } else {
                id("tablettablink").click();
                displayNone('camtablink');
            }
        } else {
            id("tablettablink").click();
            displayNone('camtablink');
        }
        if (!camoutput) {
            id('camera_frame').src = "";
            displayNone('camera_frame_display');
            displayNone('camera_detach_button');
        }
    }
    if (preferenceslist[0].enable_grbl_probe_panel === 'true') {
        displayBlock('grblprobetablink');
    } else {
        id("grblcontroltablink").click();
        displayNone('grblprobetablink');
    }

    if (preferenceslist[0].enable_DHT === 'true') {
        displayBlock('DHT_humidity');
        displayBlock('DHT_temperature');
    } else {
        displayNone('DHT_humidity');
        displayNone('DHT_temperature');
    }
    if (preferenceslist[0].enable_lock_UI === 'true') {
        displayBlock('lock_ui_btn');
        ontoggleLock(true);
    } else {
        displayNone('lock_ui_btn');
        ontoggleLock(false);
    }
    if (preferenceslist[0].enable_ping === 'true') {
        ontogglePing(true);
    } else {
        ontogglePing(false);
    }

    if (preferenceslist[0].enable_grbl_panel === 'true') displayFlex('grblPanel');
    else {
        displayNone('grblPanel');
        reportNone(false);
    }

    if (preferenceslist[0].enable_control_panel === 'true') displayFlex('controlPanel');
    else {
        displayNone('controlPanel');
        on_autocheck_position(false);
    }
    setCheckedDefault("monitor_enable_verbose_mode", preferenceslist[0]?.enable_verbose_mode);
    if (preferenceslist[0].enable_verbose_mode === 'true') {
        Monitor_check_verbose_mode();
    }

    if (preferenceslist[0].enable_files_panel === 'true') displayFlex('filesPanel');
    else displayNone('filesPanel');

    if (preferenceslist[0].has_TFT_SD === 'true') {
        displayFlex('files_refresh_tft_sd_btn');
    } else {
        displayNone('files_refresh_tft_sd_btn');
    }

    if (preferenceslist[0].has_TFT_USB === 'true') {
        displayFlex('files_refresh_tft_usb_btn');
    } else {
        displayNone('files_refresh_tft_usb_btn');
    }

    if ((preferenceslist[0].has_TFT_SD === 'true') || (preferenceslist[0].has_TFT_USB === 'true')) {
        displayFlex('files_refresh_printer_sd_btn');
        displayNone('files_refresh_btn');
    } else {
        displayNone('files_refresh_printer_sd_btn');
        displayFlex('files_refresh_btn');
    }

    if (preferenceslist[0].enable_commands_panel === 'true') {
        displayFlex('commandsPanel');
        setCheckedDefault("monitor_enable_autoscroll", preferenceslist[0]?.enable_autoscroll);
        if (preferenceslist[0].enable_autoscroll === 'true') {
            Monitor_check_autoscroll();
        }
    } else displayNone('commandsPanel');

    const autoReportValue = Number.parseInt(preferenceslist[0].autoreport_interval);
    const autoReportChanged = getValue("preferences_autoReport_Interval") !== autoReportValue;
    if (autoReportChanged) {
        setValue("preferences_autoReport_Interval", autoReportValue);
    }
    const statusIntervalValue = Number.parseInt(preferenceslist[0].interval_status);
    statusIntervalChanged = getValue('preferences_status_Interval_check') !== statusIntervalValue;
    if (statusIntervalChanged) {
        setValue('preferences_status_Interval_check', statusIntervalValue);
    }
    if (autoReportChanged || statusIntervalChanged) {
        onAutoReportIntervalChange();
    }

    setValue('preferences_pos_Interval_check', Number.parseInt(preferenceslist[0].interval_positions));
    setValue('preferences_control_xy_velocity', Number.parseFloat(preferenceslist[0].xy_feedrate));
    setValue('preferences_control_z_velocity', Number.parseFloat(preferenceslist[0].z_feedrate));

    if (grblaxis > 2) axis_Z_feedrate = Number.parseFloat(preferenceslist[0].z_feedrate);
    if (grblaxis > 3) axis_A_feedrate = Number.parseFloat(preferenceslist[0].a_feedrate);
    if (grblaxis > 4) axis_B_feedrate = Number.parseFloat(preferenceslist[0].b_feedrate);
    if (grblaxis > 5) axis_C_feedrate = Number.parseFloat(preferenceslist[0].c_feedrate);

    if (grblaxis > 3) {
        const letter = id('control_select_axis').value;
        switch (letter) {
            case "Z":
                id('preferences_control_z_velocity').value = axis_Z_feedrate;
                break;
            case "A":
                id('preferences_control_a_velocity').value = axis_A_feedrate;
                break;
            case "B":
                id('preferences_control_b_velocity').value = axis_B_feedrate;
                break;
            case "C":
                id('preferences_control_c_velocity').value = axis_C_feedrate;
                break;
        }
    }

    id('preferences_probemaxtravel').value = Number.parseFloat(preferenceslist[0].probemaxtravel);
    id('preferences_probefeedrate').value = Number.parseFloat(preferenceslist[0].probefeedrate);
    id('preferences_proberetract').value = Number.parseFloat(preferenceslist[0].proberetract);
    id('preferences_probetouchplatethickness').value = Number.parseFloat(preferenceslist[0].probetouchplatethickness);
    build_file_filter_list(preferenceslist[0].f_filters);
}

function showpreferencesdlg() {
    const modal = setactiveModal('preferencesdlg.html');
    if (modal == null) return;
    language_save = language;
    config_filename_save = GetPrefOrDefault("config_filename");
    build_dlg_preferences_list();
    displayNone('preferencesdlg_upload_msg');
    showModal();
}

const GetPrefOrDefault = (prefName) => {
    if (!prefName || typeof prefName !== "string") {
        console.error("GetPrefOrDefault called without a prefName");
        return;
    }
    if (!(prefName in default_preferenceslist[0]) && !(prefName in preferenceslist[0])) {
        console.error(`GetPrefOrDefault called with an invalid prefName '${prefName}'`);
        return;
    }
    if (typeof (preferenceslist[0][prefName]) !== 'undefined') {
        return preferenceslist[0][prefName];
    }
    return default_preferenceslist[0][prefName];
}

/** use preferenceslist to set dlg status */
function build_dlg_preferences_list() {
    let content = "<table><tr><td>";
    content += `${get_icon_svg("flag")}&nbsp;</td><td>`;
    content += build_language_list("language_preferences");
    content += "</td></tr></table>";
    setHTML("preferences_langage_list", content);

    setCheckboxes();

    //camera address
    setValue('preferences_camera_webaddress', (typeof (preferenceslist[0].camera_address) !== 'undefined') ? HTMLDecode(preferenceslist[0].camera_address) : "");

    //autoreport interval
    id('preferences_autoReport_Interval').value = Number.parseInt(GetPrefOrDefault("autoreport_interval"));
    //interval positions
    id('preferences_pos_Interval_check').value = Number.parseInt(GetPrefOrDefault("interval_positions"));
    //interval status
    id('preferences_status_Interval_check').value = Number.parseInt(GetPrefOrDefault("interval_status"));
    //xy feedrate
    id('preferences_control_xy_velocity').value = Number.parseFloat(GetPrefOrDefault("xy_feedrate"));
    if (grblaxis > 2) {
        //z feedrate
        id('preferences_control_z_velocity').value = Number.parseFloat(GetPrefOrDefault("z_feedrate"));
    }
    if (grblaxis > 3) {
        //a feedrate
        id('preferences_control_a_velocity').value = Number.parseFloat(GetPrefOrDefault("a_feedrate"));
    }
    if (grblaxis > 4) {
        //b feedrate
        id('preferences_control_b_velocity').value = Number.parseFloat(GetPrefOrDefault("b_feedrate"));
    }
    if (grblaxis > 5) {
        //c feedrate
        id('preferences_control_c_velocity').value = Number.parseFloat(GetPrefOrDefault("c_feedrate"));
    }

    //probemaxtravel
    id('preferences_probemaxtravel').value = Number.parseFloat(GetPrefOrDefault("probemaxtravel"));
    //probefeedrate
    id('preferences_probefeedrate').value = Number.parseFloat(GetPrefOrDefault("probefeedrate"));
    //proberetract
    id('preferences_proberetract').value = Number.parseFloat(GetPrefOrDefault("proberetract"));
    //probetouchplatethickness
    id('preferences_probetouchplatethickness').value = Number.parseFloat(GetPrefOrDefault("probetouchplatethickness"));

    //file filters
    if (typeof (preferenceslist[0].f_filters) !== 'undefined') {
        console.log("Use prefs filters");
        id('preferences_filters').value = preferenceslist[0].f_filters;
    } else {
        console.log("Use default filters");
        id('preferences_filters').value = String(default_preferenceslist[0].f_filters);
    }

    //config filename - fetch yaml files and populate dropdown
    fetchYamlFiles();

    //update stream
    id('preferences_update_stream').value = GetPrefOrDefault("update_stream");
}

/** Fetch YAML files from filesystem and populate the config filename dropdown */
function fetchYamlFiles() {
    const cmd = buildHttpFilesCmd({ action: 'list', path: '/' });
    SendGetHttp(cmd, populateConfigFilenameDropdown, handleYamlFilesFetchError);
}

/** Populate config filename dropdown with YAML files from the response */
function populateConfigFilenameDropdown(response) {
    try {
        const data = JSON.parse(response);
        const dropdown = id('preferences_config_filename');
        
        if (!dropdown) {
            console.error('Config filename dropdown not found');
            return;
        }

        // Clear existing options
        dropdown.innerHTML = '';

        // Filter for .yaml files
        const yamlFiles = data.files.filter(file => 
            file.name.toLowerCase().endsWith('.yaml') && file.size !== '-1'
        );

        // Add yaml files to dropdown
        yamlFiles.forEach(file => {
            const option = document.createElement('option');
            option.value = file.name;
            option.textContent = file.name;
            dropdown.appendChild(option);
        });

        // If no yaml files found, add default option
        if (yamlFiles.length === 0) {
            const option = document.createElement('option');
            option.value = 'maslow.yaml';
            option.textContent = 'maslow.yaml';
            dropdown.appendChild(option);
        }

        // Set the selected value from preferences
        const savedFilename = GetPrefOrDefault("config_filename");
        if (savedFilename) {
            dropdown.value = savedFilename;
        }
    } catch (error) {
        console.error('Error parsing yaml files response:', error);
        handleYamlFilesFetchError(0, response);
    }
}

/** Handle error when fetching YAML files */
function handleYamlFilesFetchError(errorCode, response) {
    console.warn('Failed to fetch yaml files, using default');
    const dropdown = id('preferences_config_filename');
    if (dropdown) {
        // Set default option if fetch failed
        dropdown.innerHTML = '<option value="maslow.yaml">maslow.yaml</option>';
        const savedFilename = GetPrefOrDefault("config_filename");
        if (savedFilename && savedFilename !== 'maslow.yaml') {
            const option = document.createElement('option');
            option.value = savedFilename;
            option.textContent = savedFilename;
            dropdown.appendChild(option);
            dropdown.value = savedFilename;
        }
    }
}

function closePreferencesDialog() {
    const modified = PreferencesModified() || language_save !== language;

    if (modified) {
        confirmdlg(translate_text_item("Data mofified"), translate_text_item("Do you want to save?"), process_preferencesCloseDialog)
    } else {
        closeModal('cancel');
    }
}

function process_preferencesCloseDialog(answer) {
    if (answer == 'no') {
        //console.log("Answer is no so exit");
        translate_text(language_save);
        closeModal('cancel');
    } else {
        // console.log("Answer is yes so let's save");
        SavePreferences(false);
    }
}

const getPreferencesForSave = () => {
    let newPrefsList = [];
    if (!Checkvalues("preferences_autoReport_Interval") ||
        !Checkvalues("preferences_pos_Interval_check") ||
        !Checkvalues("preferences_status_Interval_check") ||
        !Checkvalues("preferences_control_xy_velocity") ||
        !Checkvalues("preferences_filters") ||
        !Checkvalues("preferences_probemaxtravel") ||
        !Checkvalues("preferences_probefeedrate") ||
        !Checkvalues("preferences_proberetract") ||
        !Checkvalues("preferences_probetouchplatethickness")
    ) {
        return newPrefsList;
    }
    if (grblaxis > 2) {
        if (!Checkvalues("preferences_control_z_velocity")) {
            return newPrefsList;
        }
    }
    if ((grblaxis > 3) && (!Checkvalues("preferences_control_a_velocity"))) {
        return newPrefsList;
    }
    if ((grblaxis > 4) && (!Checkvalues("preferences_control_b_velocity"))) {
        return newPrefsList;
    }
    if ((grblaxis > 5) && (!Checkvalues("preferences_control_c_velocity"))) {
        return newPrefsList;
    }

    let saveprefs = [`[{"language":"${language}"`];
    saveprefs.push(`"enable_lock_UI":"${getChecked('enable_lock_UI')}"`);
    saveprefs.push(`"enable_ping":"${getChecked('enable_ping')}"`);
    saveprefs.push(`"enable_DHT":"${getChecked('enable_DHT')}"`);

    saveprefs.push(`"enable_camera":"${getChecked('show_camera_panel')}"`);
    saveprefs.push(`"auto_load_camera":"${getChecked('autoload_camera_panel')}"`);
    saveprefs.push(`"camera_address":"${HTMLEncode(getValue('preferences_camera_webaddress') || "")}"`);

    saveprefs.push(`"enable_control_panel":"${getChecked('show_control_panel')}"`);
    saveprefs.push(`"interval_positions":"${getValue('preferences_pos_Interval_check') || ""}"`);
    saveprefs.push(`"xy_feedrate":"${getValue('preferences_control_xy_velocity') || ""}"`);
    if (grblaxis > 2) {
        saveprefs.push(`"z_feedrate":"${getValue('preferences_control_z_velocity') || ""}"`);
    }
    if (grblaxis > 3) {
        saveprefs.push(`"a_feedrate":"${getValue('preferences_control_a_velocity') || ""}"`);
    }
    if (grblaxis > 4) {
        saveprefs.push(`"b_feedrate":"${getValue('preferences_control_b_velocity') || ""}"`);
    }
    if (grblaxis > 5) {
        saveprefs.push(`"c_feedrate":"${getValue('preferences_control_c_velocity') || ""}"`);
    }

    saveprefs.push(`"enable_grbl_panel":"${getChecked('show_grbl_panel')}"`);
    saveprefs.push(`"autoreport_interval":"${getValue('preferences_autoReport_Interval') || ""}"`);
    saveprefs.push(`"interval_status":"${getValue('preferences_status_Interval_check') || ""}"`);
    saveprefs.push(`"enable_grbl_probe_panel":"${getChecked('show_grbl_probe_tab')}"`);
    saveprefs.push(`"probemaxtravel":"${getValue('preferences_probemaxtravel') || ""}"`);
    saveprefs.push(`"probefeedrate":"${getValue('preferences_probefeedrate') || ""}"`);
    saveprefs.push(`"probetouchplatethickness":"${getValue('preferences_probetouchplatethickness') || ""}"`);
    saveprefs.push(`"proberetract":"${getValue('preferences_proberetract') || ""}"`);

    saveprefs.push(`"enable_files_panel":"${getChecked('show_files_panel')}"`);
    saveprefs.push(`"has_TFT_SD":"${getChecked('has_tft_sd')}"`);
    saveprefs.push(`"has_TFT_USB":"${getChecked('has_tft_usb')}"`);
    saveprefs.push(`"f_filters":"${getValue('preferences_filters') || ""}"`);
    saveprefs.push(`"config_filename":"${getValue('preferences_config_filename') || "maslow.yaml"}"`);
    saveprefs.push(`"update_stream":"${getValue('preferences_update_stream') || "release"}"`);

    saveprefs.push(`"enable_commands_panel":"${getChecked('show_commands_panel')}"`);
    saveprefs.push(`"enable_autoscroll":"${getChecked('preferences_autoscroll')}"`);
    saveprefs.push(`"enable_verbose_mode":"${getChecked('preferences_verbose_mode')}"}]`);
    try {
        newPrefsList = JSON.parse(saveprefs.join(","));
    } catch (error) {
        console.error("There was an error preparing the preferences before saving them. The preferences have not been saved. This is probably a programmer error.");
        console.error(error);
        return;
    }

    return newPrefsList;
}

function SavePreferences(useExternalSetPreference = false) {
    if (CheckForHttpCommLock()) {
        return;
    }

    console.log("Saving preferences");
    if (!useExternalSetPreference) {
        const newPrefsList = getPreferencesForSave();
        if (newPrefsList.length === 0) {
            return;
        }

        preferenceslist = newPrefsList;
    }

    const file = BuildFormDataFiles(preferences_file_name, [JSON.stringify(preferenceslist, null, " ")], { type: 'application/json' });
    var formData = new FormData();
    formData.append('path', '/');
    formData.append('myfile[]', file, preferences_file_name);

    if (useExternalSetPreference) {
        SendFileHttp(httpCmd.files, formData);
        console.info("Preferences successfully updated");
    } else {
        SendFileHttp(httpCmd.files, formData, preferencesdlgUploadProgressDisplay, preferencesUploadsuccess, preferencesUploadfailed);
    }
}

function preferencesdlgUploadProgressDisplay(oEvent) {
    if (oEvent.lengthComputable) {
        const percentComplete = (oEvent.loaded / oEvent.total) * 100;
        setValue('preferencesdlg_prg', percentComplete);
        setHTML('preferencesdlg_upload_percent', percentComplete.toFixed(0));
        displayBlock('preferencesdlg_upload_msg');
    } else {
        // Impossible because size is unknown
    }
}

function preferencesUploadsuccess(response) {
    console.info("Preferences successfully saved");
    displayNone('preferencesdlg_upload_msg');
    
    // Check if config filename changed
    const newConfigFilename = preferenceslist[0].config_filename;
    if (newConfigFilename && newConfigFilename !== config_filename_save) {
        console.log(`Config filename changed from '${config_filename_save}' to '${newConfigFilename}'`);
        // Show message that settings are being reloaded
        setHTML('preferencesdlg_upload_percent', '');
        const msgElem = id('preferencesdlg_upload_msg');
        if (msgElem) {
            // Get the "Saving" text element and replace it with "Switching configuration file..."
            const savingText = msgElem.querySelector('span[translate]');
            if (savingText) {
                savingText.textContent = 'Switching configuration file...';
            }
        }
        displayBlock('preferencesdlg_upload_msg');
        
        // Update FluidNC's Config/Filename setting and reload settings
        // Pass callback to close modal after settings are reloaded
        updateFluidNCConfigFilename(newConfigFilename, function() {
            // Settings have been reloaded, now close the modal
            displayNone('preferencesdlg_upload_msg');
            applypreferenceslist();
            init_grbl_panel();
            closeModal('ok');
        });
    } else {
        // No config change, proceed normally
        applypreferenceslist();
        init_grbl_panel();
        closeModal('ok');
    }
}

/** Update FluidNC's Config/Filename setting and reload configuration */
function updateFluidNCConfigFilename(newFilename, callback) {
    // Send ESP401 command to update Config/Filename setting in FluidNC
    // The P parameter is the setting position for Config/Filename
    // We need to find the Config/Filename setting in scl array
    const configFilenameSetting = scl.find(s => s.label === "Config/Filename");
    
    if (configFilenameSetting) {
        const cmd = buildHttpCommandCmd(httpCmdType.plain, `[ESP401]P=${configFilenameSetting.pos} T=${configFilenameSetting.type} V=${newFilename}`);
        console.log(`Updating FluidNC Config/Filename to: ${newFilename}`);
        SendGetHttp(cmd, 
            function(response) { handleConfigFilenameUpdateSuccess(response, callback); }, 
            function(error_code, response) { handleConfigFilenameUpdateFail(error_code, response, callback); }
        );
    } else {
        console.warn("Config/Filename setting not found in settings list");
        displayNone('preferencesdlg_upload_msg');
        alertdlg(translate_text_item("Error"), translate_text_item("Config/Filename setting not found. Cannot change config file."));
        // Close the modal
        applypreferenceslist();
        init_grbl_panel();
        closeModal('ok');
    }
}

function handleConfigFilenameUpdateSuccess(response, callback) {
    console.log("FluidNC Config/Filename updated successfully, saving settings to flash");
    
    // Send $SS command to save settings to flash so they persist after reset
    const saveCmd = buildHttpCommandCmd(httpCmdType.plain, "$SS");
    SendGetHttp(saveCmd, 
        function(saveResponse) {
            console.log("Settings saved to flash");
            // Config filename has been saved, but FluidNC needs to restart to load the new file
            // Show a message to the user with option to restart
            displayNone('preferencesdlg_upload_msg');
            
            const newFilename = preferenceslist[0].config_filename;
            confirmdlg(
                translate_text_item("Config File Changed"),
                `Configuration file changed to ${newFilename}. FluidNC must be restarted to load settings from the new file. Restart now?`,
                function(answer) {
                    if (answer) {
                        // User chose to restart
                        if (typeof restart_esp === 'function') {
                            restart_esp();
                        }
                    } else {
                        // User chose not to restart now
                        applypreferenceslist();
                        init_grbl_panel();
                        closeModal('ok');
                    }
                }
            );
        },
        function(error_code, response) {
            console.error(`Failed to save settings to flash: ${error_code}`, response);
            displayNone('preferencesdlg_upload_msg');
            alertdlg(translate_text_item("Error"), translate_text_item("Failed to save config filename setting"));
            // Still close the modal
            applypreferenceslist();
            init_grbl_panel();
            closeModal('ok');
        }
    );
}

function handleConfigFilenameUpdateFail(error_code, response, callback) {
    console.error(`Failed to update FluidNC Config/Filename: ${error_code}`, response);
    displayNone('preferencesdlg_upload_msg');
    alertdlg(translate_text_item("Error"), translate_text_item("Failed to update config filename setting in FluidNC"));
    // Close the modal
    applypreferenceslist();
    init_grbl_panel();
    closeModal('ok');
}

function preferencesUploadfailed(error_code, response) {
    alertdlg(translate_text_item("Error"), translate_text_item("Save preferences failed!"));
}

function Checkvalues(id_2_check) {
    let status = true;
    let value = 0;
    switch (id_2_check) {
        case "preferences_autoReport_Interval":
            value = Number.parseInt(id(id_2_check).value);
            if (!(!Number.isNaN(value) && (value === 0 || (value >= 50 && value <= 30000)))) {
                error_message = translate_text_item("Value of auto-report must be 0 or between 50ms and 30000ms !!");
                status = false;
            }
            break;
        case "preferences_status_Interval_check":
            value = Number.parseInt(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0 && value <= 100)) {
                error_message = translate_text_item("Value of auto-check must be between 0s and 99s !!");
                status = false;
            }
            break;
        case "preferences_pos_Interval_check":
            value = Number.parseInt(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 1 && value <= 100)) {
                error_message = translate_text_item("Value of auto-check must be between 0s and 99s !!");
                status = false;
            }
            break;
        case "preferences_control_xy_velocity":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0.00001)) {
                error_message = translate_text_item("XY Feedrate value must be at least 0.00001 mm/min!");
                status = false;
            }
            break;
        case "preferences_control_z_velocity":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0.00001)) {
                error_message = translate_text_item("Z Feedrate value must be at least 0.00001 mm/min!");
                status = false;
            }
            break;
        case "preferences_control_a_velocity":
        case "preferences_control_b_velocity":
        case "preferences_control_c_velocity":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0.00001)) {
                error_message = translate_text_item("Axis Feedrate value must be at least 0.00001 mm/min!");
                status = false;
            }
            break;
        case "preferences_probefeedrate":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0.00001 && value <= 9999)) {
                error_message = translate_text_item("Value of probe feedrate must be between 0.00001 mm/min and 9999 mm/min !");
                status = false;
            }
            break;
        case "preferences_probemaxtravel":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0.00001 && value <= 9999)) {
                error_message = translate_text_item("Value of maximum probe travel must be between 0.00001 mm and 9999 mm !");
                status = false;
            }
            break;
        case "preferences_proberetract":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0 && value <= 9999)) {
                error_message = translate_text_item("Value of probe retract must be between 0 mm and 9999 mm !");
                status = false;
            }
            break;
        case "preferences_probetouchplatethickness":
            value = Number.parseFloat(id(id_2_check).value);
            if (!(!Number.isNaN(value) && value >= 0 && value <= 9999)) {
                error_message = translate_text_item("Value of probe touch plate thickness must be between 0 mm and 9999 mm !");
                status = false;
            }
            break;
        case "preferences_filters":
            //TODO a regex would be better
            value = id(id_2_check).value;
            if ((value.indexOf(".") !== -1) ||
                (value.indexOf("*") !== -1)) {
                error_message = translate_text_item("Only alphanumeric chars separated by ; for extensions filters");
                status = false;
            }
            break;
    }
    if (status) {
        id(`${id_2_check}_group`).classList.remove("has-feedback");
        id(`${id_2_check}_group`).classList.remove("has-error");
        setHTML(`${id_2_check}_icon`, "");
    } else {
        // has-feedback hides the value so it is hard to fix it
        // id(id_2_check + "_group").classList.add("has-feedback");
        id(`${id_2_check}_group`).classList.add("has-error");
        // setHTML(id_2_check + "_icon", get_icon_svg("remove"));
        alertdlg(translate_text_item("Out of range"), error_message);
    }
    return status;
}
