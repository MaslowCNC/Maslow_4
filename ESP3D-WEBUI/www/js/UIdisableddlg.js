// import translate_text_item, id, setHTML, setactiveModal, showModal, saveSerialMessages

const UIdisabledDlgReconnect = () => {
	// Reconnect without a full page reload so the browser can regain control
	// even while GCode is running.  A page reload would be blocked by the
	// firmware's http_block_during_motion setting in that case.
	if (typeof resetConnectionState === 'function') {
		resetConnectionState();
	} else {
		window.location.reload();
	}
};

//UIdisabled dialog
const UIdisableddlg = (lostcon) => {
	const modal = setactiveModal("UIdisableddlg.html");
	if (modal == null) {
		return;
	}

	// Use { once: true } so the listener auto-removes itself after the first
	// click.  Without this, every call to UIdisableddlg() (each disconnection)
	// stacks a new listener on top of any previous ones.  After N disconnections
	// clicking the button fires resetConnectionState() N times in rapid succession,
	// and each call tears down the connection the previous call just opened.
	id("UIdisabled_reconnect").addEventListener("click", UIdisabledDlgReconnect, { once: true });
	id("UIdisabled_save_serial_msg").addEventListener("click", saveSerialMessages, { once: true });

	if (lostcon) {
		setHTML("disconnection_msg", translate_text_item("Connection lost for more than 20s"));
	}
	showModal();
};
