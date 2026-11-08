// MOCK ONLY. Stands in for the device API with fake data so the design can be
// tried from disk. The page functions here map onto home.js / scan.js /
// settings.js / firmware.js; the fake calls become apiCall() to the real endpoints.

const DEMO_KEY = "mock-state";
const DEMO_DEFAULTS = {
  ap: true,
  sta: true,
  ssid: "HomeNet",
  update: false,
  finishing: false,
  mismatch: false,
  rollback: false,
  offline: false,
};
const DEMO_LABELS = {
  ap: "Access point on",
  sta: "Wi-Fi connected",
  update: "Update available",
  finishing: "Finishing update",
  mismatch: "Config mismatch",
  rollback: "Rollback available",
  offline: "Device offline",
};

function demo() {
  try {
    return Object.assign({}, DEMO_DEFAULTS, JSON.parse(localStorage.getItem(DEMO_KEY)));
  } catch (e) {
    return Object.assign({}, DEMO_DEFAULTS);
  }
}
function setDemo(patch) {
  try {
    localStorage.setItem(DEMO_KEY, JSON.stringify(Object.assign(demo(), patch)));
  } catch (e) {}
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
  let notice = $("notice-icon");
  if (notice) notice.innerHTML = svg(WIFI_ON, 26);
  fake(450, function () {
    let s = demo();
    if (s.finishing) {
      let n = 48;
      setFinishing(n);
      let t = setInterval(function () {
        setFinishing(--n);
        if (n <= 0) {
          clearInterval(t);
          setDemo({ finishing: false });
          toast("Update confirmed");
        }
      }, 1000);
    }
    let page = document.body.dataset.page;
    if (page === "home") drawHome();
    if (page === "scan") scan();
    if (page === "settings") {
      loadSettings();
      loadFirmware();
    }
  });
});

function mismatchText() {
  return demo().mismatch
    ? "This device was set up as esp32-4mb but is running esp32-4mb-relay. To fix this, upload a file ending in -esp32-4mb.ota.bin from Settings."
    : "";
}
function showNote(id, text) {
  $(id).textContent = text;
  $(id).classList.toggle("show", !!text);
}

/* ---------- Home ---------- */

function statusCard(on, title, subtitle, chip, rows, offText) {
  return (
    "<article class='card'><div class='row'><span class='ico" + (on ? " on" : "") + "'>" +
    svg(on ? WIFI_ON : WIFI_OFF) +
    "</span><div class='grow'><h2>" + title + "</h2><p class='sm muted'>" + subtitle +
    "</p></div><span class='chip " + (on ? "ok" : "bad") + "'>" + chip + "</span></div>" +
    (on
      ? "<dl class='kv'>" +
        rows.map(function (r) {
          return "<div><dt>" + r[0] + "</dt><dd>" + esc(r[1]) + "</dd></div>";
        }).join("") +
        "</dl>"
      : "<p class='muted'>" + offText + "</p>") +
    "</article>"
  );
}

function drawHome() {
  let s = demo();
  showNote("mismatch", mismatchText());
  let badge = $("update-badge");
  badge.textContent = "Update v1.3.0 available, open Settings to install →";
  badge.classList.toggle("show", s.update);
  $("cards").innerHTML =
    statusCard(s.ap, "Access point", "Direct connection to the device", s.ap ? "Active" : "Off",
      [["Name", "esp32-garden-setup"], ["IP address", "192.168.4.1"]],
      "The access point is off. Turn it on in <a href='settings.html'>Settings</a>.") +
    statusCard(s.sta, "Wi-Fi network", "Connection to your router", s.sta ? "Connected" : "Not connected",
      [["Network", s.ssid], ["IP address", "192.168.1.42"]],
      "Not connected to any network. <a href='scan.html'>Pick one on Scan</a>.");
}

/* ---------- Scan ---------- */

function level(rssi) {
  return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
}

function scan() {
  $("rescan").disabled = true;
  $("rescan").textContent = "Scanning…";
  fake(900, function () {
    let s = demo();
    $("rescan").disabled = false;
    $("rescan").textContent = "Rescan";
    $("hidden-btn").disabled = false;
    $("count").textContent = NETWORKS.length;
    $("list").innerHTML = NETWORKS.map(function (n, i) {
      let on = s.sta && n.ssid === s.ssid;
      let open = n.enc === "OPEN";
      return (
        "<li><button class='net' onclick='pick(" + i + ")'" + (on ? " data-on" : "") + (n.hidden ? " disabled" : "") + ">" +
        "<span class='bars' data-l='" + level(n.rssi) + "' title='" + n.rssi + " dBm'><i></i><i></i><i></i><i></i></span>" +
        "<span class='grow'><b>" + (n.hidden ? "<i class='muted'>Hidden network</i>" : esc(n.ssid)) + "</b>" +
        "<small>Ch " + n.channel + " · " + n.rssi + " dBm · <span class='mono'>" + n.mac + "</span></small></span>" +
        (on ? "<span class='chip ok'>Connected</span>" : "<span class='chip'>" + (open ? "Open" : esc(n.enc)) + "</span>") +
        "</button></li>"
      );
    }).join("");
  });
}

