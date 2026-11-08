// Home: access point and Wi-Fi status cards from /api/status

function statusCard(on, title, subtitle, chip, rows, offText) {
  return (
    "<article class='card'><div class='row'><span class='ico" + (on ? " on" : "") + "'>" +
    svg(on ? WIFI_ON : WIFI_OFF) +
    "</span><div class='grow'><h2>" + title + "</h2><p class='sm muted'>" + subtitle +
    "</p></div><span class='chip " + (on ? "ok" : "bad") + "'>" + chip + "</span></div>" +
    (on
      ? "<dl class='kv'>" +
        rows
          .map(function (r) {
            return "<div><dt>" + r[0] + "</dt><dd>" + esc(r[1]) + "</dd></div>";
          })
          .join("") +
        "</dl>"
      : "<p class='muted'>" + offText + "</p>") +
    "</article>"
  );
}

function load() {
  api("/api/status").then(function (r) {
    if (!r) return;
    let s = r.body;
    showNote("mismatch", mismatchText(s));
    $("cards").innerHTML =
      statusCard(s.softAP.enabled, "Access point", "Direct connection to the device",
        s.softAP.enabled ? "Active" : "Off",
        [["Name", s.softAP.ssid], ["IP address", s.softAP.ip]],
        "The access point is off. Turn it on in <a href='settings'>Settings</a>.") +
      statusCard(s.station.connected, "Wi-Fi network", "Connection to your router",
        s.station.connected ? "Connected" : "Not connected",
        [["Network", s.station.ssid], ["IP address", s.station.ip]],
        "Not connected to any network. <a href='scan'>Pick one on Scan</a>.");
  });
}

// "Update vX.Y.Z available" once a check has found one (warning colour for a dev update)
function onUpdateStatus(res) {
  showUpdateNote(res.check);
}
