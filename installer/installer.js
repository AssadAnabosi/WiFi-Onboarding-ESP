// Installer page: lists the targets from firmware/install-targets.json (utils/release.py install)
// and points the ESP Web Tools button at the chosen one's manifest. js/app.js (from
// web-interface/) builds the header, theme switch and footer, as on the device.

document.addEventListener("DOMContentLoaded", function () {
  // app.js links the brand to the device's Home page; here there is only this page
  let brand = document.querySelector(".brand");
  if (brand) brand.setAttribute("href", "./");

  if (!("serial" in navigator)) $("no-serial").classList.add("show");

  let button = $("install");
  // Always a new install: the factory image replaces the settings too, so ESP Web Tools'
  // "update" path for a device already running this firmware would promise too much
  button.overrides = {
    checkSameFirmware: function () {
      return false;
    },
  };

  // esp32-4mb -> "4 MB flash", "ESP32 · esp32-4mb"
  function describe(target) {
    let parts = target.split("-");
    let chip = parts[0].toUpperCase().replace(/^(ESP32)(.+)$/, "$1-$2");
    let flash = (parts[1] || "").toUpperCase().replace("MB", " MB");
    let extra = parts.slice(2).join(", ");
    return [flash + " flash", chip + (extra ? " · " + extra : "") + " · " + target];
  }

  function showError() {
    $("targets").innerHTML =
      "<li><span class='net err'>Couldn't load the firmware list. Reload the page, or use the files from the latest release.</span></li>";
  }

  fetch("firmware/install-targets.json", { cache: "no-cache" })
    .then(function (response) {
      if (!response.ok) throw new Error(response.status);
      return response.json();
    })
    .then(function (index) {
      $("release").textContent = "Version " + index.version + ", flashed over USB right from this page.";
      $("fw-version").textContent = "v" + index.version;
      let list = $("targets");
      list.textContent = "";
      index.targets.forEach(function (entry, i) {
        let text = describe(entry.target);
        let item = document.createElement("li");
        item.innerHTML =
          "<label class='net'><input type='radio' name='target'><span class='grow'><b></b><small></small></span></label>";
        let input = item.querySelector("input");
        input.value = entry.manifest;
        input.checked = i === 0;
        input.addEventListener("change", function () {
          button.setAttribute("manifest", "firmware/" + entry.manifest);
        });
        item.querySelector("b").textContent = text[0];
        item.querySelector("small").textContent = text[1];
        list.appendChild(item);
      });
      if (index.targets.length) button.setAttribute("manifest", "firmware/" + index.targets[0].manifest);
      else showError();
    })
    .catch(showError);
});
