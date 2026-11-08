// Firmware card: installed version, update checks and installs from the release
// manifest, and manual upload of a signed .ota.bin. The file checks and messages
// are the same as web-interface/js/firmware.js; only the layout code changed.
// Update file layout: [firmware][info block, 128 bytes][signature, 512 bytes]
const INFO_SIZE = 128;
const SIGNATURE_SIZE = 512;
const INFO_FORMAT_VERSION = 1;
const ESP_IMAGE_MAGIC = 0xe9;
const PARTITION_TABLE_OFFSET = 0x8000;

var device = null;
var selectedFile = null;
var updateStatus = null;
var installing = false;
var uploading = false;

function loadFirmware() {
  api("/api/status").then(function (r) {
    if (!r) return;
    let res = r.body;
    device = res;
    $("fw-installed").textContent = "v" + res.version + (res.dev ? " (dev)" : "");
    $("fw-build").textContent = res.build + (res.commit && res.commit !== "dev" ? " (" + res.commit + ")" : "");
    $("fw-target").textContent = res.target;
    showNote("fw-mismatch", mismatchText(res));
    $("fw-rollback").style.display = res.rollback_available ? "" : "none";
    $("fw-previous").textContent = "Previous version: v" + res.previous_version;
    if (selectedFile) checkSelectedFile();
  });
}

function readText(view, offset, size) {
  let text = "";
  for (let i = 0; i < size; i++) {
    let c = view.getUint8(offset + i);
    if (c === 0) break;
    text += String.fromCharCode(c);
  }
  return text;
}

function toHex(view, offset, size) {
  let hex = "";
  for (let i = 0; i < size; i++) {
    hex += view.getUint8(offset + i).toString(16).padStart(2, "0");
  }
  return hex;
}

function compareVersions(a, b) {
  for (let i = 0; i < 3; i++) {
    if (a[i] !== b[i]) return a[i] > b[i] ? 1 : -1;
  }
  return 0;
}

// "esp32-4mb-relay" -> "esp32, 4 MB"
function describeTarget(target) {
  let parts = target.split("-");
  let flash = parts[1] ? parts[1].replace(/mb$/, " MB") : "";
  return flash ? parts[0] + ", " + flash : parts[0];
}

function readSlice(file, start, end) {
  return new Promise(function (resolve, reject) {
    let reader = new FileReader();
    reader.onload = function () {
      resolve(new DataView(reader.result));
    };
    reader.onerror = reject;
    reader.readAsArrayBuffer(file.slice(start, end));
  });
}

/**
 * Applies the same rules as the device (except the signature) to a picked file.
 * Resolves to {error} when it must be blocked, otherwise {version, target, warnings, params}.
 */
async function analyzeFile(file) {
  let expected = "Use a file ending in -" + device.home_target + ".ota.bin.";
  let head = await readSlice(file, 0, PARTITION_TABLE_OFFSET + 2);

  // A factory image holds the bootloader and partition table; classic ESP32 ones start with padding
  let hasPartitionTable =
    head.byteLength >= PARTITION_TABLE_OFFSET + 2 &&
    head.getUint8(PARTITION_TABLE_OFFSET) === 0xaa &&
    head.getUint8(PARTITION_TABLE_OFFSET + 1) === 0x50;
  if ((head.byteLength > 0 && head.getUint8(0) === 0xff) || hasPartitionTable) {
    return {
      error:
        "This is a factory image for USB flashing, not an update. Use the file ending in -" +
        device.home_target +
        ".ota.bin.",
    };
  }
  if (head.byteLength === 0 || head.getUint8(0) !== ESP_IMAGE_MAGIC) {
    return { error: "This file is not a firmware update. " + expected };
  }
  if (file.size <= INFO_SIZE + SIGNATURE_SIZE) {
    return { error: "This file isn't signed for this project." };
  }

  let infoStart = file.size - SIGNATURE_SIZE - INFO_SIZE;
  let info = await readSlice(file, infoStart, infoStart + INFO_SIZE);
  if (
    readText(info, 0, 4) !== "WOTA" ||
    info.getUint16(4, true) !== INFO_FORMAT_VERSION ||
    info.getUint32(6, true) !== infoStart
  ) {
    return { error: "This file isn't signed for this project." };
  }

  let version = [info.getUint16(10, true), info.getUint16(12, true), info.getUint16(14, true)];
  let chipId = info.getUint16(16, true);
  let layout = toHex(info, 18, 32);
  let target = readText(info, 50, 32);
  let project = readText(info, 82, 32);

  if (project !== device.project) {
    return { error: "This file is for another project (" + project + ")." };
  }
  if (chipId !== device.chip_id || layout !== device.layout) {
    return {
      error:
        "This file is for " +
        target +
        (chipId === device.chip_id ? " with another flash layout" : "") +
        " and cannot run on this device (" +
        describeTarget(device.target) +
        "). " +
        expected,
    };
  }

  let warnings = [];
  let params = [];
  if (target !== device.target) {
    if (target === device.home_target) {
      warnings.push("This restores the device's original configuration (" + target + ").");
    } else {
      warnings.push(
        "This file is for " + target + ". This device runs " + device.target +
          ". The correct file for this device ends in -" + device.target +
          ".ota.bin. If you continue, future updates will follow " + target + "."
      );
    }
    params.push("allow_target_change=1");
  }

  let installed = device.version.split(".").map(Number);
  let order = compareVersions(version, installed);
  let fileVersion = version.join(".");
  if (order < 0) {
    warnings.push(
      "This file is v" + fileVersion + ". Installed: v" + device.version + ". This is a downgrade, continue?"
    );
    params.push("allow_downgrade=1");
  } else if (order === 0) {
    warnings.push("This file is v" + fileVersion + ", the version already installed. Install it again?");
    params.push("allow_downgrade=1");
  }

  return { version: fileVersion, target: target, warnings: warnings, params: params };
}

