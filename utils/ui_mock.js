// Fake device for utils/ui_preview.py --mock. It replaces js/api.js and the page
// scripts (home.js, networks.js, settings.js, firmware.js) with the same functions
// working on fake data, so every page and state can be tried without hardware.
// The Demo states panel (bottom left) switches the fake device's state; choices
// are kept in localStorage ("mock-state"). Keep it in step with the page scripts.

// Fake versions around the one the pages were built with (VERSION)
const V_NOW = document.body.dataset.version || "1.0.0";
// The next minor release after v
function nextMinor(v) {
  let p = v.split(".").map(Number);
  return p[0] + "." + (p[1] + 1) + ".0";
}
function prevMinor(v) {
  let p = v.split(".").map(Number);
  return p[0] + "." + Math.max(p[1] - 1, 0) + ".0";
}

const DEMO_KEY = "mock-state";
const DEMO_DEFAULTS = {
  ap: true,
  sta: true,
  ssid: "HomeNet",
  update: false,
  devUpdate: false,
  dev: false,
  finishing: false,
  rollback: false,
  offline: false,
  // The running build target and the one the device was set up with (/api/status
  // target / home_target). The "psram" and "mismatch" toggles are read from these.
  target: "esp32-8mb-psram",
  home: "esp32-8mb-psram",
  // Not on the panel: changed by the pages' own actions
  installed: null, // running version (null = VERSION)
  previous: null, // other slot {version, target} (null = the release before, same target)
  finishingAt: null, // when the confirmation window ends (ms timestamp)
  settings: null, // saved Settings-page values over SETTINGS
};
const DEMO_LABELS = {
  ap: "Access point on",
  sta: "Wi-Fi connected",
  update: "Update available",
  devUpdate: "Update is a dev build",
  dev: "Running a dev build",
  finishing: "Finishing update",
  mismatch: "Config mismatch",
  rollback: "Rollback available",
  offline: "Device offline",
  psram: "Running with PSRAM",
};

// Only esp32-8mb-psram turns PSRAM on, and the device only changes target over the
// air within one flash layout (a USB flash resets the home target), so a mismatch is
// always between the two 8 MB targets.
const LAYOUT = { "esp32-4mb": "4mb", "esp32-8mb": "8mb", "esp32-8mb-psram": "8mb" };
const PSRAM_TARGET = "esp32-8mb-psram";

// The target/home pair for the panel's "psram" and "mismatch" toggles
function toggleTargets(s, psram, mismatch) {
  if (psram) return { target: PSRAM_TARGET, home: mismatch ? "esp32-8mb" : PSRAM_TARGET };
  if (mismatch) return { target: "esp32-8mb", home: PSRAM_TARGET };
  let t = s.target === "esp32-8mb" ? "esp32-8mb" : "esp32-4mb";
  return { target: t, home: t };
}

function demo() {
  let s;
  try {
    s = Object.assign({}, DEMO_DEFAULTS, JSON.parse(localStorage.getItem(DEMO_KEY)));
  } catch (e) {
    s = Object.assign({}, DEMO_DEFAULTS);
  }
  if (!LAYOUT[s.target] || !LAYOUT[s.home]) Object.assign(s, { target: DEMO_DEFAULTS.target, home: DEMO_DEFAULTS.home });
  s.psram = s.target === PSRAM_TARGET;
  s.mismatch = s.target !== s.home;
  return s;
}
function setDemo(patch) {
  let s = demo();
  if ("psram" in patch || "mismatch" in patch) {
    let p = "psram" in patch ? patch.psram : s.psram;
    let m = "mismatch" in patch ? patch.mismatch : s.mismatch;
    patch = Object.assign({}, patch, toggleTargets(s, p, m));
  }
  s = Object.assign(s, patch);
  delete s.psram;
  delete s.mismatch;
  try {
    localStorage.setItem(DEMO_KEY, JSON.stringify(s));
  } catch (e) {}
}

function target() {
  return demo().target;
}
function homeTarget() {
  return demo().home;
}
function installed() {
  return demo().installed || V_NOW;
}
function previous() {
  return demo().previous || { version: prevMinor(installed()), target: target() };
}
// Like firmware.js
function describeTarget(t) {
  let parts = t.split("-");
  let flash = parts[1] ? parts[1].replace(/mb$/, " MB") : "";
  return flash ? parts[0] + ", " + flash : parts[0];
}
function compareVersions(a, b) {
  a = a.split(".").map(Number);
  b = b.split(".").map(Number);
  for (let i = 0; i < 3; i++) if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
  return 0;
}
// Saved Settings-page values
function settings() {
  return Object.assign({}, SETTINGS, demo().settings);
}

