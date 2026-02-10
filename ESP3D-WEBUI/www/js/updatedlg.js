// import - conErr, stdErrMsg, id, displayBlock, displayNone, setValue, setHTML, closeModal, setactiveModal, showModal, alertdlg, confirmdlg, SendFileHttp, SendGetHttp, translate_text_item, Monitor_output_Update

let update_ongoing = false;
let current_update_filename = "";

const updateDlgCancel = () => closeUpdateDialog("cancel");
const updateDlgSelect = () => document.getElementById("fw_select").click();
const updateDlgFileMouseUp = () => document.getElementById("fw_select_files").click();

/** update dialog */
const updatedlg = () => {
	const modal = setactiveModal("updatedlg.html");
	if (modal == null) {
		return;
	}

	id("updateDlgCancel").addEventListener("click", updateDlgCancel);
	id("updateDlgClose").addEventListener("click", updateDlgCancel);

	id("fw_select").addEventListener("change", checkupdatefile);
	id("fw_select_files").addEventListener("click", updateDlgSelect);
	id("fw_file_name").addEventListener("mouseup", updateDlgFileMouseUp);
	id("uploadfw_button").addEventListener("click", UploadUpdatefile);

	setHTML("fw_file_name", translate_text_item("No file chosen"));
	displayNone("prgfw");
	displayNone("uploadfw_button");
	setHTML("updatemsg", "");
	setValue("fw_select", "");
	setHTML("fw_update_dlg_title", translate_text_item("ESP3D Update").replace("ESP3D", "FluidNC"));
	showModal();
};

function closeUpdateDialog(msg) {
	if (update_ongoing) {
		alertdlg(translate_text_item("Busy..."), translate_text_item("Update is ongoing, please wait and retry."));
		return;
	}
	closeModal(msg);
}

function checkupdatefile() {
	displayNone("updatemsg");
	const files = id("fw_select").files;
	switch (files.length) {
		case 0:
			displayNone("uploadfw_button");
			setHTML("fw_file_name", translate_text_item("No file chosen"));
			break;
		case 1:
			displayBlock("uploadfw_button");
			setHTML("fw_file_name", files[0].name);
			break;
		default:
			displayBlock("uploadfw_button");
			setHTML("fw_file_name", translate_text_item("$n files").replace("$n", files.length));
			break;
	}
}

function UpdateProgressDisplay(oEvent) {
	if (oEvent.lengthComputable) {
		const percentComplete = (oEvent.loaded / oEvent.total) * 100;
		setValue("prgfw", percentComplete);
		setHTML(
			"updatemsg",
			`${translate_text_item("Uploading")} ${current_update_filename} ${percentComplete.toFixed(0)}%`,
		);
		
		// Log progress to serial messages at 10% intervals
		const percent = Math.floor(percentComplete);
		if (percent % 10 === 0 && percent !== UpdateProgressDisplay.lastLoggedPercent) {
			UpdateProgressDisplay.lastLoggedPercent = percent;
			Monitor_output_Update(`[Firmware Upload] ${percent}% (${(oEvent.loaded / 1024 / 1024).toFixed(2)} MB / ${(oEvent.total / 1024 / 1024).toFixed(2)} MB)\n`);
		}
	} else {
		// Impossible because size is unknown
	}
}
UpdateProgressDisplay.lastLoggedPercent = -1;

function UploadUpdatefile() {
	confirmdlg(translate_text_item("Please confirm"), translate_text_item("Update Firmware ?"), StartUploadUpdatefile);
}

function StartUploadUpdatefile(response) {
	if (response !== "yes") {
		return;
	}
	if (CheckForHttpCommLock()) {
		return;
	}

	const files = id("fw_select").files;
	const fileList = [];
	for (const file of files) {
		fileList.push(file.name);
	}
	const formData = BuildFileUploadFormData("/", files);

	// Reset progress logging
	UpdateProgressDisplay.lastLoggedPercent = -1;
	
	// Log upload start
	const fileName = files.length === 1 ? files[0].name : fileList.join(", ");
	const fileSize = files.length === 1 ? (files[0].size / 1024 / 1024).toFixed(2) : "multiple files";
	Monitor_output_Update(`[Firmware Upload] Starting upload of ${fileName} (${fileSize} MB)\n`);

	// Disable ping monitoring during firmware upload
	disablePingForUpload();

	displayNone("fw_select_form");
	displayNone("uploadfw_button");
	displayBlock("updatemsg");
	displayBlock("prgfw");
	update_ongoing = true;

	current_update_filename = files.length === 1 ? files[0].name : "";
	setHTML("updatemsg", `${translate_text_item("Uploading")} ${fileList.join(" ")}`);

	SendFileHttp(httpCmd.fwUpdate, formData, UpdateProgressDisplay, updatesuccess, updatefailed);
}

function updatesuccess(response) {
	// Restore ping monitoring after firmware upload completes
	restorePingAfterUpload();
	
	// Log upload completion
	Monitor_output_Update("[Firmware Upload] Upload completed successfully, restarting...\n");
	
	setHTML("updatemsg", translate_text_item("Restarting, please wait...."));
	setHTML("fw_file_name", "");
	let i = 0;
	let interval;
	const x = id("prgfw");
	x.max = 10;
	interval = setInterval(() => {
		i = i + 1;
		const x = id("prgfw");
		x.value = i;
		setHTML(
			"updatemsg",
			`${translate_text_item("Restarting, please wait....")} ${x.max + 1 - i} ${translate_text_item("seconds")}`,
		);
		if (i > x.max) {
			update_ongoing = false;
			clearInterval(interval);
			location.reload();
		}
	}, 1000);
	//console.log(response);
}

function updatefailed(error_code, response) {
	// Restore ping monitoring after firmware upload fails
	restorePingAfterUpload();
	
	// Log upload failure
	Monitor_output_Update(`[Firmware Upload] Upload failed: ${error_code}\n`);
	
	displayBlock("fw_select_form");
	displayNone("prgfw");
	setHTML("fw_file_name", translate_text_item("No file chosen"));
	displayNone("uploadfw_button");
	//setHTML('updatemsg', "");
	setValue("fw_select", "");
	if (esp_error_code !== 0) {
		alertdlg(translate_text_item("Error"), stdErrMsg(`(${esp_error_code})`, esp_error_message));
		setHTML("updatemsg", translate_text_item("Upload failed : ") + esp_error_message);
		esp_error_code = 0;
	} else {
		alertdlg(translate_text_item("Error"), stdErrMsg(error_code, response));
		setHTML("updatemsg", stdErrMsg(error_code, response, translate_text_item("Upload failed")));
	}
	conErr(error_code, response);
	update_ongoing = false;
	SendGetHttp(httpCmd.fwUpdate);
}
