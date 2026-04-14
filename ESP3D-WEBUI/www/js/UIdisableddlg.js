// import translate_text_item, id, setHTML, setactiveModal, showModal, saveSerialMessages, closeModal, resetConnectionState

const UIdisabledDlgReconnect = () => {
	closeModal("Reconnect button");
	resetConnectionState();
};

//UIdisabled dialog
const UIdisableddlg = (lostcon) => {
	const modal = setactiveModal("UIdisableddlg.html");
	if (modal == null) {
		return;
	}

	id("UIdisabled_reconnect").addEventListener("click", UIdisabledDlgReconnect, { once: true });
	id("UIdisabled_save_serial_msg").addEventListener("click", saveSerialMessages, { once: true });

	if (lostcon) {
		setHTML("disconnection_msg", translate_text_item("Connection lost for more than 20s"));
	}
	showModal();
};