const NETWORKS = [
  { ssid: "HomeNet", rssi: -48, channel: 6, enc: "WPA2", mac: "A4:2B:B0:11:9C:01" },
  { ssid: "HomeNet-Guest", rssi: -57, channel: 6, enc: "WPA2", mac: "A4:2B:B0:11:9C:02" },
  { ssid: "Office 5th floor", rssi: -66, channel: 11, enc: "WPA2-ENTERPRISE", mac: "00:1E:58:AA:10:FE" },
  { ssid: "Cafe_Free_WiFi", rssi: -74, channel: 1, enc: "OPEN", mac: "C8:3A:35:02:44:7B" },
  { ssid: "", rssi: -79, channel: 3, enc: "WPA2", mac: "F0:9F:C2:7D:01:AA", hidden: true },
  { ssid: "NETGEAR42", rssi: -86, channel: 9, enc: "WPA/WPA2", mac: "9C:3D:CF:5E:22:10" },
];

function fake(ms, fn) {
  setStatus("loading");
  setTimeout(function () {
    if (demo().offline) {
      setStatus("error");
      toast("Request failed: the device did not answer", 5000);
      return;
    }
    setStatus("connected");
    fn();
  }, ms);
}

/* ---------- Shared start-up (checkConnection + checkUpdateStatus) ---------- */

document.addEventListener("DOMContentLoaded", function () {
  buildDemoPanel();
  fake(450, function () {
    let s = demo();
    $("fw-version").textContent = "v" + installed() + (s.dev ? " (dev)" : "");
    if (s.finishing) {
      // Like confirm_in_s, counted from the (fake) boot, not from this page load
      let end = s.finishingAt || Date.now() + 48000;
      if (!s.finishingAt) setDemo({ finishingAt: end });
      let tick = function () {
        let n = Math.ceil((end - Date.now()) / 1000);
        if (n > 0) return setFinishing(n);
        clearInterval(t);
        setDemo({ finishing: false, finishingAt: null });
        setFinishing(0);
        if (document.body.dataset.page === "settings") loadFirmware();
      };
      let t = setInterval(tick, 1000);
      tick();
    } else if (s.finishingAt) {
      setDemo({ finishingAt: null });
    }
    let page = document.body.dataset.page;
    if (page === "home") drawHome();
    if (page === "networks") scan();
    if (page === "settings") {
      loadSettings();
      loadFirmware();
    }
  });
});

// Same text as api.js's mismatchText()
function mismatchText() {
  if (target() === homeTarget()) return "";
  return (
    "This device was set up as " + homeTarget() + " but is running " + target() +
    ". To fix this, upload a file ending in -" + homeTarget() + ".ota.bin from Settings."
  );
}
// What /api/update/status -> check would say. Only a dev build checks the dev manifest.
function fakeCheck(state) {
  let s = demo();
  return { state: state, version: nextMinor(installed()), dev: state === "available" && s.dev && s.devUpdate };
}

/* ---------- Home ---------- */

function statusCard(on, icon, title, subtitle, chip, rows, offText) {
  return (
    "<article class='card'><div class='head'><span class='ico" + (on ? " on" : "") + "'>" +
    svg(icon) +
    "</span><div class='grow'><h2>" + title + "</h2><p class='sm muted'>" + subtitle +
    "</p></div><span class='chip " + (on ? "ok" : "bad") + "'>" + chip + "</span></div>" +
    (on ? kvList(rows) : "<p class='muted'>" + offText + "</p>") +
    "</article>"
  );
}

function kvList(rows, cls) {
  return (
    "<dl class='kv" + (cls ? " " + cls : "") + "'>" +
    rows.map(function (r) {
      return "<div><dt>" + r[0] + "</dt><dd>" + esc(r[1]) + "</dd></div>";
    }).join("") +
    "</dl>"
  );
}

// What /api/health -> data reports about the board
const BOARD = {
  chip_info: { chip_model: "ESP32-D0WD-V3", chip_revision: 301, cores: 2, cpu_freq_mhz: 240 },
  memory: { flash_size_mb: 8, free_heap_kb: 182, psram_size_mb: 8, free_psram_kb: 4010 },
};

