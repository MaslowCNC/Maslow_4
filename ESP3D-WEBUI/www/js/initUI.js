/** Set the page's title */
const Set_page_title = (page_title = "") => {
	if (page_title) {
		esp_hostname = page_title;
	}
	document.title = esp_hostname;
}

const hideAxiscontrols = () => {
	displayNone(["JogBar", "HomeZ", "control_z_position_display", "control_zm_position_row", "z_velocity_display"]);
	displayBlock("CornerZ");
}

const showAxiscontrols = () => {
	displayNone("CornerZ");
	displayBlock(["JogBar", "HomeZ", "control_z_position_display"]);
	displayTable("control_zm_position_row");
	displayInline("z_velocity_display");
}

const update_UI_firmware_target = () => {
	initpreferences();
	setHTML("control_x_position_label", "X");
	setHTML("control_y_position_label", "Y");
	setHTML("control_z_position_label", "Z");
	showAxiscontrols();

	displayNone("configtablink");
	displayNone("auto_check_control");
	displayNone("progress_btn");
	displayNone("abort_btn");
	displayNone("motor_off_control");
	setHTML("tab_title_configuration", "<span translate>GRBL configuration</span>",);
	setHTML("tab_printer_configuration", "<span translate>GRBL</span>");

	const fif = id("files_input_file");
	if (fif) {
		fif.accept = ".g,.gc,.gco,.gcode,.nc,.txt,.G,.GC,.GCO,.GCODE,.NC,.TXT";
	}
	displayInitial("zero_xyz_btn");
	displayInitial("zero_x_btn");
	displayInitial("zero_y_btn");
	if (grblaxis > 2) {
		//displayInitial('control_z_position_display');
		setHTML("control_z_position_label", "Zw");
	} else {
		hideAxiscontrols();
		displayNone("z_feedrate_group");
	}
	if (grblaxis > 3) {
		id("zero_xyz_btn_txt").innerHTML += "A";
		grblzerocmd += " A0";
		build_axis_selection();
		displayBlock("a_feedrate_group");
		displayBlock("control_a_position_display");
		id("positions_labels2").style.display = "inline-grid";
	}
	if (grblaxis > 4) {
		id("zero_xyz_btn_txt").innerHTML += "B";
		grblzerocmd += " B0";
		displayBlock("b_feedrate_group");
		displayBlock("control_b_position_display");
	}
	if (grblaxis > 5) {
		id("zero_xyz_btn_txt").innerHTML += "C";
		grblzerocmd += " C0";
		displayBlock("c_feedrate_group");
		displayBlock("control_c_position_display");
	} else {
		displayNone("control_c_position_display");
	}
	displayFlex("grblPanel");
	grblpanel();
	// id('FW_github').href = 'https://github.com/bdring/FluidNC';
	displayBlock("settings_filters");
	setHTML("control_x_position_label", "Xw");
	setHTML("control_y_position_label", "Yw");

	EP_HOSTNAME = 'Hostname';
	EP_STA_SSID = 'Sta/SSID';
	EP_STA_PASSWORD = 'Sta/Password';
	EP_STA_IP_MODE = 'Sta/IPMode';
	EP_STA_IP_VALUE = 'Sta/IP';
	EP_STA_GW_VALUE = 'Sta/Gateway';
	EP_STA_MK_VALUE = 'Sta/Netmask';
	EP_WIFI_MODE = 'WiFi/Mode';
	EP_AP_SSID = 'AP/SSID';
	EP_AP_PASSWORD = 'AP/Password';
	EP_AP_IP_VALUE = 'AP/IP';
	// Swap AP Mode and STA Mode
	SETTINGS_AP_MODE = 2;
	SETTINGS_STA_MODE = 1;

	const fwName = "FluidNC";
	setHTML("fwName", fwName);
	//SD image or not
	setHTML(
		"showSDused",
		direct_sd
			? "<svg width='1.3em' height='1.2em' viewBox='0 0 1300 1200'><g transform='translate(50,1200) scale(1, -1)'><path  fill='#777777' d='M200 1100h700q124 0 212 -88t88 -212v-500q0 -124 -88 -212t-212 -88h-700q-124 0 -212 88t-88 212v500q0 124 88 212t212 88zM100 900v-700h900v700h-900zM500 700h-200v-100h200v-300h-300v100h200v100h-200v300h300v-100zM900 700v-300l-100 -100h-200v500h200z M700 700v-300h100v300h-100z' /></g></svg>"
			: "",
	);

	return fwName;
}

const total_boot_steps = 5;
var current_boot_steps = 0;

const display_boot_progress = () => {
	current_boot_steps++;
	setValue("load_prg", Math.round((current_boot_steps * 100) / total_boot_steps));
}

/** InitUI step1 - try to connect to the ESP32 */
const initUI = () => {
	console.log("Init UI - Step 1");
	
	// Fetch unified state data from firmware FIRST, immediately after connection
	fetchStateData();

	// Start up connect dialog, don't try and get the FW data
	connectdlg(false);

	display_boot_progress();
	//check FW
	update_UI_firmware_target();
	//set title using hostname
	Set_page_title();
	//update UI version
	setHTML("UI_VERSION", web_ui_version);
	//update FW version
	setHTML("FW_VERSION", fw_version);
	// Get the element with id="defaultOpen" and click on it
	id("tablettablink").click();

	if (id("grblcontroltablink")) {
		id("grblcontroltablink").click();
	}

	initUI_2();

	setTimeout(tryAutoReport, 500); //Not sure why this needs a delay but it seems like a hack
}

/** InitUI step2 - get settings from ESP3D and start processing them */
function initUI_2() {
	console.log("Init UI - Step 2 - Get Settings");
	display_boot_progress();
	//query settings but do not update list in case wizard is showed
	refreshSettings(true);
	initUI_3();
}

/** InitUI step3 - Initialise the control and GRBL panels, get the preferences */
function initUI_3() {
	console.log("Init UI - Step 3 - Initialise the control and GRBL panels, get the preferences");
	display_boot_progress();
	GetPreferencesList();
	// Yet another hack to wait for the preferences to be set
	setTimeout(() => {
		//init panels
		init_controls_panel();
		// This is in preferencesdlg.js
		applypreferenceslist();
		init_grbl_panel();
		initUI_4();
	}, 1500);
}

/** InitUI step4 - Initialise the command and files panels, determine if the setup wizard needs to be run */
function initUI_4() {
	console.log("Init UI - Step 4 - Initialise the command and files panels, determine if the setup wizard needs to be run");
	display_boot_progress();
	init_command_panel();
	init_files_panel(false);
	
	// Ensure connection dialog is properly closed
	if (typeof forceCloseConnectionDialog === "function") {
		forceCloseConnectionDialog();
	}
	
	//check if we need setup
	if (target_firmware === "???") {
		console.log("Launch Setup");
		display_boot_progress();
		closeModal("Connection successful");
		setupdlg();
	} else {
		//wizard is done UI can be updated
		setup_is_done = true;
		do_not_build_settings = false;
		display_boot_progress();
		build_HTML_setting_list(current_setting_filter);
		closeModal();
		show_main_UI();
	}
}

function show_main_UI() {
	displayUndoNone("main_ui");
}
