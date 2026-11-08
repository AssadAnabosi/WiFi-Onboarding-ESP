// Shared by every page: theme, header/footer, status pill, "Finishing update"
// bar, notes, dialog, password fields, firmware badge and toasts. api.js talks
// to the device. Loaded in <head> without defer so the theme is set before the
// first paint.

// Theme: the saved choice, else the system setting, else light
function savedTheme() {
  try {
    return localStorage.getItem("theme");
  } catch (e) {
    return null;
  }
}
function systemTheme() {
  try {
    return matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  } catch (e) {
    return "light";
  }
}
document.documentElement.dataset.theme = savedTheme() || systemTheme();
// Until the user picks a theme, follow the system setting when it changes
try {
  matchMedia("(prefers-color-scheme: dark)").addEventListener("change", function () {
    if (!savedTheme()) document.documentElement.dataset.theme = systemTheme();
  });
} catch (e) {}

function toggleTheme() {
  let next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  document.documentElement.dataset.theme = next;
  try {
    localStorage.setItem("theme", next);
  } catch (e) {}
}

function $(id) {
  return document.getElementById(id);
}

function esc(str) {
  return str == null
    ? ""
    : String(str).replace(/[&<>"'/]/g, function (c) {
        return "&#" + c.charCodeAt(0) + ";";
      });
}

// Icons share one 24x24 stroke style; the size comes from the caller.
// Wi-Fi and eye icons: Lucide (ISC license), see THIRD_PARTY_LICENSES.md
function svg(paths, size) {
  return (
    "<svg width='" + (size || 24) + "' height='" + (size || 24) +
    "' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' aria-hidden='true'>" +
    paths + "</svg>"
  );
}
// Wi-Fi on / Wi-Fi off
const WIFI_ON =
  "<path d='M12 20h.01'/><path d='M2 8.82a15 15 0 0 1 20 0'/><path d='M5 12.859a10 10 0 0 1 14 0'/><path d='M8.5 16.429a5 5 0 0 1 7 0'/>";
const WIFI_OFF =
  "<path d='M12 20h.01'/><path d='M8.5 16.429a5 5 0 0 1 7 0'/><path d='M5 12.859a10 10 0 0 1 5.17-2.69'/><path d='M19 12.859a10 10 0 0 0-2.007-1.523'/><path d='M2 8.82a15 15 0 0 1 4.177-2.643'/><path d='M22 8.82a15 15 0 0 0-11.288-3.764'/><path d='m2 2 20 20'/>";
// Theme toggle: half-filled circle
const THEME = "<circle cx='12' cy='12' r='9'/><path d='M12 3a9 9 0 0 1 0 18z' fill='currentColor'/>";
// Password visibility: eye / eye off
const EYE =
  "<path d='M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0'/><circle cx='12' cy='12' r='3'/>";
const EYE_OFF =
  "<path d='M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49'/><path d='M14.084 14.158a3 3 0 0 1-4.242-4.242'/><path d='M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143'/><path d='m2 2 20 20'/>";

// Header, footer, dialog and toast host are built here once instead of being
// repeated in every HTML file. <body data-page="home" data-version="{{VERSION}}">
function buildChrome() {
  let body = document.body;
  let page = body.dataset.page;
  let tabs = page
    ? "<nav class='wrap tabs'>" +
      [["home", "Home"], ["scan", "Scan"], ["settings", "Settings"]]
        .map(function (t) {
          return "<a href='" + t[0] + "'" + (t[0] === page ? " aria-current=page" : "") + ">" + t[1] + "</a>";
        })
        .join("") +
      "</nav>"
    : "";
  body.insertAdjacentHTML(
    "afterbegin",
    "<header><div class='wrap row between top'><a class='brand' href='home'>" +
      svg(WIFI_ON, 22) +
      "<span>Wi-Fi On-Boarding</span></a><div class='row'><span id='conn' class='pill' data-s='busy'>Loading</span>" +
      "<button class='btn icon-btn' onclick='toggleTheme()' aria-label='Toggle light/dark theme' title='Theme'>" +
      svg(THEME, 20) +
      "</button></div></div>" +
      tabs +
      // Slides out from under the tabs (.bar.show)
      "<div id='finishing' class='bar' role='status'><div></div></div></header>"
  );
  body.insertAdjacentHTML(
    "beforeend",
    "<footer>Created by <a href='https://github.com/assadanabosi'>Assad Anabosi</a> · <span id='fw-version'>" +
      (body.dataset.version ? "v" + esc(body.dataset.version) : "") +
      "</span></footer><div id='toasts'></div>" +
      "<dialog id='dlg'><form method='dialog'><h2 id='dlg-title'></h2><p id='dlg-text' class='muted'></p>" +
      "<input id='dlg-user' autocomplete='off'><input id='dlg-pass' type='password' placeholder='Password' autocomplete='off'>" +
      // OK comes first so Enter picks it; .actions shows it on the right
      "<div class='actions'><button id='dlg-ok' class='btn primary' value='ok'></button><button class='btn' value='cancel'>Cancel</button></div></form></dialog>"
  );
}

