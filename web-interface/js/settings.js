var settings = {
  ap_ssid: "",
  ap_password: "",
  // ap_channel: 0,
  ap_hidden: false,
  ap_status: false,
  hostname: "",
};

function draw() {
  getById("reload-settings").disabled = true;
  for (var key in settings) {
    if (typeof settings[key] == "boolean") {
      getById(key).checked = settings[key];
    } else if (typeof settings[key] == "number") {
      getById(key).value = settings[key];
    } else if (typeof settings[key] == "string") {
      getById(key).value = settings[key].toString();
    }
  }

  getById("reload-settings").disabled = false;
}

function save() {
  handleModal({
    title: "Save settings",
    buttonText: "Save",
    onclick: function () {
      let newSettings = {};
      for (var key in settings) {
        key = esc(key);
        if (typeof settings[key] == "boolean") {
          newSettings[key] = getById(key).checked;
        } else if (typeof settings[key] == "number") {
          newSettings[key] = parseInt(getById(key).value);
        } else if (typeof settings[key] == "string") {
          newSettings[key] = getById(key).value.toString();
        }
      }
      toast("Saving settings...", 25000);
      apiCall({
        endpoint: "/api/settings",
        method: "POST",
        data: newSettings,
        timeout: 8000,
      });
      alert("Saving... page will reload in 30 seconds");
      setTimeout(function () {
        window.location.reload();
      }, 30000);
    },
  });
}

function reset() {
  handleModal({
    title: "Reset settings",
    buttonText: "Reset",
    onclick: function () {
      toast("Resetting settings...", 25000);
      apiCall({
        endpoint: "/api/reset",
        method: "POST",
      });
      alert("Resetting... page will reload in 30 seconds");
      setTimeout(function () {
        window.location.reload();
      }, 30000);
    },
  });
}

function reboot() {
  handleModal({
    title: "Reboot device",
    buttonText: "Reboot",
    onclick: function () {
      toast("Rebooting...", 25000);
      apiCall({
        endpoint: "/api/reboot",
        method: "POST",
      });
      alert("Rebooting... page will reload in 30 seconds");
      setTimeout(function () {
        window.location.reload();
      }, 30000);
    },
  });
}

function load() {
  apiCall({
    endpoint: "/api/settings",
    callback: function (res) {
      settings = res;
      setStatusBanner("connected");
      draw();
    },
  });
}
