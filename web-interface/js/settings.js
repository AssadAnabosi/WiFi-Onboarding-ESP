// Settings: /api/settings form, reboot and reset. Firmware card is firmware.js.

const SETTING_KEYS = ["ap_ssid", "ap_password", "ap_channel", "ap_hidden", "ap_status", "hostname"];
var settings = {};

function load() {
  loadSettings();
  loadFirmware();
}

function loadSettings() {
  api("/api/settings").then(function (r) {
    if (!r) return;
    settings = r.body;
    SETTING_KEYS.forEach(function (k) {
      if (typeof settings[k] === "boolean") $(k).checked = settings[k];
      else $(k).value = settings[k];
    });
  });
}

function save() {
  ask({
    title: "Save settings?",
    text: "The device reboots to apply them. The page reloads when it is back.",
    ok: "Save and reboot",
    onOk: function () {
      let data = {};
      SETTING_KEYS.forEach(function (k) {
        let el = $(k);
        data[k] = el.type === "checkbox" ? el.checked : el.type === "number" ? parseInt(el.value, 10) : el.value;
      });
      api("/api/settings", { method: "POST", data: data, timeout: 8000 }).then(function (r) {
        if (!r) return;
        toast(r.body.message || "Settings saved", 6000);
        reloadAfterReboot();
      });
    },
  });
}

function reboot() {
  ask({
    title: "Reboot the device?",
    ok: "Reboot",
    onOk: function () {
      api("/api/reboot", { method: "POST" }).then(function (r) {
        if (!r) return;
        toast(r.body.message || "Rebooting", 6000);
        reloadAfterReboot();
      });
    },
  });
}

function reset() {
  ask({
    title: "Reset settings?",
    text:
      "Wi-Fi and access point settings go back to defaults. Installed firmware (" +
      $("fw-installed").textContent + ") is not changed.",
    ok: "Reset",
    danger: true,
    onOk: function () {
      api("/api/reset", { method: "POST" }).then(function (r) {
        if (!r) return;
        toast(r.body.message || "Resetting", 6000);
        reloadAfterReboot();
      });
    },
  });
}
