// import - get_icon_svg, displayBlock, displayInline, displayNone, id, stdErrMsg, setHTML, alertdlg, confirmdlg, inputdlg, SendPrinterCommand, tryAutoReport, SendFileHttp, SendGetHttp, translate_text_item, Monitor_output_Update

let files_current_path = "/";
/** get/set the current path used for files */
const files_currentPath = (value) => {
	if (typeof value === "string") {
		files_current_path = value;
	} else if (typeof value !== "string") {
		files_current_path = "/";
	}
	return files_current_path;
}

let files_filter_sd_list = false;
let files_file_list = [];
let files_status_list = [];
let files_current_file_index = -1;
let files_error_status = "";
let tfiles_filters;
const tft_sd = "SD:";
const tft_usb = "U:";
const printer_sd = "SDCARD:";
let current_source = "/";
let last_source = "/";

function build_file_filter_list(filters_list) {
	build_accept(filters_list);
	update_files_list();
}

function update_files_list() {
	//console.log("Updating list");
	if (files_file_list.length === 0) {
		return;
	}
	for (let i = 0; i < files_file_list.length; i++) {
		const isdirectory = files_file_list[i].isdir;
		const file_name = files_file_list[i].name;
		files_file_list[i].isprintable = files_isgcode(file_name, isdirectory);
	}
	files_build_display_filelist();
}

function build_accept(file_filters_list) {
	let accept_txt = "";
	if (typeof file_filters_list !== "undefined") {
		tfiles_filters = file_filters_list.trim().split(";");
		for (let i = 0; i < tfiles_filters.length; i++) {
			const v = tfiles_filters[i].trim();
			if (v.length > 0) {
				if (accept_txt.length > 0) {
					accept_txt += ", ";
				}
				accept_txt += `.${v}`;
			}
		}
	}
	if (accept_txt.length === 0) {
		accept_txt = "*, *.*";
		tfiles_filters = "";
	}
	const fif = id("files_input_file");
	if (fif) {
		fif.accept = accept_txt;
	}
	console.log(accept_txt);
}

const filesRefreshCurrent = () => files_refreshFiles(files_currentPath());
const filesRefreshPrimarySD = () => files_refreshFiles(primary_sd);
const filesRefreshSecondarySD = () => files_refreshFiles(secondary_sd);
const filesRefreshPrinterSD = () => {
	current_source = printer_sd;
	files_refreshFiles(files_currentPath());
}
const filesRefreshTFTSD = () => {
	current_source = tft_sd;
	files_refreshFiles(files_currentPath());
}
const filesRefreshTFTUSB = () => {
	current_source = tft_usb;
	files_refreshFiles(files_currentPath());
}

/** Set up the event handlers for the files panel */
function init_files_panel(dorefresh = true) {
	displayInline("files_refresh_btn");
	displayNone("files_refresh_primary_sd_btn");
	displayNone("files_refresh_secondary_sd_btn");

	id("files_createdir_btn").addEventListener("click", files_Createdir);
	id("files_filter_btn").addEventListener("click", files_filter_button);

	id("files_refresh_btn").addEventListener("click", filesRefreshCurrent);
	id("files_refresh_primary_sd_btn").addEventListener("click", filesRefreshPrimarySD);
	id("files_refresh_secondary_sd_btn").addEventListener("click", filesRefreshSecondarySD);

	id("files_refresh_printer_sd_btn").addEventListener("click", filesRefreshPrinterSD);
	id("files_refresh_tft_sd_btn").addEventListener("click", filesRefreshTFTSD);
	id("files_refresh_tft_usb_btn").addEventListener("click", filesRefreshTFTUSB);

	// TODO: Find out what happened to the `files_progress` method
	// id('progress_btn').addEventListener('click', files_progress);
	id("abort_btn").addEventListener("click", files_abort);
	id("print_upload_btn").addEventListener("click", files_select_upload);

	initFilesInputFile();

	files_set_button_as_filter(files_filter_sd_list);
	if (direct_sd && dorefresh) {
		files_refreshFiles(files_currentPath());
	}
}

