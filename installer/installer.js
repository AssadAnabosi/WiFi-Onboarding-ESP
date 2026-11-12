// Installer page: the version picker (every public release, newest first, from
// firmware/install-index.json written by utils/release.py install), its release notes, the
// boards each version was built for, and the option to flash your own file. The chosen build's
// manifest is handed to the ESP Web Tools button. js/app.js (from web-interface/) builds the
// header, theme switch and footer, as on the device; js/markdown.js renders the notes.

const OWN_FILE = "own";

// Chip IDs in the ESP image header (esp_image_header_t.chip_id), as ESP Web Tools names the chips
const CHIP_FAMILIES = { 0: "ESP32", 2: "ESP32-S2", 5: "ESP32-C3", 9: "ESP32-S3", 12: "ESP32-C2", 13: "ESP32-C6" };
const IMAGE_MAGIC = 0xe9;
const PARTITION_TABLE = 0x8000;

let index = null; // install-index.json
let chosenTarget = null; // kept when switching versions, if the new version has it
let ownManifestUrl = null;

document.addEventListener("DOMContentLoaded", function () {
  // app.js links the brand to the device's Home page; here there is only this page
  let brand = document.querySelector(".brand");
  if (brand) brand.setAttribute("href", "./");
  // No firmware version in the footer: the page offers many. Drop app.js's " · " before it too.
  let footerVersion = $("fw-version");
  if (footerVersion) {
    let separator = footerVersion.previousSibling;
    if (separator && separator.nodeType === Node.TEXT_NODE) separator.textContent = separator.textContent.replace(/\s*·\s*$/, "");
    footerVersion.remove();
  }

  if (!("serial" in navigator)) $("no-serial").classList.add("show");

  // Always a new install: ESP Web Tools then erases the whole chip first, which the trimmed
  // images rely on, and its "update" path for a device already running this firmware would
  // promise to keep settings the factory image replaces
  $("install").overrides = {
    checkSameFirmware: function () {
      return false;
    },
  };

  $("version").addEventListener("change", showVersion);
  $("own-file").addEventListener("change", function () {
    pickOwnFile(this.files[0]);
  });
  let drop = $("drop");
  drop.ondragover = function (e) {
    e.preventDefault();
    drop.classList.add("over");
  };
  drop.ondragleave = function () {
    drop.classList.remove("over");
  };
  drop.ondrop = function (e) {
    e.preventDefault();
    drop.classList.remove("over");
    if (e.dataTransfer.files[0]) pickOwnFile(e.dataTransfer.files[0]);
  };

  // Links to the collapsed "No port selected" card open it
  document.querySelectorAll("a[href='#no-port']").forEach(function (link) {
    link.addEventListener("click", function () {
      $("no-port").open = true;
    });
  });
  if (location.hash === "#no-port") $("no-port").open = true;

  numberSteps();
  fetch("firmware/install-index.json", { cache: "no-cache" })
    .then(function (response) {
      if (!response.ok) throw new Error(response.status);
      return response.json();
    })
    .then(function (data) {
      index = data;
      let select = $("version");
      select.textContent = "";
      index.versions.forEach(function (entry, i) {
        select.add(new Option(entry.version + (i === 0 ? " (latest)" : ""), entry.version));
      });
      select.add(new Option("Your own file…", OWN_FILE));
      select.disabled = false;
      showVersion();
    })
    .catch(function () {
      // The own-file option still works without the list
      index = { versions: [] };
      let select = $("version");
      select.textContent = "";
      select.add(new Option("Your own file…", OWN_FILE));
      select.disabled = false;
      showVersion();
      $("own-message").className = "sm err";
      $("own-message").textContent = "Couldn't load the release list. Reload the page, or flash a file of your own.";
    });
});

// Step numbers follow the visible cards, since the board step hides for your own file
function numberSteps() {
  let n = 0;
  document.querySelectorAll(".card").forEach(function (card) {
    let step = card.querySelector(".step");
    if (step && card.style.display !== "none") step.textContent = ++n;
  });
}

function setManifest(url) {
  if (url) $("install").setAttribute("manifest", url);
  else $("install").removeAttribute("manifest");
  $("connect").disabled = !url;
}