function pick(i) {
  let n = NETWORKS[i];
  let s = demo();
  if (s.sta && n.ssid === s.ssid) {
    ask({
      title: "Disconnect from " + n.ssid + "?",
      text: "The device stays reachable through its access point.",
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
  if (n.enc === "OPEN") return join(n.ssid);
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
    let all = Object.assign({ ap_status: demo().ap }, SETTINGS);
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
      setDemo({ ap: $("ap_status").checked });
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
    text: "Wi-Fi and access point settings go back to defaults. Installed firmware (v1.2.0) is not changed.",
    ok: "Reset",
    danger: true,
    onOk: function () {
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
  $("fw-installed").textContent = "v1.2.0";
  $("fw-build").textContent = "2026-11-02 (a7f2fb1)";
  $("fw-target").textContent = s.mismatch ? "esp32-4mb-relay" : "esp32-4mb";
  showNote("fw-mismatch", mismatchText());
  $("fw-rollback").style.display = s.rollback ? "" : "none";
  $("fw-previous").textContent = "Previous version: v1.1.4";
  renderCheck(s.update ? "available" : "never");
}

function renderCheck(state) {
  let msg = {
    never: "Not checked yet.",
    checking: "Checking for updates…",
    up_to_date: "You're on the latest version.",
    available: "",
  }[state];
  $("fw-check-message").textContent = msg;
  $("fw-check").disabled = state === "checking";
  let available = state === "available";
  $("fw-available").style.display = available ? "" : "none";
  $("fw-badge").textContent = available ? "Update available" : state === "up_to_date" ? "Up to date" : "";
  $("fw-badge").className = "chip" + (available || state === "up_to_date" ? " ok" : "");
  if (available) {
    $("fw-available-title").textContent = "v1.3.0 is available";
    $("fw-notes").textContent =
      "## Added\n- Light and dark themes\n- Signal bars on the Scan page\n\n## Fixed\n- Hidden networks with an empty name";
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

function installUpdate() {
  ask({
    title: "Install v1.3.0?",
    text: "The device downloads the update, installs it and reboots. Settings are kept.",
    ok: "Install",
    onOk: function () {
      progress("Downloading…", function () {
        setDemo({ update: false, finishing: true, rollback: true });
        fakeReboot("Update installed");
      });
    },
  });
}

let picked = null;
function pickFirmware(file) {
  if (!file) return;
  picked = file;
  $("fw-picked").style.display = "";
  $("fw-upload").style.display = "";
  // The real page reads the info block here (analyzeFile in firmware.js)
  if (!/\.ota\.bin$/.test(file.name)) {
    setMessage(file.name + ": this file is not a firmware update. Use a file ending in -esp32-4mb.ota.bin.", "err");
    $("fw-upload").disabled = true;
    return;
  }
  setMessage(file.name + ": v1.3.0 for esp32-4mb.");
  $("fw-upload").disabled = false;
}

function uploadFirmware() {
  ask({
    title: "Install v1.3.0 (esp32-4mb)?",
    ok: "Install",
    onOk: function () {
      progress("Uploading…", function () {
        setDemo({ finishing: true, rollback: true });
        fakeReboot("Update installed");
      });
    },
  });
}

function rollBack() {
  ask({
    title: "Roll back to v1.1.4?",
    text: "The device reboots into the version it ran before the last update. Settings are kept.",
    ok: "Roll back",
    danger: true,
    onOk: function () {
      setDemo({ rollback: false });
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
    drop.classList.add("over");
  };
  drop.ondragleave = function () {
    drop.classList.remove("over");
  };
  drop.ondrop = function (e) {
    e.preventDefault();
    drop.classList.remove("over");
    pickFirmware(e.dataTransfer.files[0]);
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
    "<button class='btn' onclick='location.reload()'>Apply</button></div>" +
    "<button class='btn' style='box-shadow:var(--shadow)' onclick=\"let d=$('demo');d.style.display=d.style.display?'':'none'\">Demo states</button>";
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