/** Wire up the `files_input_file` handler */
const initFilesInputFile = () => id("files_input_file").addEventListener("change", files_check_if_upload);

const files_set_button_as_filter = (isfilter) => setHTML("files_filter_glyph", get_icon_svg(!isfilter ? "filter" : "list-alt", "1em", "1em"));

function files_filter_button() {
	files_filter_sd_list = !files_filter_sd_list;
	files_set_button_as_filter(files_filter_sd_list);
	files_build_display_filelist();
}

function formatFileSize(size) {
	const nSize = Number(size);
	if (Number.isNaN(nSize)) {
		return size;
	}
	// This is using true binary sizes, powers of 2, and not decimalised sizes
	for (const fSize in [{ size: "TB", pow: 40 }, { size: "GB", pow: 30 }, { size: "MB", pow: 20 }, { size: "KB", pow: 10 }, { size: "B", pow: 0 }]) {
		const dSize = (2 ** fSize.pow);
		if (nSize > dSize) {
			return `${(nSize / dSize).toFixed(2)} ${fSize.size}`;
		}
	}
	// We should only end up here if the file size is 0
	return `${nSize} B`;
}

const FileButton = (btnId, btnClass, icon, index) =>{
	return `<button id="${btnId}" data-index="${index}" class="btn btn-xs ${btnClass}" style='padding-top: 4px;'>${get_icon_svg(icon, "1em", "1em")}</button>`;
}

const FileAnchor = (btnId, btnClass, icon, url) => {
	return `<a id="${btnId}" class="btn btn-xs ${btnClass}" href="${url}" download="${url}" style='padding-top: 4px;'>${get_icon_svg(icon)}</a>`;
}

function files_build_file_line(index, actions) {
	let content = "";
	const entry = files_file_list[index];
	const is_clickable = files_is_clickable(index);
	if ((files_filter_sd_list && entry.isprintable) || !files_filter_sd_list) {
		const fliId = `filelist_${index}`;
		const clickStyle = is_clickable ? " style='cursor:pointer;'" : "";
		content += `<li id='${fliId}' data-index='${index}' class='list-group-item list-group-hover' ${clickStyle}>`;
		content += "<div class='row'>";
		content += "<div class='col-md-5 col-sm-5 no_overflow'>";
		content += "<table><tr>";
		content += `<td><span style='color:DeepSkyBlue;'>${get_icon_svg(entry.isdir ? "folder-open" : "file")}</span></td>`;
		content += `<td>${entry.name}</td>`;
		content += "</tr></table>";
		content += "</div>";
		if (is_clickable) {
			actions.push({ id: fliId, method: files_click_file });
		}
		let sizecol = "col-md-2 col-sm-2 filesize";
		let timecol = "col-md-2 col-sm-2";
		let iconcol = "col-md-3 col-sm-3";
		if (!entry.isdir && entry.datetime === "") {
			sizecol = "col-md-3 col-sm-3 filesize";
			timecol = "hide_it";
			iconcol = "col-md-4 col-sm-4";
		}
		const entrySize = entry.isdir ? "" : formatFileSize(entry.size);
		content += `<div class='${sizecol}'>${entrySize}</div>`;

		content += `<div class='${timecol}'>${entry.datetime}</div>`;
		content += `<div class='${iconcol}'>`;
		content += "<div class='pull-right'>";
		if (entry.isprintable) {
			content += FileButton(`${fliId}_print_btn`, "btn-default", "play", index);
			actions.push({ id: `${fliId}_print_btn`, method: files_print });
		}
		content += "&nbsp;";
		if (!entry.isdir) {
			content += FileAnchor(`${fliId}_download_btn`, "btn-default", "download", buildFileHref(index));			
		}
		if (files_showdeletebutton(index)) {
			content += FileButton(`${fliId}_delete_btn`, "btn-danger", "trash", index);
			actions.push({ id: `${fliId}_delete_btn`, method: files_delete });
		}
		content += FileButton(`${fliId}_rename_btn`, "btn-default", "wrench", index);
		actions.push({ id: `${fliId}_rename_btn`, method: files_rename });
		content += "</div>";
		content += "</div>";
		content += "</div>";
		content += "</li>";
	}
	return content;
}

