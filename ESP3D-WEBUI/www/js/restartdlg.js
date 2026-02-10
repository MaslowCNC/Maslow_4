// import conErr, stdErrMsg, displayBlock, displayNone, id, setHTML, closeModal, setactiveModal, showModal, SendPrinterCommand, translate_text_item 

/** Restart dialog */
const restartdlg = () => {
	console.log("show restart");
	const modal = setactiveModal("restartdlg.html");
	if (modal == null) {
		return;
	}

	displayBlock("prgrestart");
	setHTML("restartmsg", translate_text_item("Restarting, please wait...."));
	showModal();
	SendPrinterCommand(
		"[ESP444]RESTART",
		false,
		restart_esp_success,
		restart_esp_failed,
	);
};

function restart_esp_success(response) {
	let i = 0;
	const x = id("prgrestart");
	http_communication_locked = true;
	x.max = 10;
	const interval = setInterval(() => {
		last_ping = Date.now();
		i = i + 1;
		const x = id("prgrestart");
		x.value = i;
		setHTML(
			"restartmsg",
			`${translate_text_item("Restarting, please wait....")} ${x.max + 1 - i} ${translate_text_item("seconds")}`,
		);
		if (i > x.max) {
			clearInterval(interval);
			location.reload();
		}
	}, 1000);
	//console.log(response);
}

function restart_esp_failed(error_code, response) {
	displayNone("prgrestart");
	setHTML(
		"restartmsg",
		stdErrMsg(error_code, response, translate_text_item("Upload failed")),
	);
	conErr(error_code, response);
	closeModal("Cancel");
}
