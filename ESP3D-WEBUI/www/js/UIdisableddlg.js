// import translate_text_item, id, setHTML, setactiveModal, showModal, saveSerialMessages, closeModal, resetConnectionState

const UIdisabledDlgReconnect = () => {
	closeModal("Reconnect button");
	resetConnectionState();
};

// The dialog's original message and button text, kept so a notice can be undone next time
let UIdisabledDefaultText = null;

/** Set the dialog's message and button: a close notice (socket.js WS_CLOSE_NOTICES), a lost
 *  connection, or the original text */
const setUIdisabledText = (lostcon, notice) => {
	const msg = id("disconnection_msg");
	const btn = id("UIdisabled_reconnect");
	if (!msg || !btn) {
		return;
	}
	if (UIdisabledDefaultText === null) {
		UIdisabledDefaultText = { message: msg.innerHTML, button: btn.innerHTML };
	}
	if (notice) {
		setHTML("disconnection_msg", translate_text_item(notice.message));
		setHTML("UIdisabled_reconnect", translate_text_item(notice.button));
		return;
	}
	setHTML("UIdisabled_reconnect", UIdisabledDefaultText.button);
	setHTML("disconnection_msg", lostcon ? translate_text_item("Connection lost for more than 20s") : UIdisabledDefaultText.message);
};

//UIdisabled dialog
const UIdisableddlg = (lostcon, notice) => {
	const modal = setactiveModal("UIdisableddlg.html");
	if (modal == null) {
		return;
	}

	id("UIdisabled_reconnect").addEventListener("click", UIdisabledDlgReconnect, { once: true });
	id("UIdisabled_save_serial_msg").addEventListener("click", saveSerialMessages, { once: true });

	setUIdisabledText(lostcon, notice);
	showModal();
};