function tabletSelectGCodeFile(filename) {
	const selector = id("filelist");
	const options = Array.from(selector.options);
	const option = options.find((item) => item.text === filename);
	option.selected = true;
}

const getEventIndex = (event) => Number.parseInt(event.currentTarget.dataset.index);

function files_print(event) {
	event.stopPropagation();
	const index = getEventIndex(event);
	const file = files_file_list[index];
	const path = `${files_currentPath()}${file.name}`;
	tabletSelectGCodeFile(file.name);
	tabletLoadGCodeFile(path, file.size);
	files_print_filename(path);
}

function files_print_filename(path) {
	get_status();
	if (reportType === "none") {
		tryAutoReport(); // will fall back to polled if autoreport fails
	}
	SendPrinterCommand(`$SD/Run=${path}`);
}

const files_Createdir = () => inputdlg(translate_text_item("Please enter directory name"), translate_text_item("Name:"), process_files_Createdir);

function process_files_Createdir(answer) {
	if (answer.length > 0) {
		files_create_dir(answer.trim());
	}
}

function files_create_dir(name) {
	if (!direct_sd) {
		return;
	}

	displayBlock("files_nav_loader");

	const cmd = buildHttpFileCmd({ action: "createdir", filename: name });
	SendGetHttp(cmd, files_list_success, files_list_failed);
}

function files_delete(event) {
	event.stopPropagation();
	const index = getEventIndex(event);
	files_current_file_index = index;
	const ffli = files_file_list[index];
	const msg = `${translate_text_item(ffli.isdir ? "Confirm deletion of directory: " : "Confirm deletion of file: ")}${ffli.name}`;
	confirmdlg(translate_text_item("Please Confirm"), msg, process_files_Delete);
}

function process_files_Delete(answer) {
	if (answer === "yes" && files_current_file_index !== -1) {
		files_delete_file(files_current_file_index);
	}
	files_current_file_index = -1;
}

function files_delete_file(index) {
	if (!direct_sd || (files_file_list.length - 1) < index) {
		return;
	}
	const fFile = files_file_list[index];
	files_error_status = `Delete ${fFile.name}`;

	// Log deletion start
	Monitor_output_Update(`[Delete] Deleting ${fFile.isdir ? 'directory' : 'file'}: ${fFile.name}\n`);
	
	// Disable ping monitoring during delete (can take long for large files)
	disablePingForUpload();

	displayBlock("files_nav_loader");

	const action = fFile.isdir ? "deletedir" : "delete";
	const cmd = buildHttpFileCmd({ action: action, filename: fFile.sdname });
	SendGetHttp(cmd, files_delete_success, files_delete_failed);
}

const files_is_clickable = (index) => files_file_list[index].isdir ? true : direct_sd;
const files_enter_dir = (name) => files_refreshFiles(`${files_currentPath()}${name}/`);

let old_file_name;
function files_rename(event) {
	event.stopPropagation();
	const index = getEventIndex(event);
	const entry = files_file_list[index];
	old_file_name = entry.sdname;
	inputdlg(translate_text_item("New file name"), translate_text_item("Name:"), process_files_rename, old_file_name);
}

function process_files_rename(new_file_name) {
	if (!new_file_name) {
		return;
	}
	files_error_status = `Rename ${old_file_name}`;

	displayBlock("files_nav_loader");

	const cmd = buildHttpFileCmd({ action: "rename", filename: old_file_name, newname: new_file_name });
	SendGetHttp(cmd, files_list_success, files_list_failed);
}

