// import - disable_items, displayNone, getChecked, id, setChecked, setHTML, opentab, creditsdlg, cameratab, configtab, changepassworddlg, showpreferencesdlg, connectdlg, translate_text_item, DisconnectLogin, setupdlg, settingstab

const navBarMainTabLink = (event) => opentab(event, "maintab", "mainuitabscontent", "mainuitablinks");
const navBarCamTabLink = (event) => opentab(event, "cameratab", "mainuitabscontent", "mainuitablinks");
const navBarConfigTabLink = (event) => opentab(event, "configtab", "mainuitabscontent", "mainuitablinks");
const navBarSettingTabLink = (event) => opentab(event, "settingstab", "mainuitabscontent", "mainuitablinks");
const navBarTabletTabLink = (event) => opentab(event, "tablettab", "mainuitabscontent", "mainuitablinks");
const navBarDebuggingTabLink = (event) => opentab(event, "debuggingtab", "mainuitabscontent", "mainuitablinks");

const navBarLogout = () => confirmdlg(translate_text_item("Disconnection requested"), translate_text_item("Please confirm disconnection."), DisconnectLogin);

/** Set up the event handlers for the navbar */
const navbar = () => {
	id("toggleLockUI").addEventListener("click", ontoggleLock);

	id("showPreferencesDlg").addEventListener("click", showpreferencesdlg);
	id("showConnectDlg").addEventListener("click", () => connectdlg(false));
	id("showSetupDlg").addEventListener("click", setupdlg);
	id("showCreditsDlg").addEventListener("click", creditsdlg);

	// Note: for `maintab` see dashtab.html
	id("maintablink").addEventListener("click", navBarMainTabLink);
	id("camtablink").addEventListener("click", navBarCamTabLink);
	id("configtablink").addEventListener("click", navBarConfigTabLink);
	id("settingtablink").addEventListener("click", navBarSettingTabLink);
	id("tablettablink").addEventListener("click", navBarTabletTabLink);
	id("debuggingtablink").addEventListener("click", navBarDebuggingTabLink);

	id("password_menu").addEventListener("click", changepassworddlg);
	id("showLoginDlg").addEventListener("click", logindlg);
	id("logout_menu").addEventListener("click", navBarLogout);

	cameratab();
	configtab();
	// settingstab();
};

const enableItem = (itemName) => {
	const itemElem = id(itemName);
	if (!itemElem) {
		return;
	}

	itemElem.disabled = false;
};

const ontoggleLock = (forcevalue) => {
	if (typeof forcevalue !== "undefined") {
		setChecked("lock_UI", forcevalue);
	}

	const jogUIElem = id("JogUI");
	if (getChecked("lock_UI") !== "false") {
		setHTML("lock_UI_btn_txt", translate_text_item("Unlock interface"));
		disable_items(id("maintab"), true);
		disable_items(id("configtab"), true);
		enableItem("progress_btn");
		enableItem("clear_monitor_btn");
		enableItem("monitor_enable_verbose_mode");
		enableItem("monitor_enable_autoscroll");
		enableItem("settings_update_fw_btn");
		enableItem("settings_restart_btn");
		if (jogUIElem) {
			disable_items(jogUIElem, false);
			displayNone("JogUI");
		}
	} else {
		setHTML("lock_UI_btn_txt", translate_text_item("Lock interface"));
		disable_items(id("maintab"), false);
		disable_items(id("configtab"), false);
		enableItem("settings_update_fw_btn");
		enableItem("settings_restart_btn");
		if (jogUIElem) {
			jogUIElem.style.pointerEvents = "auto";
		}
	}
};
