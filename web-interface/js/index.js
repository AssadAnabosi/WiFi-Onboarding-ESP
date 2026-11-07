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
    case "rebooting":
      (banner.style.backgroundColor = "#ff0"),
        (banner.innerHTML = "Rebooting..."),
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
    if (typeof draw !== "undefined") {
      draw();
    }
  },
  onError = function (error) {
    setStatusBanner("error");
    let msg = "Request failed for " + endpoint;
    console.error(msg);
    toast(msg, 6000);
    console.error("Response: " + this.responseText);
    console.error(error);
    if (typeof draw !== "undefined") {
      draw();
    }
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
      if (this.status >= 200 && this.status < 300) {
        setStatusBanner("connected");
        const response = JSON.parse(this.responseText);
        if (
          typeof response.success !== "undefined" &&
          response.success === false
        ) {
          toast(response.message, 6000);
        }
        callback(response, this.status);
      } else {
        setStatusBanner("error");
        let msg =
          "Request failed for " +
          method +
          " " +
          endpoint +
          ": " +
          this.statusText;
        try {
          const response = JSON.parse(this.responseText);
          if (response.message) msg = response.message;
        } catch (e) {}
        toast(msg, 6000);
        console.error(this);
        console.error(msg);
        console.error("Response: " + this.responseText);
        if (typeof draw !== "undefined") {
          draw();
        }
      }
    }
  };
  request.send(data ? JSON.stringify(data) : null);
}

function connected(res) {
  // The footer version is written in at build time; dev builds are marked here
  let version = getById("fw-version");
  if (version && res && res.data && res.data.firmware && res.data.firmware.dev) {
    if (!version.textContent.endsWith("(dev)")) version.textContent += " (dev)";
  }
  if (typeof load !== "undefined") {
    load();
  }
  checkUpdateStatus();
}

// While a new firmware has not confirmed itself, the device refuses reboots,
// settings saves, factory reset and updates: show a countdown and disable them
var finishingTimer = null;
var updateFinishing = false;

function setFinishing(seconds) {
  let banner = getById("finishing-banner");
  if (!banner) {
    banner = document.createElement("div");
    banner.id = "finishing-banner";
    getById("status-banner").after(banner);
  }
  banner.style.display = seconds > 0 ? "block" : "none";
  banner.textContent = "Finishing update, " + seconds + " s";
  updateFinishing = seconds > 0;
  document.querySelectorAll(".lock-while-finishing").forEach(function (el) {
    el.disabled = updateFinishing;
  });
  if (typeof refreshFirmwareButtons !== "undefined") {
    refreshFirmwareButtons();
  }
}

function checkUpdateStatus() {
  apiCall({
    endpoint: "/api/update/status",
    callback: function (res) {
      clearInterval(finishingTimer);
      // Pages that show update state (Home badge, Firmware card) define this
      if (typeof onUpdateStatus !== "undefined") {
        onUpdateStatus(res);
      }
      if (!res.confirming) {
        setFinishing(0);
        return;
      }
      let seconds = Math.max(res.confirm_in_s || 0, 1);
      setFinishing(seconds);
      finishingTimer = setInterval(function () {
        seconds--;
        if (seconds > 0) {
          setFinishing(seconds);
        } else {
          clearInterval(finishingTimer);
          setTimeout(checkUpdateStatus, 1000);
        }
      }, 1000);
    },
  });
}

// After an install or rollback: wait for the device to go down and come back, then reload
function reloadAfterReboot() {
  setStatusBanner("rebooting");
  function poll() {
    let request = new XMLHttpRequest();
    request.open("GET", "/api/health", true);
    request.timeout = 3000;
    request.onload = function () {
      if (this.status === 200) {
        window.location.reload();
      } else {
        setTimeout(poll, 2000);
      }
    };
    request.onerror = request.ontimeout = function () {
      setTimeout(poll, 2000);
    };
    request.send();
  }
  setTimeout(poll, 5000);
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

// Running a configuration other than the one the device was set up with (/api/status)
function mismatchText(status) {
  if (!status || !status.target_mismatch) return "";
  return (
    "This device was set up as " +
    status.home_target +
    " but is running " +
    status.target +
    ". To fix this, upload a file ending in -" +
    status.home_target +
    ".ota.bin from Settings."
  );
}

function showMismatch(id, status) {
  let warning = getById(id);
  let text = mismatchText(status);
  warning.textContent = text;
  warning.style.display = text ? "block" : "none";
}