const buildFileHref = (index) => encodeURIComponent(`SD/${files_currentPath()}${files_file_list[index].sdname}`.replace("//", "/"));

function files_click_file(event) {
	event.stopPropagation();
	const index = getEventIndex(event);
	const entry = files_file_list[index];
	if (entry.isdir) {
		files_enter_dir(entry.name);
		return;
	}
	if (false && direct_sd) {
		// Don't download on click; use the button
		//console.log("file on direct SD");
		window.location.href = buildFileHref(index);
		return;
	}
}

function files_isgcode(filename, isdir) {
	if (isdir) {
		return false;
	}
	// This can happen if files_showprintbutton is called before the
	// files panel has been created
	if (typeof tfiles_filters === "undefined") {
		return false;
	}
	if (tfiles_filters.length === 0) {
		return true;
	}
	for (let i = 0; i < tfiles_filters.length; i++) {
		const v = `.${tfiles_filters[i].trim()}`;
		if (filename.endsWith(v)) {
			return true;
		}
	}
	return false;
}

function files_showdeletebutton(index) {
	//can always deleted dile or dir ?
	//if /ext/ is serial it should failed as fw does not support it
	//var entry = files_file_list[index];
	//if (direct_sd) return true;
	//if (!entry.isdir) return true;
	return true;
}

function files_refreshFiles(path) {
	//console.log("refresh requested " + path);
	const cmdpath = path;
	files_currentPath(path);
	if (current_source !== last_source) {
		files_currentPath("/");
		// path = "/";
		last_source = current_source;
	}

	if ([tft_sd, tft_usb].includes(current_source)) {
		displayNone("print_upload_btn");
	} else {
		displayBlock("print_upload_btn");
	}
	setHTML("files_currentPath", files_currentPath());
	files_file_list = [];
	files_status_list = [];
	files_build_display_filelist(false);
	displayBlock("files_list_loader");
	displayBlock("files_nav_loader");

	//this is pure direct SD
	if (direct_sd) {
		const cmd = buildHttpFileCmd({ path: cmdpath });
		SendGetHttp(cmd, files_list_success, files_list_failed);
	}
}

function addOption(selector, name, value, isDisabled, isSelected) {
	const opt = document.createElement("option");
	opt.appendChild(document.createTextNode(name));
	opt.disabled = isDisabled;
	opt.selected = isSelected;
	opt.value = value;
	selector.appendChild(opt);
}

const populateTabletFileSelector = (files, path) => {
	const selector = id("filelist");
	if (!selector) {
		return;
	}

	selector.length = 0;
	selector.selectedIndex = 0;
	const selectedFile = gCodeFilename.split("/").slice(-1)[0];

	if (!files.length) {
		addOption(selector, "No files found", -3, true, selectedFile === "");
		// Update delete button state after populating file list
		if (typeof updateDeleteButtonState === 'function') {
			updateDeleteButtonState();
		}
		return;
	}
	const inRoot = path === "/";
	const legend = `Load GCode File from /SD${path}`;
	addOption(selector, legend, -2, true, true); // A different one might be selected later

	// Add "Clear GCode from Memory" option at the top
	addOption(selector, "Clear GCode from Memory", -4, false, false);

	if (!inRoot) {
		addOption(selector, "..", -1, false, false);
	}
	let gCodeFileFound = false;
	files.forEach((file, index) => {
		if (file.isprintable) {
			const found = file.name === selectedFile;
			if (found) {
				gCodeFileFound = true;
			}
			addOption(selector, file.name, index, false, found);
		}
	});
	// Only clear the loaded file if it's supposed to be in this directory
	// Check if gCodeFilename's directory matches the current path
	// Don't clear if restoration is currently in progress OR if file is being loaded
	if (!gCodeFileFound && gCodeFilename) {
		// Check if restoration is in progress (if the variable exists)
		const isRestoring = (typeof restoringGCodeState !== 'undefined') && restoringGCodeState;
		// Check if file is set but not loaded yet (being loaded)
		const isLoading = gCodeFilename && !gCodeLoaded;
		if (!isRestoring && !isLoading) {
			const fileDirectory = gCodeFilename.substring(0, gCodeFilename.lastIndexOf('/') + 1) || '/';
			const normalizedPath = path.endsWith('/') ? path : path + '/';
			// Only clear if the file should be in this directory
			if (fileDirectory === normalizedPath) {
				gCodeFilename = "";
				gCodeDisplayable = false;
				showGCode("");
			}
		}
	}

	files.forEach((file, index) => {
		if (file.isdir) {
			addOption(selector, `${file.name}/`, index, false, false);
		}
	});
	
	// Update delete button state after populating file list
	if (typeof updateDeleteButtonState === 'function') {
		updateDeleteButtonState();
	}
};