function setFirmwareMessage(text, kind) {
  $("fw-picked").style.display = text ? "" : "none";
  $("fw-message").textContent = text;
  $("fw-message").className = "grow sm " + (kind === "error" ? "err" : kind || "muted");
}

function updateRunning() {
  let state = updateStatus ? updateStatus.state : "idle";
  return uploading || installing || ["downloading", "receiving", "installing", "rebooting"].includes(state);
}

// Called by app.js when the "Finishing update" state changes
function refreshFirmwareButtons() {
  let busy = updateRunning();
  let checking = updateStatus && updateStatus.check.state === "checking";
  $("fw-file").disabled = busy;
  $("drop").style.pointerEvents = busy ? "none" : "";
  $("fw-upload").disabled = busy || updateFinishing || !selectedFile || !selectedFile.ok;
  $("fw-upload").style.display = selectedFile && selectedFile.ok && !busy ? "" : "none";
  $("fw-check").disabled = busy || checking;
  $("fw-install").disabled = busy || updateFinishing;
  $("fw-rollback-button").disabled = busy || updateFinishing;
}

function setUploading(value) {
  uploading = value;
  $("fw-progress").style.display = value ? "" : "none";
  refreshFirmwareButtons();
}

async function checkSelectedFile() {
  let file = selectedFile.file;
  selectedFile.ok = false;
  refreshFirmwareButtons();
  if (!device) {
    setFirmwareMessage("Waiting for the device...");
    return;
  }
  let result;
  try {
    result = await analyzeFile(file);
  } catch (e) {
    setFirmwareMessage("Could not read " + file.name, "error");
    return;
  }
  if (!selectedFile || selectedFile.file !== file) return;
  if (result.error) {
    setFirmwareMessage(file.name + ": " + result.error, "error");
    return;
  }
  selectedFile.result = result;
  selectedFile.ok = true;
  let summary = file.name + ": v" + result.version + " for " + result.target + ".";
  if (result.warnings.length) {
    setFirmwareMessage(summary + " " + result.warnings.join(" "), "warn");
  } else {
    setFirmwareMessage(summary);
  }
  refreshFirmwareButtons();
}

function pickFirmware(file) {
  selectedFile = file ? { file: file, ok: false } : null;
  if (!selectedFile) {
    setFirmwareMessage("");
    refreshFirmwareButtons();
    return;
  }
  checkSelectedFile();
}

function uploadFirmware() {
  if (!selectedFile || !selectedFile.ok) return;
  let result = selectedFile.result;
  ask({
    title: "Install v" + result.version + " (" + result.target + ")?",
    text: result.warnings.join("\n\n"),
    ok: "Install",
    onOk: function () {
      sendFirmware(selectedFile.file, result.params);
    },
  });
}

function sendFirmware(file, params) {
  let request = new XMLHttpRequest();
  request.open("POST", "/api/update" + (params.length ? "?" + params.join("&") : ""), true);
  request.setRequestHeader("Content-Type", "application/octet-stream");

  setUploading(true);
  $("fw-progress").value = 0;
  setFirmwareMessage("Uploading...");

  request.upload.onprogress = function (event) {
    if (event.lengthComputable) {
      let percent = Math.floor((event.loaded * 100) / event.total);
      $("fw-progress").value = percent;
      setFirmwareMessage("Uploading... " + percent + "%");
    }
  };
  request.upload.onload = function () {
    setFirmwareMessage("Verifying and installing...");
  };
  request.onload = function () {
    let response = {};
    try {
      response = JSON.parse(this.responseText);
    } catch (e) {}
    if (this.status >= 200 && this.status < 300) {
      setFirmwareMessage("Update installed. Rebooting, the page reloads when the device is back.");
      toast("Update installed, rebooting...", 10000);
      reloadAfterReboot();
      return;
    }
    let message = response.message || "Update failed (" + this.status + " " + this.statusText + ")";
    setFirmwareMessage(message, "error");
    toast(message, 6000);
    setUploading(false);
  };
  request.onerror = function () {
    setFirmwareMessage("Upload failed: the connection to the device was lost", "error");
    setUploading(false);
  };
  request.send(file);
}

