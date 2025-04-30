function getById(name) {
  return document.getElementById(name);
}

function esc(str) {
  return str
    ? str
        .toString()
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/\"/g, "&quot;")
        .replace(/\'/g, "&#39;")
        .replace(/\//g, "&#x2F;")
    : "";
}

function setStatusBanner(msg) {
  let banner = getById("status-banner");
  switch (msg) {
    case "connected":
      (banner.style.backgroundColor = "#3c5"),
        (banner.innerHTML = "Connected"),
        (banner.style.color = "#fff");
      break;
    case "loading":
      (banner.style.backgroundColor = "#ff0"),
        (banner.innerHTML = "Loading..."),
        (banner.style.color = "var(--primary-foreground)");
      break;
    case "error":
      (banner.style.backgroundColor = "#d33"),
        (banner.innerHTML = "Disconnected"),
        (banner.style.color = "#fff");
      break;
    default:
      (banner.style.backgroundColor = "#d33"),
        (banner.innerHTML = "Unknown Status"),
        (banner.style.color = "#fff");
  }
}

function apiCall({
  endpoint,
  callback = function () {},
  timeout = 6000,
  method = "GET",
  onTimeout = function () {
    setStatusBanner("error");
    let msg = "Request timed out for " + endpoint;
    console.error(msg);
    toast(msg, 6000);
    if ("undefined" != typeof draw) draw();
  },
  onError = function (error) {
    setStatusBanner("error");
    let msg = "Request failed for " + endpoint;
    console.error(msg);
    toast(msg, 6000);
    console.error("Response: " + this.responseText);
    console.error(error);
    if ("undefined" != typeof draw) draw();
  },
  data = null,
}) {
  setStatusBanner("loading");
  let request = new XMLHttpRequest();

  request.open(method, encodeURI(endpoint), true);
  request.setRequestHeader("Content-Type", "application/json");
  request.timeout = timeout;
  request.ontimeout = onTimeout;
  request.onerror = onError;
  request.overrideMimeType("application/json");
  request.onreadystatechange = function () {
    if (this.readyState === XMLHttpRequest.DONE) {
      if (200 === this.status) n(JSON.parse(this.responseText));
      else {
        setStatusBanner("error");
        let t = "Request failed for " + endpoint + ": " + this.statusText;
        toast(t, 6000),
          console.error(this),
          console.error(t),
          console.error("Response: " + this.responseText),
          "undefined" != typeof draw && draw();
      }
    }
  };
  request.send(data ? JSON.stringify(data) : null);
}

function connected() {
  "undefined" != typeof load && load();
}
function checkConnection() {
  apiCall({
    endpoint: "/api/health",
    callback: connected,
    timeout: 6000,
    method: "GET",
    onTimeout: function () {
      setTimeout(checkConnection, 2e3);
    },
    onError: function () {
      setTimeout(checkConnection, 2e3);
    },
  });
}
