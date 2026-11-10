function noop() {}

/**
 * Send a command to the machine.
 * - [ESP...] commands go over HTTP; the firmware answers in the HTTP body, which goes to processfn.
 * - Realtime characters (?, !, ~, 0x18, bytes >= 0x80) go straight down the WebSocket; no reply.
 * - Every other line goes over the WebSocket through wsCommands, so all lines on this tab's
 *   firmware channel are in one ordered queue and each ok/error is matched to its own line:
 *   processfn() runs on "ok", errorfn(code) on "error:N".
 * Lines are never held for later: if the socket isn't open, nothing is sent and errorfn runs.
 */
function SendPrinterCommand(prnCmd, echo_on, processfn, errorfn, cmd_code, max_cmd_code, extra_arg) {
    if (prnCmd.trim().length === 0) {
        return;
    }

    var push_cmd = typeof echo_on !== 'undefined' ? echo_on : true;
    if (push_cmd) {
        Monitor_output_Update(`[#]${prnCmd.trim()}\n`);
    }

    //removeIf(production)
    console.log(prnCmd);
    if (typeof processfn !== 'undefined') {
        processfn("Test response");
    } else {
        SendPrinterCommandSuccess("Test response");
    }
    return;
    //endRemoveIf(production)

    if (!prnCmd.startsWith("[ESP")) {
        sendLineOrRealtime(prnCmd, processfn, errorfn);
        return;
    }

    // Ensure that we have valid functions defined for process and error returns
    const procFn = typeof processfn === "function" ? processfn : SendPrinterCommandSuccess;
    const errFn = typeof errorfn === "function" ? errorfn : SendPrinterCommandFailed;

	let cmd = buildHttpCommandCmd(httpCmdType.commandText, prnCmd);
    if (extra_arg) {
        cmd += `&${extra_arg}`;
    }

    SendGetHttp(cmd, procFn, errFn, cmd_code, max_cmd_code);
    //console.log(cmd);
}

/**
 * Send a non-[ESP] command over the WebSocket: a realtime character as a raw byte, anything
 * else as a tracked line. Returns true if it was sent.
 */
function sendLineOrRealtime(text, onOk, onError) {
    const realtime = wsCommands.isRealtime(text);
    const line = realtime ? text : text.trim();
    const sent = realtime
        ? wsCommands.sendRealtime(line.charCodeAt(0))
        : wsCommands.send(line, {
            onOk: typeof onOk === "function" ? () => onOk() : undefined,
            onError: typeof onError === "function" ? (code) => onError(code) : undefined,
        });
    if (!sent) {
        const why = `connection to the machine is ${wsCommands.socketStatus()}`;
        if (line === "?") {
            console.debug(`Status poll not sent (${why})`); // polled; the connection dot shows this
        } else if (typeof addMessage === "function") {
            const shown = realtime ? `0x${line.charCodeAt(0).toString(16)}` : line;
            addMessage(`Not sent: '${shown}' (${why})`);
        }
        if (typeof onError === "function") {
            onError("not sent");
        }
    }
    return sent;
}

function SendPrinterCommandSuccess(response) {
}

function SendPrinterCommandFailed(error_code, response) {
    const resp = HTMLDecode((response || "").trim());
    const errMsg = (error_code === 0)
        ? translate_text_item("Connection error")
        : stdErrMsg(error_code, resp, translate_text_item("Error"));

    Monitor_output_Update(`${errMsg}\n`);

    conErr(error_code, resp, "printer cmd Error");
}

//removeIf(production)
// ESM exports for unit testing - stripped from production builds by the gulp pipeline
export { sendLineOrRealtime };
//endRemoveIf(production)
