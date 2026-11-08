// Networks: network list from /api/scan, connect / disconnect

var networks = [];

function load() {
  scan();
}

function setScanning(on) {
  $("rescan").disabled = on;
  $("rescan").textContent = on ? "Scanning…" : "Rescan";
}

function scan() {
  setScanning(true);
  api("/api/scan", { timeout: 10000 }).then(function (r) {
    if (!r) {
      setScanning(false);
      return;
    }
    // 202: the scan is still running in the background
    if (r.status === 202) {
      setTimeout(scan, 1000);
      return;
    }
    networks = connectedFirst(Array.isArray(r.body) ? r.body : []);
    setScanning(false);
    draw();
  });
}

// The connected network goes on top; the rest keep the device's order (strongest first)
function connectedFirst(list) {
  return list.filter(function (n) {
    return n.connected;
  }).concat(list.filter(function (n) {
    return !n.connected;
  }));
}

function level(rssi) {
  return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
}

function draw() {
  $("hidden-btn").disabled = false;
  $("count").textContent = networks.length;
  if (!networks.length) {
    $("list").innerHTML = "<li class='net muted'>No networks found. Try Rescan.</li>";
    return;
  }
  $("list").innerHTML = networks
    .map(function (n, i) {
      // A hidden network can't be picked from the list unless it is the connected one
      let locked = n.hidden && !n.connected;
      return (
        "<li><button class='net' onclick='pick(" + i + ")'" + (n.connected ? " data-on" : "") + (locked ? " disabled" : "") + ">" +
        "<span class='bars l" + level(n.rssi) + "' title='" + n.rssi + " dBm'><i></i><i></i><i></i><i></i></span>" +
        "<span class='grow'><b>" + (locked ? "<i class='muted'>Hidden network</i>" : esc(n.ssid)) + "</b>" +
        "<small>Ch " + n.channel + " · <span class='mono'>" + esc(n.mac) + "</span></small></span>" +
        (n.connected
          ? "<span class='chip ok'>Connected</span>"
          : "<span class='chip'>" + (n.enc === "OPEN" ? "Open" : esc(n.enc.replace(/-PSK$/, ""))) + "</span>") +
        "</button></li>"
      );
    })
    .join("");
}

function pick(i) {
  let n = networks[i];
  if (n.connected) {
    ask({
      title: "Disconnect from " + n.ssid + "?",
      text: "The saved password is forgotten. The device stays reachable through its access point if it is on.",
      ok: "Disconnect",
      danger: true,
      onOk: function () {
        api("/api/disconnect", { method: "POST" }).then(function (r) {
          if (r) toast(r.body.message || "Disconnected");
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
        join(n.ssid, "");
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
    onOk: function (user, pass) {
      // Enterprise credentials travel as "username|password"
      join(n.ssid, enterprise ? user + "|" + pass : pass);
    },
  });
}

function connectHidden() {
  ask({
    title: "Connect to a hidden network",
    user: "Network name",
    pass: true,
    ok: "Connect",
    onOk: function (ssid, pass) {
      join(ssid, pass);
    },
  });
}

function join(ssid, password) {
  api("/api/connect", { method: "POST", timeout: 10000, data: { ssid: ssid, password: password } }).then(function (r) {
    if (!r) return;
    toast("Connecting to " + ssid + "…", 4000);
    waitForConnection(ssid, Date.now() + 30000);
  });
}

// The device connects in the background; poll its status until the attempt ends
function waitForConnection(ssid, deadline) {
  api("/api/status", { timeout: 3000, quiet: true }).then(function (r) {
    let state = r && r.body.station.attempt.state;
    if (!r || state === "connecting") {
      if (Date.now() > deadline) {
        toast("Timed out waiting for " + ssid, 6000);
        scan();
        return;
      }
      setStatus("loading");
      setTimeout(function () {
        waitForConnection(ssid, deadline);
      }, 1000);
      return;
    }
    toast(state === "succeeded" ? "Connected to " + ssid : "Failed to connect to " + ssid, 6000);
    scan();
  });
}