const files_list_success = (response_text) => {
	// Note: Ping restore is NOT done here because this callback is used for multiple operations
	// Upload completion calls this via files_upload_success which handles ping restore
	// Delete completion calls this via files_delete_success which handles ping restore
	// Regular list refreshes don't need ping restore
	
	displayBlock("files_navigation_buttons");
	let error = false;
	let response;
	try {
		if (response_text.length) {
			response = JSON.parse(response_text);
		} else {
			response = {files: []}
		}
	} catch (e) {
		console.error(`Parsing error: ${e}\n${response_text}`);
		error = true;
	}
	if (error || typeof response.status === "undefined") {
		files_list_failed(406, translate_text_item("Wrong data", true));
		return;
	}
	// Process files first to build files_file_list
	files_file_list = [];
	if (Array.isArray(response.files)) {
		for (let i = 0; i < response.files.length; i++) {
			const file = response.files[i];
			const file_name = file.name;
			const isdirectory = file.size === "-1";
			files_file_list.push({
				name: file_name,
				sdname: file_name,
				size: !isdirectory ? file.size : "",
				isdir: isdirectory,
				datetime: file.datetime,
				isprintable: files_isgcode(file_name, isdirectory),
			});
		}
	}
	files_file_list.sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: 'base' }));
	
	// Now populate the tablet file selector with the processed file list
	populateTabletFileSelector(files_file_list, files_currentPath());
	let vtotal = "-1";
	let vused = "-1";
	let voccupation = "-1";
	if (typeof response.total !== "undefined") {
		vtotal = response.total;
	}
	if (typeof response.used !== "undefined") {
		vused = response.used;
	}
	if (typeof response.occupation !== "undefined") {
		voccupation = response.occupation;
	}
	files_status_list = [];
	files_status_list.push({
		status: translate_text_item(response.status),
		path: response.path,
		used: vused,
		total: vtotal,
		occupation: voccupation,
	});
	files_build_display_filelist();
};

/** Shows an alert dialog for the ESP error, and then clears the ESP error_code */
const alertEspError = () => {
	alertdlg(translate_text_item("Error"), stdErrMsg(`(${esp_error_code})`, esp_error_message));
	esp_error_code = 0;
};

function files_list_failed(error_code, response) {
	displayBlock("files_navigation_buttons");
	if (esp_error_code !== 0) {
		alertEspError();
	} else {
		alertdlg(translate_text_item("Error"), translate_text_item("No connection"));
	}
	files_build_display_filelist(false);
}

const files_delete_success = (response_text) => {
	// Restore ping monitoring after delete completes
	restorePingAfterUpload();
	
	// Log deletion completion
	Monitor_output_Update("[Delete] Deletion completed successfully\n");
	
	// Reuse the list success handler
	files_list_success(response_text);
};

const files_delete_failed = (error_code, response) => {
	// Restore ping monitoring after delete fails
	restorePingAfterUpload();
	
	// Log deletion failure
	Monitor_output_Update(`[Delete] Deletion failed: ${error_code}\n`);
	
	// Reuse the list failed handler
	files_list_failed(error_code, response);
};