// Puts a show/hide eye button inside a password field
function addEye(input) {
  let wrap = document.createElement("span");
  wrap.className = "pw";
  input.replaceWith(wrap);
  wrap.appendChild(input);
  let eye = document.createElement("button");
  eye.type = "button";
  eye.className = "eye";
  eye.onclick = function () {
    setPasswordVisible(input, input.type === "password");
    input.focus();
  };
  wrap.appendChild(eye);
  setPasswordVisible(input, false);
}
function setPasswordVisible(input, visible) {
  let eye = input.nextElementSibling;
  input.type = visible ? "text" : "password";
  eye.innerHTML = svg(visible ? EYE_OFF : EYE, 18);
  eye.setAttribute("aria-label", visible ? "Hide password" : "Show password");
  eye.title = eye.getAttribute("aria-label");
}

document.addEventListener("DOMContentLoaded", function () {
  buildChrome();
  document.querySelectorAll("input[type=password]").forEach(addEye);
  let notice = $("notice-icon");
  if (notice) notice.innerHTML = svg(WIFI_ON, 26);
});

// "connected" | "loading" | "rebooting" | "error"
function setStatus(state) {
  let pill = $("conn");
  if (!pill) return;
  let map = {
    connected: ["ok", "Connected"],
    loading: ["busy", "Loading"],
    rebooting: ["busy", "Rebooting"],
    error: ["error", "Disconnected"],
  };
  let s = map[state] || ["error", "Unknown"];
  pill.dataset.s = s[0];
  pill.textContent = s[1];
}

// While new firmware has not confirmed itself the device refuses reboots, saves,
// resets and updates: show a countdown and disable those controls
var updateFinishing = false;
function setFinishing(seconds) {
  let bar = $("finishing");
  updateFinishing = seconds > 0;
  bar.classList.toggle("show", updateFinishing);
  // Keep the last text while the bar slides away
  if (updateFinishing) bar.firstChild.textContent = "Finishing update, " + seconds + " s. Some actions are paused until then.";
  document.querySelectorAll(".lock-while-finishing").forEach(function (el) {
    el.disabled = updateFinishing;
  });
  if (typeof refreshFirmwareButtons !== "undefined") refreshFirmwareButtons();
}

// Notes (update available, mismatch) slide in and out; empty text hides them
function showNote(id, text, kind) {
  let note = $(id);
  if (text) note.textContent = text;
  if (kind !== undefined) note.className = "note " + kind;
  note.classList.toggle("show", !!text);
}

/**
 * Firmware badge [text, chip class] for /api/update/status check and the dev-build flag:
 * an available update wins, then "Dev build", then "Up to date". Dev ones use the warning colour.
 */
function firmwareBadge(check, devBuild) {
  if (check && check.state === "available") return check.dev ? ["Dev update available", "warn"] : ["Update available", "ok"];
  if (devBuild) return ["Dev build", "warn"];
  if (check && check.state === "up_to_date") return ["Up to date", "ok"];
  return ["", ""];
}

// Home: "Update vX.Y.Z available" note, in the warning colour for a dev update
function showUpdateNote(check) {
  let available = check && check.state === "available";
  showNote(
    "update-badge",
    available ? (check.dev ? "Dev update v" : "Update v") + check.version + " available, open Settings to install →" : "",
    available && check.dev ? "" : "info"
  );
}

function toast(message, ms) {
  let el = document.createElement("div");
  el.className = "toast";
  el.textContent = message;
  $("toasts").prepend(el);
  setTimeout(function () {
    el.classList.add("out");
    setTimeout(function () {
      el.remove();
    }, 200);
  }, ms || 3000);
}

/**
 * One dialog for confirmations and credential prompts.
 * opts: {title, text, ok, danger, user: placeholder|null, pass: bool, minPass, onOk(user, pass)}
 */
function ask(opts) {
  let dlg = $("dlg");
  let user = $("dlg-user");
  let pass = $("dlg-pass");
  let ok = $("dlg-ok");
  $("dlg-title").textContent = opts.title;
  $("dlg-text").textContent = opts.text || "";
  $("dlg-text").style.display = opts.text ? "" : "none";
  user.style.display = opts.user ? "" : "none";
  user.placeholder = opts.user || "";
  pass.parentNode.style.display = opts.pass ? "" : "none";
  user.value = pass.value = "";
  setPasswordVisible(pass, false);
  ok.textContent = opts.ok;
  ok.className = "btn primary" + (opts.danger ? " danger" : "");
  let check = function () {
    ok.disabled = (opts.user && !user.value) || (opts.pass && pass.value.length < (opts.minPass || 0));
  };
  user.oninput = pass.oninput = check;
  check();
  dlg.onclose = function () {
    if (dlg.returnValue === "ok" && opts.onOk) opts.onOk(user.value, pass.value);
  };
  dlg.returnValue = "";
  dlg.showModal();
  (opts.user ? user : opts.pass ? pass : ok).focus();
}