// Same text as home.js's memorySize() / memoryFree()
function memorySize(mem) {
  return mem.flash_size_mb + " MB flash" + (mem.psram_size_mb ? " · " + mem.psram_size_mb + " MB PSRAM" : "");
}
function memoryFree(mem) {
  if (!mem.psram_size_mb) return mem.free_heap_kb + " KB";
  return mem.free_heap_kb + " KB RAM · " + (mem.free_psram_kb / 1024).toFixed(1) + " MB PSRAM";
}

function deviceCard() {
  let chip = BOARD.chip_info;
  // Flash size comes from the image header and only esp32-8mb-psram turns PSRAM on
  let t = target();
  let mem = t === "esp32-8mb-psram"
    ? BOARD.memory
    : { flash_size_mb: LAYOUT[t] === "8mb" ? 8 : 4, free_heap_kb: 182, psram_size_mb: 0, free_psram_kb: 0 };
  return (
    "<article class='card wide'><div class='head'><span class='ico'>" + svg(CPU) +
    "</span><div class='grow'><h2>Device</h2></div></div>" +
    kvList([
      ["Chip", chip.chip_model + " v3.1"],
      ["CPU", chip.cores + " cores · " + chip.cpu_freq_mhz + " MHz"],
      ["Memory", memorySize(mem)],
      ["Free memory", memoryFree(mem)],
    ], "cols2") +
    "</article>"
  );
}

function drawHome() {
  let s = demo();
  // WiFiManager::begin() starts the access point when the saved network fails, whatever ap_status says
  let ap = s.ap || !s.sta;
  showNote("mismatch", mismatchText());
  showUpdateNote(s.update ? fakeCheck("available") : null);
  $("cards").innerHTML =
    statusCard(ap, ap ? RADIO_TOWER : WIFI_OFF, "Access point", "Direct connection to the device", ap ? "Active" : "Off",
      [["Name", settings().ap_ssid], ["IP address", "192.168.4.1"]],
      "The access point is off. Turn it on in <a href='settings'>Settings</a>.") +
    statusCard(s.sta, s.sta ? WIFI_ON : WIFI_OFF, "Wi-Fi network", "Connection to your router", s.sta ? "Connected" : "Not connected",
      [["Network", s.ssid], ["IP address", "192.168.1.42"]],
      "Not connected to any network. <a href='networks'>Pick one on Networks</a>.") +
    deviceCard();
}

/* ---------- Networks ---------- */

function level(rssi) {
  return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
}

// The scan list as /api/scan reports it. A name that isn't broadcast belongs to
// the hidden network: like the device (which matches by BSSID), that entry is
// marked connected and carries the real name.
function scanResults() {
  let s = demo();
  let joinedHidden = s.sta && !NETWORKS.some(function (n) {
    return !n.hidden && n.ssid === s.ssid;
  });
  let list = NETWORKS.map(function (n) {
    let connected = s.sta && (n.hidden ? joinedHidden : n.ssid === s.ssid);
    return Object.assign({}, n, { connected: connected, ssid: n.hidden && connected ? s.ssid : n.ssid });
  });
  // Like networks.js: the connected network on top, the rest strongest first
  return list.filter(function (n) { return n.connected; }).concat(list.filter(function (n) { return !n.connected; }));
}

function scan() {
  $("rescan").disabled = true;
  $("rescan").textContent = "Scanning…";
  fake(900, function () {
    $("rescan").disabled = false;
    $("rescan").textContent = "Rescan";
    $("hidden-btn").disabled = false;
    $("count").textContent = NETWORKS.length;
    $("list").innerHTML = scanResults().map(function (n, i) {
      let on = n.connected;
      let open = n.enc === "OPEN";
      // A hidden network can't be picked from the list unless it is the connected one
      let locked = n.hidden && !on;
      return (
        "<li><button class='net' onclick='pick(" + i + ")'" + (on ? " data-on" : "") + (locked ? " disabled" : "") + ">" +
        "<span class='bars l" + level(n.rssi) + "' title='" + n.rssi + " dBm'><i></i><i></i><i></i><i></i></span>" +
        "<span class='grow'><b>" + (locked ? "<i class='muted'>Hidden network</i>" : esc(n.ssid)) + "</b>" +
        "<small>Ch " + n.channel + " · <span class='mono'>" + n.mac + "</span></small></span>" +
        (on ? "<span class='chip ok'>Connected</span>" : "<span class='chip'>" + (open ? "Open" : esc(n.enc)) + "</span>") +
        "</button></li>"
      );
    }).join("");
  });
}

