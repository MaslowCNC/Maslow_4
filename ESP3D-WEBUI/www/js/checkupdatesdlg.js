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

/**
 * The release assets (firmware.bin / index.html.gz) cannot be downloaded by the
 * browser directly from a GitHub Release: those URLs redirect to
 * release-assets.githubusercontent.com, which sends no CORS headers. The ESP32
 * itself has too little free RAM to perform the HTTPS/TLS download.
 *
 * Instead, a GitHub Action mirrors each release's assets into the repo under
 * web-assets/<safe_tag>/ (and web-assets/latest/), where safe_tag is a sanitized
 * form of the release tag_name (see .github/workflows/release.yml). raw.githubusercontent.com serves
 * those files WITH "access-control-allow-origin: *", so the browser can fetch
 * them cross-origin and then upload them to the device over the existing,
 */
const MIRROR_BRANCH = "Maslow-Main";
const MIRROR_BASE = `https://raw.githubusercontent.com/${GITHUB_REPO}/${MIRROR_BRANCH}/web-assets`;

/** Download a URL as a Blob, reporting progress via onProgress(loaded, total). */
async function downloadBlob(url, onProgress) {
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) {
        throw new Error(`HTTP ${response.status} ${response.statusText}`);
    }
    const total = parseInt(response.headers.get("Content-Length") || "0", 10);
    if (!response.body || !response.body.getReader) {
        return await response.blob();  // streaming unsupported — fall back
    }
    const reader = response.body.getReader();
    const chunks = [];
    let loaded = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        chunks.push(value);
        loaded += value.length;
        if (total && onProgress) {
            onProgress(loaded, total);
        }
    }
    return new Blob(chunks);
}

/**
 * Fetch a mirrored release asset from raw.githubusercontent.com. Tries the
 * version-specific path (web-assets/<tag>/) first, then falls back to "latest".
 */
async function fetchMirrorAsset(tag, filename, onProgress) {
    const candidates = [];
    if (tag) {
        candidates.push(`${MIRROR_BASE}/${encodeURIComponent(tag)}/${filename}`);
    }
    candidates.push(`${MIRROR_BASE}/latest/${filename}`);

    let lastErr = null;
    for (const url of candidates) {
        try {
            return await downloadBlob(url, onProgress);
        } catch (err) {
            lastErr = err;
        }
    }
    throw new Error(`${translate_text_item("Download failed:")} ${filename} (${lastErr ? lastErr.message : "not found"})`);
}

/** Wrap SendFileHttp in a Promise that resolves on success / rejects on error. */
function sendFileHttpPromise(endpoint, formData, onProgress, context) {
    return new Promise((resolve, reject) => {
        SendFileHttp(
            endpoint,
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

/**
 * Upload a blob to the device.
 *   endpoint = httpCmd.files    -> save to LocalFS root (e.g. index.html.gz)
 *   endpoint = httpCmd.fwUpdate -> OTA firmware flash (triggers reboot)
 */
function uploadBlobToDevice(blob, filename, endpoint, onProgress, context) {
    const file = new File([blob], filename);
    const formData = BuildFileUploadFormData("/", [file]);
    return sendFileHttpPromise(endpoint, formData, onProgress, context);
}

/** Install the update: download mirrored assets in the browser, then upload to the device */
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
    const tag = release.tag_name;
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
        let uiBlob = null;
        let fwBlob = null;

        // Phase 1: Download index.html.gz from the CORS-enabled mirror (0–25 %)
        if (uiAsset) {
            setHTML("checkupdates_step", `${translate_text_item("Downloading")} index.html.gz...`);
            uiBlob = await fetchMirrorAsset(tag, "index.html.gz", (loaded, total) => {
                setProgress(Math.round((loaded / total) * 25));
            });
        }

        // Phase 2: Download firmware.bin from the mirror (25–50 %)
        if (fwAsset) {
            setHTML("checkupdates_step", `${translate_text_item("Downloading")} firmware.bin...`);
            fwBlob = await fetchMirrorAsset(tag, "firmware.bin", (loaded, total) => {
                setProgress(25 + Math.round((loaded / total) * 25));
            });
        }

        // Phase 3: Install the web UI to the filesystem (50–65 %)
        // Done before the firmware flash because the firmware flash reboots the device.
        if (uiBlob) {
            setHTML("checkupdates_step", `${translate_text_item("Installing UI")}...`);
            await uploadBlobToDevice(uiBlob, "index.html.gz", httpCmd.files, (evt) => {
                if (evt.lengthComputable) {
                    setProgress(50 + Math.round((evt.loaded / evt.total) * 15));
                }
            }, "install UI");
            setProgress(65);
        }

        // Phase 4: Flash the firmware (65–100 %) — triggers reboot on success
        if (fwBlob) {
            setHTML("checkupdates_step", `${translate_text_item("Installing firmware")}...`);
            await uploadBlobToDevice(fwBlob, "firmware.bin", httpCmd.fwUpdate, (evt) => {
                if (evt.lengthComputable) {
                    setProgress(65 + Math.round((evt.loaded / evt.total) * 35));
                }
            }, "flash firmware");
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