function files_directSD_upload_failed(error_code, response) {
	// Restore ping monitoring after upload fails
	restorePingAfterUpload();
	
	// Log upload failure
	Monitor_output_Update(`[Upload] Upload failed: ${error_code}\n`);
	
	if (esp_error_code !== 0) {
		alertEspError();
	} else {
		alertdlg(translate_text_item("Error"), translate_text_item("Upload failed"));
	}
	displayNone("files_uploading_msg");
	displayBlock("files_navigation_buttons");
}

const need_up_level = () => files_currentPath() !== "/";

function files_go_levelup(event) {
	event.stopPropagation();

	const tlist = files_currentPath().split("/");
	let path = "/";
	let nb = 1;
	while (nb < tlist.length - 2) {
		path += `${tlist[nb]}/`;
		nb++;
	}
	files_refreshFiles(path);
}

function files_build_display_filelist(displaylist = true) {
	populateTabletFileSelector(files_file_list, files_currentPath());

	displayNone("files_uploading_msg");
	displayNone("files_list_loader");
	displayNone("files_nav_loader");

	const fileListElem = id("files_fileList");

	if (!displaylist) {
		displayNone("files_status_sd_status");
		displayNone("files_space_sd_status");
		if (fileListElem) {
			fileListElem.innerHTML = "";
			displayNone("files_fileList");
		}
		return;
	}

	if (fileListElem) {
		const actions = [];
		let content = "";
		if (need_up_level()) {
			const liId = "filelist_go_up";
			content += `<li id='${liId}' class='list-group-item list-group-hover' style='cursor:pointer'>`;
			content += `<span>${get_icon_svg("level-up")}</span>&nbsp;&nbsp;<span translate>Up...</span>`;
			content += "</li>";
			actions.push({ id: liId, method: files_go_levelup });
		}
		for (let index = 0; index < files_file_list.length; index++) {
			if (!files_file_list[index].isdir)
				content += files_build_file_line(index, actions);
		}
		for (index = 0; index < files_file_list.length; index++) {
			if (files_file_list[index].isdir)
				content += files_build_file_line(index, actions);
		}

		fileListElem.innerHTML = content;
		for (const action of actions) {
			const elem = id(action.id);
			if (elem) {
				elem.addEventListener("click", action.method);
			}
		}
		displayBlock("files_fileList");
	}

	if (files_status_list.length === 0 && files_error_status !== "") {
		files_status_list.push({
			status: files_error_status,
			path: files_currentPath(),
			used: "-1",
			total: "-1",
			occupation: "-1",
		});
	}
	if (files_status_list.length > 0) {
		if (files_status_list[0].total !== "-1") {
			setHTML("files_sd_status_total", files_status_list[0].total);
			setHTML("files_sd_status_used", files_status_list[0].used);
			setValue("files_sd_status_occupation", files_status_list[0].occupation);
			setHTML("files_sd_status_percent", files_status_list[0].occupation);
			displayTable("files_space_sd_status");
		} else {
			displayNone("files_space_sd_status");
		}
		if (
			files_error_status !== "" &&
			(files_status_list[0].status.toLowerCase() === "ok" ||
				files_status_list[0].status.length === 0)
		) {
			files_status_list[0].status = files_error_status;
		}
		files_error_status = "";
		if (files_status_list[0].status.toLowerCase() !== "ok") {
			setHTML("files_sd_status_msg", translate_text_item(files_status_list[0].status, true));
			displayTable("files_status_sd_status");
		} else {
			displayNone("files_status_sd_status");
		}
	} else displayNone("files_space_sd_status");
}

const files_abort = () => SendPrinterCommand("abort");

/** Wires up, and clicks the `files_input_file` element for you */
const files_select_upload = () => {
	initFilesInputFile();
	id("files_input_file").click();
}