function pick(i) {
  let n = scanResults()[i];
  if (n.connected) {
    ask({
      title: "Disconnect from " + n.ssid + "?",
      text: "The saved password is forgotten. The device stays reachable through its access point if it is on.",
      ok: "Disconnect",
      danger: true,
      onOk: function () {
        fake(500, function () {
          setDemo({ sta: false });
          toast("Disconnected");
          scan();
        });
      },
    });
    return;
  }
  if (n.enc === "OPEN") {
    ask({
      title: "Connect to " + n.ssid + "?",
      text: "This network is open: traffic to and from the device is not encrypted.",
      ok: "Connect",
      onOk: function () {
        join(n.ssid);
      },
    });
    return;
  }
  let enterprise = n.enc === "WPA2-ENTERPRISE";
  ask({
    title: "Connect to " + n.ssid,
    text: enterprise ? "This network asks for a username and password." : "",
    user: enterprise ? "Username" : null,
    pass: true,
    minPass: enterprise ? 1 : 8,
    ok: "Connect",
    onOk: function () {
      join(n.ssid);
    },
  });
}

function connectHidden() {
  ask({
    title: "Connect to a hidden network",
    user: "Network name",
    pass: true,
    ok: "Connect",
    onOk: function (ssid) {
      join(ssid);
    },
  });
}

function join(ssid) {
  toast("Connecting to " + ssid + "…", 1800);
  fake(1800, function () {
    setDemo({ sta: true, ssid: ssid });
    toast("Connected to " + ssid);
    scan();
  });
}

/* ---------- Settings ---------- */

const SETTINGS = {
  ap_ssid: "esp32-garden-setup",
  ap_password: "changeme123",
  ap_channel: 6,
  ap_hidden: false,
  hostname: "esp32-garden",
};

function loadSettings() {
  fake(350, function () {
    let all = Object.assign({ ap_status: demo().ap }, settings());
    for (let k in all) {
      if (typeof all[k] === "boolean") $(k).checked = all[k];
      else $(k).value = all[k];
    }
  });
}

function save() {
  ask({
    title: "Save settings?",
    text: "The device reboots to apply them. The page reloads in about 30 s.",
    ok: "Save and reboot",
    onOk: function () {
      let saved = {};
      for (let k in SETTINGS) {
        let v = SETTINGS[k];
        saved[k] = typeof v === "boolean" ? $(k).checked : typeof v === "number" ? Number($(k).value) : $(k).value;
      }
      setDemo({ ap: $("ap_status").checked, settings: saved });
      fakeReboot("Settings saved");
    },
  });
}

function reboot() {
  ask({ title: "Reboot the device?", ok: "Reboot", onOk: function () { fakeReboot("Rebooting"); } });
}

function reset() {
  ask({
    title: "Reset settings?",
    text: "Wi-Fi and access point settings go back to defaults. Installed firmware (" +
      $("fw-installed").textContent + ") is not changed.",
    ok: "Reset",
    danger: true,
    onOk: function () {
      // The saved network is forgotten and the access point is back on (DEFAULT_AP_ENABLED)
      setDemo({ sta: false, ap: true, settings: null });
      fakeReboot("Settings reset");
    },
  });
}

// reloadAfterReboot(): the pill shows "Rebooting" until /api/health answers again
function fakeReboot(message) {
  toast(message + ", the page reloads when the device is back", 3000);
  setStatus("rebooting");
  setTimeout(function () {
    location.reload();
  }, 3000);
}

/* ---------- Firmware ---------- */

function loadFirmware() {
  let s = demo();
  $("fw-installed").textContent = "v" + installed() + (s.dev ? " (dev)" : "");
  // FIRMWARE_BUILD_DATE is __DATE__ " " __TIME__; local builds have commit "dev", which firmware.js hides
  $("fw-build").textContent = "Nov 12 2026 14:03:11" + (s.dev ? "" : " (a7f2fb1)");
  $("fw-target").textContent = target();
  showNote("fw-mismatch", mismatchText());
  // rollback_available is false while the update is unconfirmed
  $("fw-rollback").style.display = s.rollback && !s.finishing ? "" : "none";
  $("fw-previous").textContent = "Previous version: v" + previous().version;
  // The device checks on its own 30 s after boot, so the mock starts from a finished check
  renderCheck(s.update ? "available" : "up_to_date");
}

