// Talks to the device: api() calls, the start-up connection check, the
// "Finishing update" countdown, reloadAfterReboot() and the mismatch text.
// Relative /api URLs, so utils/ui_preview.py can forward them to a device.

/**
 * Calls the API and resolves to {status, body} for a 2xx answer. Anything else
 * resolves to null after showing the device's message (unless quiet).
 * opts: {method, data, timeout, quiet}
 */
function api(endpoint, opts) {
  opts = opts || {};
  if (!opts.quiet) setStatus("loading");
  return new Promise(function (resolve) {
    let req = new XMLHttpRequest();
    req.open(opts.method || "GET", endpoint, true);
    // Bodiless POSTs must still declare JSON (the device's POST check)
    req.setRequestHeader("Content-Type", "application/json");
    req.timeout = opts.timeout || 6000;
    let fail = function (message) {
      setStatus("error");
      if (!opts.quiet) toast(message, 6000);
      resolve(null);
    };
    req.onerror = function () {
      fail("Request failed for " + endpoint);
    };
    req.ontimeout = function () {
      fail("Request timed out for " + endpoint);
    };
    req.onload = function () {
      let body = {};
      try {
        body = JSON.parse(req.responseText);
      } catch (e) {}
      // 502/504: the preview server (utils/ui_preview.py) could not reach the device
      if (req.status === 502 || req.status === 504) {
        fail(body.message || "Device not reachable");
        return;
      }
      // Any other answer means the device is there, even when it says no
      setStatus("connected");
      if (req.status >= 200 && req.status < 300) {
        if (body.success === false && !opts.quiet) toast(body.message, 6000);
        resolve({ status: req.status, body: body });
        return;
      }
      if (!opts.quiet) toast(body.message || "Request failed: " + req.status + " " + req.statusText, 6000);
      resolve(null);
    };
    req.send(opts.data ? JSON.stringify(opts.data) : null);
  });
}

// Waits for /api/health, fills in the version, then runs the page's load()
function start() {
  api("/api/health", { quiet: true }).then(function (r) {
    if (!r) {
      setStatus("error");
      setTimeout(start, 2000);
      return;
    }
    let fw = r.body.data && r.body.data.firmware;
    if (fw) $("fw-version").textContent = "v" + fw.version + (fw.dev ? " (dev)" : "");
    if (typeof load !== "undefined") load();
    checkUpdateStatus();
  });
}
document.addEventListener("DOMContentLoaded", start);

var finishingTimer = null;

function checkUpdateStatus() {
  api("/api/update/status", { quiet: true }).then(function (r) {
    if (!r) return;
    let res = r.body;
    clearInterval(finishingTimer);
    // Pages that show update state (Home badge, Firmware card) define this
    if (typeof onUpdateStatus !== "undefined") onUpdateStatus(res);
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
  });
}

// After a save, reset, reboot, install or rollback: wait for the device to come back, then reload
function reloadAfterReboot() {
  setStatus("rebooting");
  function poll() {
    let req = new XMLHttpRequest();
    req.open("GET", "/api/health", true);
    req.timeout = 3000;
    req.onload = function () {
      if (this.status === 200) location.reload();
      else setTimeout(poll, 2000);
    };
    req.onerror = req.ontimeout = function () {
      setTimeout(poll, 2000);
    };
    req.send();
  }
  setTimeout(poll, 5000);
}

// Running a configuration other than the one the device was set up with
function mismatchText(status) {
  if (!status || !status.target_mismatch) return "";
  return (
    "This device was set up as " + status.home_target + " but is running " + status.target +
    ". To fix this, upload a file ending in -" + status.home_target + ".ota.bin from Settings."
  );
}