// Polls /api/update/status every second until done(status) returns true
function pollStatus(done) {
  api("/api/update/status", { quiet: true, timeout: 4000 }).then(function (r) {
    if (r) {
      onUpdateStatus(r.body);
      if (done(r.body)) return;
    }
    setTimeout(function () {
      pollStatus(done);
    }, 1000);
  });
}

function renderCheck(check) {
  let text = "";
  let kind = "muted";
  switch (check.state) {
    case "never":
      text = "Not checked yet.";
      break;
    case "checking":
      text = "Checking for updates...";
      break;
    case "up_to_date":
      text = "Up to date.";
      break;
    case "available":
      break;
    case "no_update":
      text = check.message;
      break;
    case "needs_manual":
      text = check.message;
      kind = "warn";
      break;
    default:
      text = check.message || "The update check failed.";
      kind = "err";
  }
  $("fw-check-message").textContent = text;
  $("fw-check-message").className = "grow sm " + kind;

  let available = check.state === "available";
  $("fw-available").style.display = available ? "" : "none";
  $("fw-badge").textContent = available ? "Update available" : check.state === "up_to_date" ? "Up to date" : "";
  $("fw-badge").className = "chip" + (available || check.state === "up_to_date" ? " ok" : "");
  if (available) {
    $("fw-available-title").textContent = "v" + check.version + " is available";
    // Release notes are plain text: never parsed as HTML
    $("fw-notes").textContent = check.notes || "No release notes.";
  }
}

// Called by api.js on page load and by the polls below
function onUpdateStatus(res) {
  updateStatus = res;
  renderCheck(res.check);
  if (!installing && res.source === "pull" && (res.state === "downloading" || res.state === "installing")) {
    installing = true;
    pollStatus(trackInstall);
  }
  refreshFirmwareButtons();
}

function checkForUpdates() {
  api("/api/update/check", { method: "POST" }).then(function (r) {
    if (!r) return;
    pollStatus(function (res) {
      return res.check.state !== "checking";
    });
  });
}

function trackInstall(res) {
  let progress = $("fw-progress");
  switch (res.state) {
    case "downloading":
      progress.style.display = "";
      progress.value = res.progress;
      setFirmwareMessage("Downloading... " + res.progress + "%");
      return false;
    case "installing":
      setFirmwareMessage("Verifying and installing...");
      return false;
    case "rebooting":
      setFirmwareMessage("Update installed. Rebooting, the page reloads when the device is back.");
      toast("Update installed, rebooting...", 10000);
      reloadAfterReboot();
      return true;
    default:
      installing = false;
      progress.style.display = "none";
      setFirmwareMessage(res.message || "The update failed.", "error");
      refreshFirmwareButtons();
      return true;
  }
}

function installUpdate() {
  let check = updateStatus && updateStatus.check;
  if (!check || check.state !== "available") return;
  ask({
    title: "Install v" + check.version + "?",
    text: "The device downloads the update, installs it and reboots. Settings are kept.",
    ok: "Install",
    onOk: function () {
      api("/api/update/install", { method: "POST" }).then(function (r) {
        if (!r) return;
        installing = true;
        refreshFirmwareButtons();
        setFirmwareMessage("Downloading...");
        pollStatus(trackInstall);
      });
    },
  });
}

function rollBack() {
  let version = device && device.previous_version;
  if (!version) return;
  ask({
    title: "Roll back to v" + version + "?",
    text: "The device reboots into the version it ran before the last update. Settings are kept.",
    ok: "Roll back",
    danger: true,
    onOk: function () {
      api("/api/update/rollback", { method: "POST" }).then(function (r) {
        if (!r) return;
        setFirmwareMessage(r.body.message + ". The page reloads when the device is back.");
        toast(r.body.message, 10000);
        reloadAfterReboot();
      });
    },
  });
}

// Drag and drop onto the upload box
document.addEventListener("DOMContentLoaded", function () {
  let drop = $("drop");
  drop.ondragover = function (e) {
    e.preventDefault();
    drop.classList.add("over");
  };
  drop.ondragleave = function () {
    drop.classList.remove("over");
  };
  drop.ondrop = function (e) {
    e.preventDefault();
    drop.classList.remove("over");
    if (!updateRunning()) pickFirmware(e.dataTransfer.files[0]);
  };
});