function renderCheck(state) {
  let msg = {
    never: "Not checked yet.",
    checking: "Checking for updates…",
    up_to_date: "Up to date.",
    available: "",
  }[state];
  $("fw-check-message").textContent = msg;
  $("fw-check").disabled = state === "checking" || updateFinishing;
  let check = fakeCheck(state);
  let available = state === "available";
  $("fw-available").style.display = available ? "" : "none";
  let badge = firmwareBadge(check, demo().dev);
  $("fw-badge").textContent = badge[0];
  $("fw-badge").className = "chip " + badge[1];
  if (available) {
    $("fw-available-title").textContent = (check.dev ? "Dev build v" : "v") + check.version + " is available";
    // Same shape as a CHANGELOG section, plus the edge cases: nesting, long code, an unclosed marker
    renderMarkdown(
      $("fw-notes"),
      "### Added\n\n- Light and dark themes, see [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)\n" +
        "- `esp32-8mb` build target with **two ~3.9 MB app slots**\n" +
        "  - Moving a board to it needs *one* USB flash\n\n" +
        "### Fixed\n\n- Hidden networks with an empty name, in\n  `esp32/src/wifi/a_very_long_file_name_that_would_otherwise_widen_the_card.cpp`\n" +
        "- An unclosed **marker stays plain text"
    );
  }
}

function checkForUpdates() {
  renderCheck("checking");
  fake(1200, function () {
    renderCheck(demo().update ? "available" : "up_to_date");
  });
}

function progress(label, done) {
  let bar = $("fw-progress");
  let p = 0;
  bar.style.display = "";
  $("fw-picked").style.display = "";
  $("fw-upload").style.display = "none";
  let t = setInterval(function () {
    p += 7;
    bar.value = Math.min(p, 100);
    setMessage(label + " " + Math.min(p, 100) + "%");
    if (p >= 100) {
      clearInterval(t);
      setMessage("Verifying and installing…");
      setTimeout(done, 900);
    }
  }, 120);
}

function setMessage(text, kind) {
  $("fw-message").textContent = text;
  $("fw-message").className = "grow sm " + (kind || "muted");
}

// The new firmware boots unconfirmed in the other slot; the running one becomes the previous slot
function bootNewFirmware(version, t) {
  let patch = { target: t }; // the home target stays
  patch.previous = { version: installed(), target: target() };
  patch.installed = version;
  patch.finishing = true;
  patch.finishingAt = null;
  patch.rollback = true;
  setDemo(patch);
}

function installUpdate() {
  let version = nextMinor(installed());
  ask({
    title: "Install v" + version + "?",
    text: "The device downloads the update, installs it and reboots. Settings are kept.",
    ok: "Install",
    onOk: function () {
      progress("Downloading…", function () {
        setDemo({ update: false });
        bootNewFirmware(version, target());
        fakeReboot("Update installed");
      });
    },
  });
}

// The real page reads the file's info block (analyzeFile in firmware.js); the mock reads
// <slug>-<version>-<target>.ota.bin from the name and applies the same rules.
function analyzeName(name) {
  let home = homeTarget();
  let running = target();
  let expected = "Use a file ending in -" + home + ".ota.bin.";
  if (/\.factory\.bin$/.test(name)) {
    return { error: "This is a factory image for USB flashing, not an update. Use the file ending in -" + home + ".ota.bin." };
  }
  if (!/\.ota\.bin$/.test(name)) return { error: "This file is not a firmware update. " + expected };
  let m = /-(\d+\.\d+\.\d+)-(esp32-[a-z0-9-]+)\.ota\.bin$/.exec(name);
  let version = m ? m[1] : nextMinor(installed());
  let t = m ? m[2] : running;
  if (LAYOUT[t] !== LAYOUT[running]) {
    return {
      error: "This file is for " + t + " with another flash layout and cannot run on this device (" +
        describeTarget(running) + "). " + expected,
    };
  }
  let warnings = [];
  if (t !== running) {
    warnings.push(t === home
      ? "This restores the device's original configuration (" + t + ")."
      : "This file is for " + t + ". This device runs " + running + ". The correct file for this device ends in -" +
        running + ".ota.bin. If you continue, future updates will follow " + t + ".");
  }
  let order = compareVersions(version, installed());
  if (order < 0) {
    warnings.push("This file is v" + version + ". Installed: v" + installed() + ". This is a downgrade, continue?");
  } else if (order === 0) {
    warnings.push("This file is v" + version + ", the version already installed. Install it again?");
  }
  return { version: version, target: t, warnings: warnings };
}

