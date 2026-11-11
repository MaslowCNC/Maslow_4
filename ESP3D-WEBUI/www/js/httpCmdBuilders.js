// Various helper methods for building http commands

/** 'Commands' to be sent as the first part of the URL after the host name */
const httpCmd = {
    command: "/command",
    fileGet: "/",
    /** Perform a GET or POST file action. Used by files.js and tablet.js (i.e. not SPIFFs) */
    fileUpload: "/upload",
    /** Perform a files action.
     * For a POST this is used with FormData.
     * For a GET this is used with parameters */
    files: "/files",
    /** Perform a firmware action.
     * For a POST this is used with FormData the firmware.
     * For a GET this this does something else? */
    fwUpdate: "/updatefw",
    /** Perform some auth related GET action */
    login: "/login",
};

/** Command Types for the http `/command` command */
const httpCmdType = {
    "plain": "plain",
    "commandText": "commandText"
};

/** Extract a named parameter value from the supplied params value,
 * if it's falsey use the defaultValue */
const getParam = (params, paramName, defaultValue = "") => {
    return (paramName in params && params[paramName].trim())
        ? params[paramName].trim()
        : defaultValue;
}

/** Do a deep copy of the obj */
const deepCopy = (obj) => JSON.parse(JSON.stringify(obj));

/** Build out command based on the supplied parameters, and whether a given parameter should be encoded or not */
const buildHttpCmd = (httpcmd, params = {}, encKeys = [], noEncKeys = []) => {
    const cmd = [httpcmd];
    const prms = deepCopy(params);

    for (const key of Object.keys(prms)) {
        let pVal = getParam(prms, key);
        if (!pVal) {
            continue;
        }
        // If the key is not in the `noEncKeys` list then it will be encoded
        if (![noEncKeys].includes(key)) {
            pVal = encodeURIComponent(pVal);
        }
        // If the key is in the `encKeys` list then it will be encoded
        if ([encKeys].includes(key)) {
            pVal = encodeURIComponent(pVal);
        }
        // If this is the first part of the command then append it to the httpcmd
        // so long as httpcmd does not already include a first parameter
        if (cmd.length === 1 && !cmd[0].includes("?")) {
            cmd[0] += `?${key}=${pVal}`;
        } else {
            cmd.push(`${key}=${pVal}`);
        }
    }

    return cmd.join("&");
}

/** Build a full `/login` GET command, encoding the supplied params excluding DISCONNECT (and SUBMIT) */
const buildHttpLoginCmd = (params = {}) => {
    const cmd = [];
    let prms = deepCopy(params);

    if ("DISCONNECT" in prms && prms.DISCONNECT === "yes") {
        // Disconnect - throw away any other parameters
        prms = { "DISCONNECT": "yes" };
    } else {
        // Login / Change Password - add the submit param
        prms.SUBMIT = "yes";
    }

    return buildHttpCmd(httpCmd.login, prms, [], ["DISCONNECT", "SUBMIT"]);
}

/** Build a full `/files` GET command, encoding all the supplied params excluding `action` */
const buildHttpFilesCmd = (params = {}) => buildHttpCmd(httpCmd.files, params, [], ["action"]);

/** Build a full `/upload` GET command, encoding the supplied `name`, `newname` and `path` values */
const buildHttpFileCmd = (params = { action: "", path: "", filename: "" }) => {
    const prms = deepCopy(params);
    // `path` is special, it always goes into the command, and it always goes first
    const path = encodeURIComponent(getParam(prms, "path", files_currentPath()));

    // Remove path from the params
    prms.path = undefined;

    return buildHttpCmd(`${httpCmd.fileUpload}?path=${path}`, prms, ["name", "newname"]);
}

/** Build a simple file GET command. For some reason the filename is not encoded */
const buildHttpFileGetCmd = (filename) => `${httpCmd.fileGet}${filename}`;

/** Build either form of the `command` GET command, fully encoding the supplied `cmd` value.
 * * Note: this includes replacing '#', because '#' is not encoded by `encodeURIComponent`.
 */
// No PAGEID: only [ESP...] and plain= requests use this now, and the firmware runs those in the
// web server and answers in the HTTP body. Line commands go over the WebSocket (wscommands.js).
const buildHttpCommandCmd = (cmdType, cmd) => `${httpCmd.command}?${cmdType}=${encodeURIComponent(cmd).replace("#", "%23")}`;

/** Build the supplied data into a blob, then a file, ready for inclusion as form data */
const BuildFormDataFiles = (filename, filedata, options) => {
    const blob = new Blob(filedata, options);
    return new File([blob], filename);
}

/** Check if a file exists in the root directory by listing files
 * @param {string} filename - Name of the file to check
 * @param {function} successCallback - Called with true if file exists
 * @param {function} errorCallback - Called with false if check fails
 */
const checkFileExists = (filename, successCallback, errorCallback) => {
    const cmd = buildHttpFilesCmd({ action: "list", path: "/" });
    SendGetHttp(
        cmd,
        (response) => {
            try {
                const data = JSON.parse(response);
                const exists = data.files && data.files.some(file => file.name === filename);
                successCallback(exists);
            } catch (e) {
                console.error("Error parsing file list:", e);
                errorCallback();
            }
        },
        (error_code, response) => {
            console.error("Error checking if file exists:", error_code, response);
            errorCallback();
        }
    );
}
