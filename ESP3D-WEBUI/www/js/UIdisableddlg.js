// import translate_text_item, id, setHTML, setactiveModal, showModal, saveSerialMessages, resetConnectionState, closeModal, startSocket

const UIdisabledDlgReconnect = () => {
	// Reset the disabled state and attempt a WebSocket reconnect.  If the
	// firmware is reachable the WebSocket connects and onopen re-enables the
	// interface.  If it is not yet reachable, onclose retries every 3 seconds
	// automatically — no hard page reload required, so in-progress GCode jobs
	// remain visible after reconnect.
	resetConnectionState();
	startSocket();
};

//UIdisabled dialog
const UIdisableddlg = (lostcon) => {
	const modal = setactiveModal("UIdisableddlg.html");
	if (modal == null) {
		return;
	}

	id("UIdisabled_reconnect").addEventListener("click", UIdisabledDlgReconnect);
	id("UIdisabled_save_serial_msg").addEventListener("click", saveSerialMessages);

	if (lostcon) {
		setHTML("disconnection_msg", translate_text_item("Connection lost for more than 20s"));
	}
	showModal();
};
