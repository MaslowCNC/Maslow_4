let http_communication_locked = false;
/** A list of various command objects that can be used as a queue */
const cmd_list = [];
let processing_cmd = false;
let xmlhttpupload;
const max_cmd = 40;
let cmdInterval = 0;

const processNextCmd = () => {
    if (!cmd_list.length) {
        console.warn("Command list empty, no messages to process at this time");
        cmdInterval = 0;
        return;
    }

    switch (process_cmd_list(cmd_list[0], "process")) {
        case 1:
            // Success, job done, move on ...
            break;
        case -3:
            // Malformed command, remove from the list without changing anything else
            cmd_list.splice(0, 1);
            break;
        case -2:
            // TODO: Too many commands in the cmd_list, this should be reprocessed later
            break;
        case -1:
            // TODO: Currently processing a command, this should be retried
            break;
        default:
            break;
    }
    
    // Schedule next processing if there are more commands
    if (cmd_list.length > 0 && cmdInterval) {
        scheduleTask(processNextCmd);
    } else {
        cmdInterval = 0;
    }
}

/** This comes straight out of the Mozilla website for Math.random */
const getRandomIntExclusive = (min = 0, max = 10000) => {
    const minCeiled = Math.ceil(min);
    const maxFloored = Math.floor(max);
    return Math.floor(Math.random() * (maxFloored - minCeiled) + minCeiled);
}

const zeroPrefixedString = (value, prefix = "00000") => `${prefix}${value}`.slice(-prefix.length);

/** Remove the command with the specified Id */
const removeCmd = (cmdId) => {
    const ix = cmd_list.findIndex((c) => c.id === cmdId);
    if (ix !== -1) {
        cmd_list.splice(ix, 1);
    } else {
        console.warn(`Wanted to remove the command with id:'${cmdId}', but no matching command found in the list. The command list has probably been purged.`);
    }
}

const validateProcessing = (cmd, step = "") => {
    if (cmd_lock) {
        // Currently doing a cmd_list process, so this should probably be retried
        return -1;
    }

    if (!["add", "process", "remove", "purge"].includes(step)) {
        // The step is invalid, so the associated command should be discarded
        return -3;
    }
    
    if (cmd_list.length > max_cmd) {
        http_errorfn(cmd, 503, translate_text_item("Server not responding"));
        // Exceeded the cmd_list maximum size, this should probably be retried once other commands have been processed and removed
        return -2;
    }

    return 0;
}

const validateCommand = (cmd, step = "") => {
    const isCmdObject = typeof cmd === "object";
    if (!isCmdObject) {
        // The cmd is invalid, so this should be discarded
        return -3;
    }

    const cmdType = cmd?.type || "";
    if (!["GET", "POST"].includes(cmdType)) {
        // The cmd is invalid, so this should be discarded
        return -3;
    }

    // Make sure that any step (except for "add" and "purge") already has an id in the cmd
    if (!("id" in cmd) && ["process", "remove"].includes(step)) {
        // The cmd is invalid, so this should be discarded, although this does imply a programmer error
        return -3;
    }

    return 0;
}

/** A semaphore (sorta mutex) for working with the cmd_list */
let cmd_lock = false;

/** Process the supplied cmd through the various stages in its life cycle */
const process_cmd_list = (cmd, step = "") => {
    let validationState = validateProcessing(cmd, step);
    if (validationState < 0) {
        return validationState;
    }

    validationState = validateCommand(cmd, step);
    if (validationState < 0) {
        return validationState;
    }

    cmd_lock = true;

    if (!("id" in cmd) && step === "add") {
        // Ensure there's always an id when adding a command. Hopefully unique
        cmd.id = `T${Date.now()}R${zeroPrefixedString(getRandomIntExclusive())}`;
    }

    let doNext = false;

    switch (step) {
        case "add":
            // Add a new cmd to the list
            cmd_list.push(cmd);
            doNext = true;
            break;
        case "process":
            // Pass the supplied cmd from the list off for processing
            process_cmd(cmd);
            // Nothing more to do because the XHR callbacks take over from here
            doNext = false;
            break;
        case "remove":
            // Clean up after processing the cmd
            removeCmd(cmd.id);

            if (cmd_list.length) {
                doNext = true;
            } else {
                cmdInterval = 0;
            }

            processing_cmd = false;
            break;
        case "purge":
            // Wipe the command list 
            cmd_list.length = 0;
            cmdInterval = 0;
            doNext = false;
            processing_cmd = false;
            break;
    }
    cmd_lock = false;

    if (doNext) {
        // Use scheduleTask (MessageChannel) to ensure command processing works in background tabs
        // This provides immediate scheduling without the 10ms throttling, which is acceptable
        // since HTTP requests themselves provide natural throttling
        if (!cmdInterval) {
            cmdInterval = 1; // Mark as active
            scheduleTask(processNextCmd);
        }
    }

    // Success
    return 1;
}

