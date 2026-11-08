// Home: access point, Wi-Fi and device cards from /api/status and /api/health

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
    rows
      .map(function (r) {
        return "<div><dt>" + r[0] + "</dt><dd>" + esc(r[1]) + "</dd></div>";
      })
      .join("") +
    "</dl>"
  );
}

// The core reports the chip revision as major * 100 + minor (301 = v3.1)
function chipRevision(rev) {
  return rev >= 100 ? Math.floor(rev / 100) + "." + (rev % 100) : String(rev);
}

// Full-width card under the two status cards: chip, CPU, flash and free heap from /api/health
function deviceCard(info) {
  let chip = info.chip_info || {};
  let mem = info.memory || {};
  return (
    "<article class='card wide'><div class='head'><span class='ico'>" + svg(CPU) +
    "</span><div class='grow'><h2>Device</h2></div></div>" +
    kvList(
      [
        ["Chip", (chip.chip_model || "?") + (chip.chip_revision != null ? " v" + chipRevision(chip.chip_revision) : "")],
        ["CPU", chip.cores + (chip.cores === 1 ? " core" : " cores") + " · " + chip.cpu_freq_mhz + " MHz"],
        ["Flash", mem.flash_size_mb + " MB"],
        ["Free heap", mem.free_heap_kb + " KB"],
      ],
      "cols2"
    ) +
    "</article>"
  );
}

function load() {
  api("/api/status").then(function (r) {
    if (r) drawCards(r.body);
  });
}

function drawCards(s) {
  showNote("mismatch", mismatchText(s));
  $("cards").innerHTML =
    statusCard(s.softAP.enabled, s.softAP.enabled ? RADIO_TOWER : WIFI_OFF, "Access point", "Direct connection to the device",
      s.softAP.enabled ? "Active" : "Off",
      [["Name", s.softAP.ssid], ["IP address", s.softAP.ip]],
      "The access point is off. Turn it on in <a href='settings'>Settings</a>.") +
    statusCard(s.station.connected, s.station.connected ? WIFI_ON : WIFI_OFF, "Wi-Fi network", "Connection to your router",
      s.station.connected ? "Connected" : "Not connected",
      [["Network", s.station.ssid], ["IP address", s.station.ip]],
      "Not connected to any network. <a href='networks'>Pick one on Networks</a>.") +
    deviceCard(deviceInfo);
}

// "Update vX.Y.Z available" once a check has found one (warning colour for a dev update)
function onUpdateStatus(res) {
  showUpdateNote(res.check);
}