let picked = null;
function pickFirmware(file) {
  if (!file) return;
  $("fw-picked").style.display = "";
  $("fw-upload").style.display = "";
  let result = analyzeName(file.name);
  if (result.error) {
    picked = null;
    setMessage(file.name + ": " + result.error, "err");
    $("fw-upload").disabled = true;
    return;
  }
  picked = result;
  let summary = file.name + ": v" + result.version + " for " + result.target + ".";
  setMessage(result.warnings.length ? summary + " " + result.warnings.join(" ") : summary, result.warnings.length ? "warn" : "");
  $("fw-upload").disabled = updateFinishing;
}

function uploadFirmware() {
  if (!picked) return;
  let result = picked;
  ask({
    title: "Install v" + result.version + " (" + result.target + ")?",
    text: result.warnings.join("\n\n"),
    ok: "Install",
    onOk: function () {
      progress("Uploading…", function () {
        bootNewFirmware(result.version, result.target);
        fakeReboot("Update installed");
      });
    },
  });
}

function rollBack() {
  let prev = previous();
  ask({
    title: "Roll back to v" + prev.version + "?",
    text: "The device reboots into the version it ran before the last update. Settings are kept.",
    ok: "Roll back",
    danger: true,
    onOk: function () {
      let patch = { target: prev.target };
      patch.previous = { version: installed(), target: target() };
      patch.installed = prev.version;
      patch.rollback = false;
      setDemo(patch);
      fakeReboot("Rolling back");
    },
  });
}

// Drag and drop onto the upload box
document.addEventListener("DOMContentLoaded", function () {
  let drop = $("drop");
  if (!drop) return;
  drop.ondragover = function (e) {
    e.preventDefault();
    if (!$("fw-file").disabled) drop.classList.add("over");
  };
  drop.ondragleave = function () {
    drop.classList.remove("over");
  };
  drop.ondrop = function (e) {
    e.preventDefault();
    drop.classList.remove("over");
    if (!$("fw-file").disabled) pickFirmware(e.dataTransfer.files[0]);
  };
});

/* ---------- Demo panel (mock only) ---------- */

function buildDemoPanel() {
  let s = demo();
  let box = document.createElement("div");
  box.style.cssText = "position:fixed;left:1rem;bottom:1rem;z-index:40;display:grid;gap:.5rem;justify-items:start";
  box.innerHTML =
    "<div id='demo' class='card' style='display:none;gap:.6rem;padding:.9rem 1rem;width:15rem'>" +
    "<strong class='sm'>Demo states (mock only)</strong>" +
    Object.keys(DEMO_LABELS).map(function (k) {
      return "<label class='row between sm'>" + DEMO_LABELS[k] +
        "<input type='checkbox' class='switch' data-k='" + k + "'" + (s[k] ? " checked" : "") + "></label>";
    }).join("") +
    "<div class='row'><button class='btn grow' onclick='location.reload()'>Apply</button>" +
    "<button class='btn grow' onclick='demoToast()'>Test toast</button></div></div>" +
    "<div class='row'><button class='btn' style='box-shadow:var(--shadow)' onclick=\"let d=$('demo');d.style.display=d.style.display?'':'none'\">Demo states</button>" +
    // The published demo (ui_preview.py --export --back) links back to the installer
    (document.body.dataset.demoBack
      ? "<a class='btn' style='box-shadow:var(--shadow)' href='" + esc(document.body.dataset.demoBack) + "'>Back to the installer</a>"
      : "") +
    "</div>";
  document.body.appendChild(box);
  document.body.style.paddingBottom = "3.5rem";
  box.querySelectorAll("[data-k]").forEach(function (el) {
    el.onchange = function () {
      let p = {};
      p[el.dataset.k] = el.checked;
      setDemo(p);
    };
  });
}

// Cycles through short and long sample messages so toasts can be checked
var demoToasts = [
  "Settings saved",
  "Connecting to HomeNet…",
  "Request failed: the device did not answer",
  "This file is for esp32-8mb. This device runs esp32-8mb-psram. The correct file for this device ends in -esp32-8mb-psram.ota.bin.",
];
var demoToastIndex = 0;
function demoToast() {
  toast(demoToasts[demoToastIndex++ % demoToasts.length], 4000);
}
