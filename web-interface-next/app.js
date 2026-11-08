// Shared by every page: theme, header/footer, status pill, "Finishing update"
// bar, dialog and toasts. Replaces index.js' UI parts, modal.js and toast.js.
// Loaded in <head> without defer so the theme is set before the first paint.

(function () {
  let saved = null;
  try {
    saved = localStorage.getItem("theme");
  } catch (e) {}
  let dark = saved ? saved === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
  document.documentElement.dataset.theme = dark ? "dark" : "light";
})();

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

// The existing Wi-Fi on / Wi-Fi off icons, unchanged; the size comes from the caller
function svg(paths, size) {
  return (
    "<svg width='" + (size || 24) + "' height='" + (size || 24) +
    "' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>" +
    paths + "</svg>"
  );
}
const WIFI_ON =
  "<path d='M12 20h.01'/><path d='M2 8.82a15 15 0 0 1 20 0'/><path d='M5 12.859a10 10 0 0 1 14 0'/><path d='M8.5 16.429a5 5 0 0 1 7 0'/>";
const WIFI_OFF =
  "<path d='M12 20h.01'/><path d='M8.5 16.429a5 5 0 0 1 7 0'/><path d='M5 12.859a10 10 0 0 1 5.17-2.69'/><path d='M19 12.859a10 10 0 0 0-2.007-1.523'/><path d='M2 8.82a15 15 0 0 1 4.177-2.643'/><path d='M22 8.82a15 15 0 0 0-11.288-3.764'/><path d='m2 2 20 20'/>";
// Half-filled circle for the theme toggle (the only new icon)
const THEME = "<circle cx='12' cy='12' r='9'/><path d='M12 3a9 9 0 0 1 0 18z' fill='currentColor'/>";

function toggleTheme() {
  let next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  document.documentElement.dataset.theme = next;
  try {
    localStorage.setItem("theme", next);
  } catch (e) {}
}

// Header, footer, dialog and toast host are built here once instead of being
// repeated in every HTML file. <body data-page="home" data-version="{{VERSION}}">
function buildChrome() {
  let body = document.body;
  let page = body.dataset.page;
  // The device serves pages without ".html"; the static mock needs it
  let ext = /\.html$/.test(location.pathname) ? ".html" : "";
  let tabs = page
    ? "<nav class='wrap tabs'>" +
      [["home", "Home"], ["scan", "Scan"], ["settings", "Settings"]]
        .map(function (t) {
          return "<a href='" + t[0] + ext + "'" + (t[0] === page ? " aria-current=page" : "") + ">" + t[1] + "</a>";
        })
        .join("") +
      "</nav>"
    : "";
  body.insertAdjacentHTML(
    "afterbegin",
    "<header><div class='wrap row between top'><a class='brand' href='home" + ext + "'>" +
      svg(WIFI_ON, 22) +
      "<span id='hostname'>Wi-Fi On-Boarding</span></a><div class='row'><span id='conn' class='pill' data-s='busy'>Loading</span>" +
      "<button class='btn icon-btn' onclick='toggleTheme()' aria-label='Toggle light/dark theme' title='Theme'>" +
      svg(THEME, 20) +
      "</button></div></div>" +
      tabs +
      "<div id='finishing' class='bar'></div></header>"
  );
  body.insertAdjacentHTML(
    "beforeend",
    "<footer>Created by <a href='https://github.com/assadanabosi'>Assad Anabosi</a> · <span id='fw-version'>" +
      (body.dataset.version ? "v" + esc(body.dataset.version) : "") +
      "</span></footer><div id='toasts'></div>" +
      "<dialog id='dlg'><form method='dialog'><h2 id='dlg-title'></h2><p id='dlg-text' class='muted'></p>" +
      "<input id='dlg-user' autocomplete='off'><input id='dlg-pass' type='password' placeholder='Password' autocomplete='off'>" +
      "<label id='dlg-show' class='row sm muted'><input type='checkbox' class='switch' onchange=\"$('dlg-pass').type=this.checked?'text':'password'\">Show password</label>" +
      // OK comes first so Enter picks it; .actions shows it on the right
      "<div class='actions'><button id='dlg-ok' class='btn primary' value='ok'></button><button class='btn' value='cancel'>Cancel</button></div></form></dialog>"
  );
}
document.addEventListener("DOMContentLoaded", function () {
  buildChrome();
  let notice = $("notice-icon");
  if (notice) notice.innerHTML = svg(WIFI_ON, 26);
});

// "connected" | "loading" | "rebooting" | "error" (same names as setStatusBanner)
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
  bar.style.display = updateFinishing ? "block" : "none";
  bar.textContent = "Finishing update, " + seconds + " s. Some actions are paused until then.";
  document.querySelectorAll(".lock-while-finishing").forEach(function (el) {
    el.disabled = updateFinishing;
  });
  if (typeof refreshFirmwareButtons !== "undefined") refreshFirmwareButtons();
}

function toast(message, ms) {
  let el = document.createElement("div");
  el.className = "toast";
  el.textContent = message;
  $("toasts").prepend(el);
  setTimeout(function () {
    el.remove();
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
  pass.style.display = $("dlg-show").style.display = opts.pass ? "" : "none";
  user.value = pass.value = "";
  pass.type = "password";
  $("dlg-show").querySelector("input").checked = false;
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