function showVersion() {
  let value = $("version").value;
  let own = value === OWN_FILE;
  $("own").style.display = own ? "" : "none";
  $("release-notes").style.display = own ? "none" : "";
  $("board-card").style.display = own ? "none" : "";
  numberSteps();
  if (own) {
    setManifest(ownManifestUrl);
    return;
  }

  let entry = index.versions.filter(function (v) {
    return v.version === value;
  })[0];
  $("notes-title").textContent = "What's new in " + entry.version;
  let notes = entry.notes || "No release notes.";
  if (typeof renderMarkdown === "function") renderMarkdown($("notes"), notes);
  else $("notes").textContent = notes;
  showTargets(entry.targets);
}

function showTargets(targets) {
  // Keep the board picked before if this version has it, else the recommended one
  let chosen =
    targets.filter(function (t) {
      return t.target === chosenTarget;
    })[0] ||
    targets.filter(function (t) {
      return t.recommended;
    })[0] ||
    targets[0];
  let list = $("targets");
  list.textContent = "";
  targets.forEach(function (entry) {
    let item = document.createElement("li");
    item.innerHTML =
      "<label class='net'><input type='radio' name='target'><span class='grow'>" +
      "<b></b><small class='desc'></small><small class='mono'></small></span></label>";
    let input = item.querySelector("input");
    input.checked = entry === chosen;
    input.addEventListener("change", function () {
      chosenTarget = entry.target;
      setManifest("firmware/" + entry.manifest);
    });
    // Title and description come from each target's header (TARGET_TITLE, TARGET_DESCRIPTION)
    item.querySelector("b").textContent = entry.title || entry.target;
    if (entry.recommended) item.querySelector("b").insertAdjacentHTML("afterend", " <span class='chip ok'>Recommended</span>");
    item.querySelector(".desc").textContent = entry.description || "";
    item.querySelector(".mono").textContent = entry.target;
    list.appendChild(item);
  });
  $("pick-hint").style.display = chosen && chosen.recommended && targets.length > 1 ? "" : "none";
  setManifest(chosen ? "firmware/" + chosen.manifest : null);
}

/**
 * Checks that a picked file is a full flash image written at offset 0 (bootloader and partition
 * table included) and returns its ESP Web Tools chip family, or throws a message for the user.
 */
function inspectImage(name, bytes) {
  let hasTable = bytes.length > PARTITION_TABLE + 2 && bytes[PARTITION_TABLE] === 0xaa && bytes[PARTITION_TABLE + 1] === 0x50;
  // The bootloader sits at 0x1000 on ESP32 and ESP32-S2, at 0 on the newer chips
  let bootloader = bytes[0x1000] === IMAGE_MAGIC ? 0x1000 : bytes[0] === IMAGE_MAGIC ? 0 : -1;
  if (!hasTable || bootloader < 0) {
    if (/\.ota\.bin$/i.test(name))
      throw "This is an update file (.ota.bin) for a device's Settings page. Pick the .factory.bin from the same release.";
    if (bytes[0] === IMAGE_MAGIC)
      throw "This is only the firmware, without bootloader and partition table. Pick a full image such as the .factory.bin (or the build's merged.bin).";
    throw "This isn't a full ESP flash image. Pick a .factory.bin or merged.bin written at offset 0.";
  }
  let family = CHIP_FAMILIES[bytes[bootloader + 12] | (bytes[bootloader + 13] << 8)];
  if (!family) throw "This image is for a chip this installer doesn't know.";
  return family;
}

function pickOwnFile(file) {
  if (!file) return;
  let message = $("own-message");
  if (ownManifestUrl) {
    URL.revokeObjectURL(ownManifestUrl);
    ownManifestUrl = null;
  }
  setManifest(null);
  file.arrayBuffer().then(function (buffer) {
    let family;
    try {
      family = inspectImage(file.name, new Uint8Array(buffer));
    } catch (problem) {
      message.className = "sm err";
      message.textContent = problem;
      return;
    }
    // ESP Web Tools takes a manifest URL; for a local file both are blob: URLs
    let manifest = {
      name: file.name,
      version: "your file",
      new_install_prompt_erase: false,
      builds: [{ chipFamily: family, parts: [{ path: URL.createObjectURL(file), offset: 0 }] }],
    };
    ownManifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: "application/json" }));
    message.className = "sm";
    message.textContent = file.name + ": " + family + " image, " + Math.round(buffer.byteLength / 1024) + " KB. Ready to install.";
    if ($("version").value === OWN_FILE) setManifest(ownManifestUrl);
  });
}