function files_check_if_upload() {
	if (direct_sd) {
		SendPrinterCommand("[ESP200]", false, process_check_sd_presence, null);
	} else {
		//no reliable way to know SD is present or not so let's upload
		files_start_upload();
	}
}

function process_check_sd_presence(answer) {
	//console.log(answer);
	//for direct SD there is a SD check
	if (direct_sd) {
		if (answer.indexOf("o SD card") > -1) {
			alertdlg(translate_text_item("Upload failed"), translate_text_item("No SD card detected"));
			files_error_status = "No SD card";
			files_build_display_filelist(false);
			setHTML("files_sd_status_msg", translate_text_item(files_error_status, true));
			displayTable("files_status_sd_status");
		} else {
			files_start_upload();
		}
	} else {
		//for smoothiware ls say no directory
		files_start_upload();
	}
}

const FileUploadNotice = (file) => {
	files_error_status = `Upload ${file.name}`;
	setHTML("files_currentUpload_msg", file.name);
}

const BuildFileUploadFormData = (path, files, perFileFn) => {
	//console.log("upload from " + path );
	const formData = new FormData();
	formData.append("path", path);
	for (let i = 0; i < files.length; i++) {
		const file = files[i];
		const fullFilename = `${path}${file.name}`;
		//append file size first to check upload is complete
		formData.append(`${fullFilename}S`, file.size);
		formData.append("myfile[]", file, fullFilename);
		console.info(`Preparing ${fullFilename} for upload`);

		if (typeof perFileFn === "function") {
			perFileFn(file);
		}
	}
	return formData;
}

function files_start_upload() {
	if (CheckForHttpCommLock()) {
		return;
	}

	const files = id("files_input_file").files;

	if (!files.length || typeof files[0].name === "undefined") {
		console.log("nothing to upload");
		return;
	}

	const formData = BuildFileUploadFormData(files_currentPath(), files, FileUploadNotice);

	// Reset progress logging
	FilesUploadProgressDisplay.lastLoggedPercent = -1;
	
	// Log upload start
	const fileName = files[0].name;
	const fileSize = (files[0].size / 1024 / 1024).toFixed(2);
	Monitor_output_Update(`[Upload] Starting upload of ${fileName} (${fileSize} MB)\n`);

	// Disable ping monitoring during upload
	disablePingForUpload();

	displayBlock("files_uploading_msg");
	displayNone("files_navigation_buttons");
	if (direct_sd) {
		SendFileHttp(httpCmd.fileUpload, formData, FilesUploadProgressDisplay, files_upload_success, files_directSD_upload_failed);
		//console.log("send file");
	}
	setValue("files_input_file", "");
}

const files_upload_success = (response_text) => {
	// Restore ping monitoring after upload completes
	restorePingAfterUpload();
	
	// Log upload completion
	Monitor_output_Update("[Upload] Upload completed successfully\n");
	
	// Reuse the list success handler
	files_list_success(response_text);
};

function FilesUploadProgressDisplay(oEvent) {
	if (oEvent.lengthComputable) {
		const percentComplete = (oEvent.loaded / oEvent.total) * 100;
		setValue("files_prg", percentComplete);
		setHTML("files_percent_upload", percentComplete.toFixed(0));
		
		// Log progress to serial messages at 10% intervals
		const percent = Math.floor(percentComplete);
		if (percent % 10 === 0 && percent !== FilesUploadProgressDisplay.lastLoggedPercent) {
			FilesUploadProgressDisplay.lastLoggedPercent = percent;
			Monitor_output_Update(`[Upload] ${percent}% (${(oEvent.loaded / 1024 / 1024).toFixed(2)} MB / ${(oEvent.total / 1024 / 1024).toFixed(2)} MB)\n`);
		}
	} else {
		// Impossible because size is unknown
	}
}
FilesUploadProgressDisplay.lastLoggedPercent = -1;