/** Wipe the command list - Nuclear option */
const clear_cmd_list = () => {
    process_cmd_list({ "id": "0" }, "purge");
}

function http_resultfn(cmd, response_text) {
    if (typeof cmd.resultfn === "function") {
        cmd.resultfn(response_text);
    }
    process_cmd_list(cmd, "remove");
}

function http_errorfn(cmd, error_code, response_text) {
    if (typeof cmd.errorfn === "function") {
        if (error_code === 401) {
            logindlg();
            console.log("Authentication issue pls log");
        }
        cmd.errorfn(error_code, response_text);
    } else {
        console.error(`Error '${error_code}' with response '${response_text}'`);
    }
    process_cmd_list(cmd, "remove");
}

const process_cmd = (cmd) => {
    if (processing_cmd) {
        return;
    }

    const cmdType = cmd.type;
    processing_cmd = true;
    switch (cmdType) {
        case "GET":
            ProcessGetHttp(cmd);
            break;
        case "POST":
            // POST is only ever used for file uploading
            //console.log("Uploading");
            ProcessFileHttp(cmd);
            break;
        default:
            // Unknown command type
            // This should never be true, but just in case we will handle it
            http_errorfn(cmd, 400, translate_text_item(`Unknown command type '${cmdType}'`));
            break;
    }
}

function SendGetHttp(url, result_fn, error_fn, cmd_code, max_cmd_code) {
    let cmd_code_id = 0;
    /** The maximum number of times that this cmd_code can be added to the list */
    let max_of_cmd_code = 1;

    if (typeof cmd_code !== 'undefined') {
        cmd_code_id = cmd_code;
        if (typeof max_cmd_code !== 'undefined') {
            max_of_cmd_code = max_cmd_code;
        }
        //else console.log("No Max ID defined");
        for (p = 0; p < cmd_list.length; p++) {
            //console.log("compare " + (max_cmd_code - max_of_cmd_code));
            if (cmd_list[p].cmd_code === cmd_code_id) {
                max_of_cmd_code--;
                //console.log("found " + cmd_list[p].cmd_code + " and " + cmd_code_id);
            }
            if (max_of_cmd_code <= 0) {
                console.log(`Limit reached for ${cmd_code}`);
                return;
            }
        }
    } //else console.log("No ID defined");

    const cmd = {
        cmd: url,
        type: "GET",
        isupload: false,
        resultfn: result_fn,
        errorfn: error_fn,
        cmd_code: cmd_code_id
    };
    process_cmd_list(cmd, "add");
}

function ProcessGetHttp(cmd) {
    if (http_communication_locked) {
        http_errorfn(cmd, 503, translate_text_item("Communication locked!"));
        console.log("locked");
        return;
    }

    const xmlhttp = new XMLHttpRequest();
    xmlhttp.onreadystatechange = () => {
        if (xmlhttp.readyState === 4) {
            if (xmlhttp.status === 200) {
                //console.log("*** " + url + " done");
                http_resultfn(cmd, xmlhttp.responseText);
            } else {
                if (xmlhttp.status === 401) {
                    GetIdentificationStatus();
                }
                http_errorfn(cmd, xmlhttp.status, xmlhttp.responseText);
            }
        }
    }

    xmlhttp.open("GET", cmd.cmd, true);
    xmlhttp.send();
}

/** POST the file FormData */
function SendFileHttp(url, postdata, progress_fn, result_fn, error_fn) {
    const cmd = {
        cmd: url,
        type: "POST",
        isupload: true,
        data: postdata,
        progressfn: progress_fn,
        resultfn: result_fn,
        errorfn: error_fn,
        cmd_code: 0
    };
    process_cmd_list(cmd, "add");
}

function ProcessFileHttp(cmd) {
    if (http_communication_locked) {
        http_errorfn(cmd, 503, translate_text_item("Communication locked!"));
        return;
    }

    http_communication_locked = true;

    xmlhttpupload = new XMLHttpRequest();
    xmlhttpupload.onreadystatechange = () => {
        if (xmlhttpupload.readyState === 4) {
            http_communication_locked = false;
            if (xmlhttpupload.status === 200) {
                http_resultfn(cmd, xmlhttpupload.responseText);
            } else {
                if (xmlhttpupload.status === 401) GetIdentificationStatus();
                http_errorfn(cmd, xmlhttpupload.status, xmlhttpupload.responseText);
            }
        }
    }
    xmlhttpupload.open("POST", cmd.cmd, true);
    if (typeof cmd.progressfn === "function") {
        xmlhttpupload.upload.addEventListener("progress", cmd.progressfn, false);
    }
    xmlhttpupload.send(cmd.data);
}

const CheckForHttpCommLock = () => {
    if (http_communication_locked) {
        alertdlg(translate_text_item("Busy..."), translate_text_item("Communications are currently locked, please wait and retry."));
        console.warn("communication locked");
    }
    return http_communication_locked;
}
