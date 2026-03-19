// import translate_text_item, id, setHTML, setactiveModal, showModal, saveSerialMessages

// Reconnect via WebSocket only — do NOT reload the page.
// A full page reload (window.location.reload) would trigger an HTTP GET /
// which returns 503 while GCode is running (HTTP blocked during motion).
// The 503 page contains a "Feedhold" button that can accidentally pause the
// machine.  Reconnecting the WebSocket only bypasses HTTP entirely so the UI
// can reconnect while a GCode job is in progress.
const UIdisabledDlgReconnect = () => {
	closeModal();
	resetConnectionState();
};

//UIdisabled dialog
const UIdisableddlg = (lostcon) => {
	const modal = setactiveModal("UIdisableddlg.html");
	if (modal == null) {
		return;
	}

	id("UIdisabled_reconnect").addEventListener("click", UIdisabledDlgReconnect, { once: true });
	id("UIdisabled_save_serial_msg").addEventListener("click", saveSerialMessages);

	if (lostcon) {
		setHTML("disconnection_msg", translate_text_item("Connection lost for more than 20s"));
	}
	showModal();
};
