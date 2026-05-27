// Check for Updates dialog
// Fetches the latest release from GitHub for the configured update stream,
// then downloads and installs firmware.bin and index.html.gz if an update is available.

const GITHUB_REPO = "MaslowCNC/Maslow_4";
const GITHUB_API_BASE = `https://api.github.com/repos/${GITHUB_REPO}/releases`;

let checkUpdates_latestRelease = null;
let checkUpdates_ongoing = false;

/** Open the Check for Updates dialog */
const checkupdatesdlg = () => {
    const modal = setactiveModal("checkupdatesdlg.html");
    if (modal == null) {
        return;
    }

    id("checkUpdatesDlgCancel").addEventListener("click", closeCheckUpdatesDialog);
    id("checkUpdatesDlgClose").addEventListener("click", closeCheckUpdatesDialog);
    id("checkUpdatesDlgCheck").addEventListener("click", checkForUpdates);
    id("checkUpdatesDlgInstall").addEventListener("click", installUpdate);

    resetCheckUpdatesDialog();
    showModal();

    // Automatically check for updates when dialog opens
    checkForUpdates();
};

function closeCheckUpdatesDialog() {
    if (checkUpdates_ongoing) {
        alertdlg(
            translate_text_item("Busy..."),
            translate_text_item("Update is in progress, please wait.")
        );
        return;
    }
    closeModal("cancel");
}

function resetCheckUpdatesDialog() {
    checkUpdates_latestRelease = null;
    setHTML("checkupdates_status", translate_text_item("Checking for updates..."));
    displayNone("checkupdates_info");
    displayNone("checkupdates_progress");
    displayNone("checkUpdatesDlgInstall");
    displayBlock("checkUpdatesDlgCheck");
}

/** Return the configured update stream from preferences */
function getUpdateStream() {
    return GetPrefOrDefault("update_stream") || "release";
}

/** Select the best release from the list based on the configured stream.
 *
 * Tag convention (see .github/workflows in PR #975):
 *   nightly      — rolling pre-release published daily; tag_name is literally "nightly"
 *   experimental — versioned pre-release; tag_name matches v*-exp* (e.g. v1.2.3-exp.1)
 *   release      — stable full release; tag_name is bare semver (e.g. v1.2.3)
 */
function selectReleaseForStream(releases, stream) {
    if (!releases || releases.length === 0) {
        return null;
    }
    let candidates;
    switch (stream) {
        case "nightly":
            // The nightly workflow always recreates a single rolling release whose
            // tag_name is the literal string "nightly".
            candidates = releases.filter(r => r.tag_name === "nightly" && !r.draft);
            break;
        case "experimental":
            // Versioned pre-releases (v*-exp.*) — exclude the rolling nightly tag.
            candidates = releases.filter(
                r => r.prerelease && !r.draft && r.tag_name !== "nightly"
            );
            break;
        case "release":
        default:
            // Stable, non-prerelease only.
            candidates = releases.filter(r => !r.prerelease && !r.draft);
            break;
    }
    // GitHub returns releases newest-first; pick the first match.
    return candidates.length > 0 ? candidates[0] : null;
}

/** Return true if latestTag is newer than currentVersion */
function isNewerVersion(currentVersion, latestTag) {
    // Normalize: strip leading 'v', convert to lower-case, drop pre-release suffix for comparison
    const normalize = (v) => {
        // Strip leading 'v', lowercase, then remove any pre-release suffix (e.g. '-beta', '-rc1')
        return v.replace(/^v/i, "").trim().toLowerCase().replace(/-.*$/, "");
    };
    const cur = normalize(currentVersion || "");
    const latest = normalize(latestTag || "");
    if (!cur || !latest) {
        return (latest !== cur);
    }
    if (cur === latest) {
        return false;
    }
    // Numeric semver comparison
    const toNum = (s) => s.split(".").map(p => parseInt(p, 10) || 0);
    const curParts = toNum(cur);
    const latestParts = toNum(latest);
    const len = Math.max(curParts.length, latestParts.length);
    for (let i = 0; i < len; i++) {
        const c = curParts[i] || 0;
        const l = latestParts[i] || 0;
        if (l > c) return true;
        if (l < c) return false;
    }
    return false;
}

/** Look up an asset by name (case-insensitive) in a release */
function findAsset(release, name) {
    if (!release || !release.assets) return null;
    return release.assets.find(
        a => a.name.toLowerCase() === name.toLowerCase()
    ) || null;
}

/** Fetch releases from GitHub and update the dialog */
function checkForUpdates() {
    displayNone("checkupdates_info");
    displayNone("checkUpdatesDlgInstall");
    setHTML("checkupdates_status", translate_text_item("Checking for updates..."));
    displayBlock("checkUpdatesDlgCheck");

    const stream = getUpdateStream();

    fetch(GITHUB_API_BASE, {
        headers: {
            "Accept": "application/vnd.github+json",
            "User-Agent": "MaslowCNC-WebUI"
        }
    })
        .then(response => {
            if (!response.ok) {
                throw new Error(`GitHub API error: ${response.status}`);
            }
            return response.json();
        })
        .then(releases => {
            const release = selectReleaseForStream(releases, stream);
            handleReleaseFetched(release, stream);
        })
        .catch(err => {
            console.error("Error checking for updates:", err);
            setHTML(
                "checkupdates_status",
                `<span style="color:red;">${translate_text_item("Failed to check for updates:")} ${err.message}</span>`
            );
        });
}

