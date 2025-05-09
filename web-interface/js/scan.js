var access_points = [];

updateListeners = function () {
  if (window.innerWidth <= 768) {
    // get all elements with class 'network' and add onclick event
    var networks = document.querySelectorAll(".network");
    networks?.forEach(function (network) {
      network.onclick = function () {
        var connected = this.getAttribute("data-connected") === "true";
        if (connected) {
          disconnect();
        } else {
          var ssid = this.getAttribute("data-ssid");
          connect(ssid);
        }
      };
    });
    var actionButtons = document.querySelectorAll(".action-button");
    // hide action buttons
    actionButtons?.forEach(function (button) {
      button.style.display = "none";
    });
  } else {
    // revert to default behavior
    var networks = document.querySelectorAll(".network");
    networks?.forEach(function (network) {
      network.onclick = null;
    });
    var actionButtons = document.querySelectorAll(".action-button");
    actionButtons?.forEach(function (button) {
      button.style.display = null;
    });
  }
};

function getTableHeader() {
  thead = document.createElement("thead");
  thead.innerHTML = `<thead><th class="network-ssid">SSID</th><th class="network-ch" style="text-align: center">Ch</th><th class="network-enc"></th><th class="network-mac">MAC</th><th class="network-action"></th></thead>`;
  return thead;
}

function getNetworkRow(ap) {
  ssid = esc(ap.ssid);
  channel = ap.channel;
  rssi = ap.rssi;
  enc = esc(ap.enc);
  mac = ap.mac;
  connected = ap.connected;
  width = parseInt(rssi) + 130;
  if (width < 50) color = "meter-red";
  else if (width < 70) color = "meter-orange";
  else color = "meter-green";

  let row = document.createElement("tr");
  row.className = "network";
  row.setAttribute("data-connected", connected);
  row.setAttribute("data-ssid", ssid);
  row.innerHTML = `<td class='network-ssid'><span style='font-weight: 500'>${ssid}</span><div class='meter-background'> <div class='meter-foreground ${color}' style='width: ${width}%;'><div class='meter-value'>${rssi}</div></div></div></td><td class='network-channel' style='text-align: center;'>${channel}</td><td class='network-enc'>${enc} ${" "}${
    enc == "OPEN" ? "&#x1F513;" : "&#x1f512;"
  } </td><td class='network-mac'>${mac}</td>${
    connected
      ? `<td class='action-button' ><button class='red' onclick='disconnect("${ssid}")'>Disconnect</button></td>`
      : `<td class='action-button' ><button class='green' onclick='connect("${ssid}")'>Connect</button></td>`
  }
`;
  return row;
}

function draw() {
  getById("scan-access-points").disabled = false;
  let totalSpan = getById("access-points-total");
  totalSpan.innerHTML = `: ${access_points.length}`;
  let table = getById("access-points-table");
  table.innerHTML = "";
  if (access_points.length == 0) {
    return;
  }
  let thead = getTableHeader();
  let tbody = document.createElement("tbody");
  tbody.className = "access-points-table-body";

  access_points.forEach(function (ap) {
    tbody.appendChild(getNetworkRow(ap));
  });

  table.appendChild(thead);
  table.appendChild(tbody);
  updateListeners();

  window.addEventListener("resize", function () {
    updateListeners();
  });
}

function load() {
  getById("scan-access-points").disabled = true;
  apiCall({
    endpoint: "/api/scan",
    callback: function (res) {
      access_points = res;
      setStatusBanner("connected");
      draw();
    },
  });
}

function connect(ssid) {
  var ap = access_points.find((ap) => ap.ssid === ssid);
  if (!ap) {
    alert("Network not found");
    return;
  }

  const openNetworks = ["NONE", "OPEN"];
  enc = ap.enc;

  if (openNetworks.includes(enc)) {
    // No password needed
    sendConnectRequest(ssid, "");
  } else {
    // Show password modal
    handleModal({
      title: "Enter password for " + ssid,
      input: true,
      buttonText: "Connect",
      onclick: function () {
        var password = getById("modal-input").value;
        sendConnectRequest(ssid, password);
      },
    });
  }
}

function sendConnectRequest(ssid, password) {
  apiCall({
    endpoint: "/api/connect",
    method: "POST",
    data: { ssid: ssid, password: password },
    callback: function () {
      showMessage("Connected to " + ssid);
      load();
    },
  });
}

function disconnect(ssid) {
  handleModal({
    title: "Are you sure you want to disconnect from " + ssid + "?",
    input: false,
    buttonText: "Disconnect",
    onclick: function () {
      sendDisconnectRequest();
    },
  });
}

function sendDisconnectRequest() {
  apiCall({
    endpoint: "/api/disconnect",
    method: "POST",
    callback: function () {
      load();
    },
  });
}