function streamLabel(stream) {
    switch (stream) {
        case "experimental": return translate_text_item("Experimental (pre-release)");
        case "nightly": return translate_text_item("Nightly (latest)");
        default: return translate_text_item("Release (stable)");
    }
}

function handleReleaseFetched(release, stream) {
    checkUpdates_latestRelease = release;

    setHTML("checkupdates_stream", streamLabel(stream));

    if (!release) {
        setHTML(
            "checkupdates_status",
            translate_text_item("No releases found for the selected update stream.")
        );
        return;
    }

    const latestTag = release.tag_name || "";
    const currentFw = fw_version || "";
    const currentUi = web_ui_version || "";

    // Display versions, flagging each if it's behind
    const fwUpdateAvailable = isNewerVersion(currentFw, latestTag);
    const uiUpdateAvailable = isNewerVersion(currentUi, latestTag);

    const updateBadge = (label) =>
        `${label} <span style="color:orange;">(&#x25B2; update available)</span>`;

    const fwLabel = currentFw || translate_text_item("unknown");
    const uiLabel = currentUi || translate_text_item("unknown");

    setHTML("checkupdates_fw_version", fwUpdateAvailable ? updateBadge(fwLabel) : fwLabel);
    setHTML("checkupdates_ui_version", uiUpdateAvailable ? updateBadge(uiLabel) : uiLabel);
    setHTML("checkupdates_latest_version", latestTag);
    displayBlock("checkupdates_info");

    // Show release notes if present
    if (release.body && release.body.trim()) {
        setHTML("checkupdates_notes", release.body.trim());
        displayBlock("checkupdates_notes_section");
    } else {
        displayNone("checkupdates_notes_section");
    }

    const hasFirmware = !!findAsset(release, "firmware.bin");
    const hasUI = !!findAsset(release, "index.html.gz");

    if (!hasFirmware && !hasUI) {
        setHTML(
            "checkupdates_status",
            translate_text_item("Release found but no installable assets (firmware.bin / index.html.gz) are attached.")
        );
        return;
    }

    if (fwUpdateAvailable || uiUpdateAvailable) {
        setHTML(
            "checkupdates_status",
            `<span style="color:green;">${translate_text_item("A new update is available!")}</span>`
        );
        displayBlock("checkUpdatesDlgInstall");
    } else {
        setHTML(
            "checkupdates_status",
            translate_text_item("Your firmware is up to date.")
        );
    }
}

/** Download a file from a URL and return a Blob */
async function downloadAsset(url, onProgress) {
    const response = await fetch(url, { headers: { "Accept": "application/octet-stream" } });
    if (!response.ok) {
        throw new Error(`Download failed (${response.status} ${response.statusText}): ${url}`);
    }

    // Stream the download so we can report progress
    const contentLength = response.headers.get("Content-Length");
    const total = contentLength ? parseInt(contentLength, 10) : 0;
    const reader = response.body.getReader();
    const chunks = [];
    let loaded = 0;

    while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        loaded += value.length;
        if (total > 0 && onProgress) {
            onProgress(loaded, total);
        }
    }

    return new Blob(chunks);
}

/** Wrap SendFileHttp in a Promise */
function sendFileHttpPromise(url, formData, onProgress, context) {
    return new Promise((resolve, reject) => {
        SendFileHttp(
            url,
            formData,
            onProgress,
            () => resolve(),
            (error_code, response) => {
                const ctx = context ? `${context}: ` : "";
                reject(new Error(`${ctx}HTTP ${error_code} — ${response}`));
            }
        );
    });
}

/** Wrap checkFileExists in a Promise — resolves with true/false */
function checkFileExistsPromise(filename) {
    return new Promise((resolve) => {
        checkFileExists(filename, resolve, () => resolve(false));
    });
}

/** Save a blob to the ESP32 filesystem via /files */
function saveToFilesystem(blob, filename, onProgress) {
    const file = new File([blob], filename);
    const formData = BuildFileUploadFormData("/", [file]);
    return sendFileHttpPromise(httpCmd.files, formData, onProgress, `save ${filename}`);
}

/** Upload a blob to the firmware-update endpoint (/updatefw) — triggers reboot */
function flashFirmware(blob, onProgress) {
    const file = new File([blob], "firmware.bin");
    const formData = BuildFileUploadFormData("/", [file]);
    return sendFileHttpPromise(httpCmd.fwUpdate, formData, onProgress, "flash firmware");
}

/** Install the update: download both assets, save to filesystem, verify, then flash */
function installUpdate() {
    if (!checkUpdates_latestRelease) {
        alertdlg(translate_text_item("Error"), translate_text_item("No release selected."));
        return;
    }
    confirmdlg(
        translate_text_item("Please confirm"),
        translate_text_item("Install Update?") + "\n" + checkUpdates_latestRelease.tag_name,
        startInstallUpdate
    );
}

async function startInstallUpdate(response) {
    if (response !== "yes") return;

    const release = checkUpdates_latestRelease;
    const fwAsset = findAsset(release, "firmware.bin");
    const uiAsset = findAsset(release, "index.html.gz");

    if (!fwAsset && !uiAsset) {
        alertdlg(translate_text_item("Error"), translate_text_item("No installable assets found in this release."));
        return;
    }

    checkUpdates_ongoing = true;
    displayNone("checkUpdatesDlgInstall");
    displayNone("checkUpdatesDlgCheck");
    displayBlock("checkupdates_progress");
    setHTML("checkupdates_status", translate_text_item("Installing update..."));

    // Disable ping monitoring during install
    disablePingForUpload();

    try {
        let fwBlob = null;
        let uiBlob = null;

        // Phase 1: Download firmware.bin (0–20 %)
        if (fwAsset) {
            setHTML("checkupdates_step", `${translate_text_item("Downloading")} firmware.bin...`);
            fwBlob = await downloadAsset(fwAsset.browser_download_url, (loaded, total) => {
                setProgress(Math.round((loaded / total) * 20));
            });
        }

        // Phase 2: Download index.html.gz (20–40 %)
        if (uiAsset) {
            setHTML("checkupdates_step", `${translate_text_item("Downloading")} index.html.gz...`);
            uiBlob = await downloadAsset(uiAsset.browser_download_url, (loaded, total) => {
                setProgress(20 + Math.round((loaded / total) * 20));
            });
        }

        // Phase 3: Save firmware.bin to filesystem (40–55 %)
        if (fwBlob) {
            setHTML("checkupdates_step", `${translate_text_item("Saving")} firmware.bin...`);
            await saveToFilesystem(fwBlob, "firmware.bin", (evt) => {
                if (evt.lengthComputable) {
                    setProgress(40 + Math.round((evt.loaded / evt.total) * 15));
                }
            });
        }

        // Phase 4: Save index.html.gz to filesystem — this installs the UI (55–70 %)
        if (uiBlob) {
            setHTML("checkupdates_step", `${translate_text_item("Installing UI")}...`);
            await saveToFilesystem(uiBlob, "index.html.gz", (evt) => {
                if (evt.lengthComputable) {
                    setProgress(55 + Math.round((evt.loaded / evt.total) * 15));
                }
            });
        }

        // Phase 5: Verify both files exist on filesystem (70–75 %)
        setHTML("checkupdates_step", translate_text_item("Verifying downloads..."));
        setProgress(70);

        if (fwBlob) {
            const fwExists = await checkFileExistsPromise("firmware.bin");
            if (!fwExists) {
                throw new Error(`${translate_text_item("Verification failed:")} firmware.bin`);
            }
        }

        if (uiBlob) {
            const uiExists = await checkFileExistsPromise("index.html.gz");
            if (!uiExists) {
                throw new Error(`${translate_text_item("Verification failed:")} index.html.gz`);
            }
        }

        setProgress(75);

        // Phase 6: Flash firmware (75–100 %) — triggers reboot
        if (fwBlob) {
            setHTML("checkupdates_step", `${translate_text_item("Installing firmware")}...`);
            await flashFirmware(fwBlob, (evt) => {
                if (evt.lengthComputable) {
                    setProgress(75 + Math.round((evt.loaded / evt.total) * 25));
                }
            });
        }

        finishInstallUpdate();
    } catch (err) {
        checkUpdates_ongoing = false;
        restorePingAfterUpload();
        displayBlock("checkUpdatesDlgCheck");
        setHTML(
            "checkupdates_status",
            `<span style="color:red;">${translate_text_item("Update failed:")} ${err.message}</span>`
        );
        console.error("Update failed:", err);
    }
}

function finishInstallUpdate() {
    checkUpdates_ongoing = false;
    restorePingAfterUpload();
    setHTML("checkupdates_step", translate_text_item("Update installed. Restarting..."));
    setProgress(100);
    setHTML("checkupdates_status", translate_text_item("Restarting, please wait...."));

    let i = 0;
    const prg = id("checkupdates_prg");
    prg.max = 30;
    prg.value = 0;
    const interval = setInterval(() => {
        i++;
        prg.value = i;
        setHTML("checkupdates_step",
            `${translate_text_item("Restarting, please wait....")} ${31 - i} ${translate_text_item("seconds")}`
        );
        if (i >= 30) {
            clearInterval(interval);
            location.reload();
        }
    }, 1000);
}

function setProgress(pct) {
    const prg = id("checkupdates_prg");
    prg.max = 100;
    prg.value = pct;
    setHTML("checkupdates_pct", `${pct}%`);
}
