let connections_status = {
  softAP: { enabled: false },
  station: { connected: false },
};

const iconWiFiOn =
  "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='icon icon-true'><path d='M12 20h.01'></path><path d='M2 8.82a15 15 0 0 1 20 0'></path><path d='M5 12.859a10 10 0 0 1 14 0'></path><path d='M8.5 16.429a5 5 0 0 1 7 0'></path></svg>";
const iconWiFiOff =
  "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round' class='icon icon-false'><path d='M12 20h.01'></path><path d='M8.5 16.429a5 5 0 0 1 7 0'></path><path d='M5 12.859a10 10 0 0 1 5.17-2.69'></path><path d='M19 12.859a10 10 0 0 0-2.007-1.523'></path><path d='M2 8.82a15 15 0 0 1 4.177-2.643'></path><path d='M22 8.82a15 15 0 0 0-11.288-3.764'></path><path d='m2 2 20 20'></path></svg>";

const getAPCard = (ap) =>
  `<div class='card'>${
    ap.enabled
      ? "<div class='card-badge badge-green'>Active</div>"
      : "<div class='card-badge badge-red'>Inactive</div>"
  }<div class='card-title'>Soft Access Point Mode</div><div class='card-description text-sm text-muted'>Access point for direct connection</div><div class='card-content'>${
    ap.enabled ? iconWiFiOn : iconWiFiOff
  }<div class='connection-details'>${
    ap.enabled
      ? `<div><p class='text-sm text-muted'>SSID</p><p class='font-medium'>${ap.ssid}</p></div><div><p class='text-sm text-muted'>IP Address</p><p class='font-medium'>${ap.ip}</p></div>`
      : "<div><p class='text-sm text-muted'>Access Point is disabled</p><p class='font-medium'>Enable it in the settings</p></div>"
  }</div></div></div>`;

const getStationCard = (ap) =>
  `<div class="card">${
    ap.connected
      ? "<div class='card-badge badge-green'>Connected</div>"
      : "<div class='card-badge badge-red'>Disconnected</div>"
  }<div class="card-title">Station Mode</div><div class="card-description">Connection to existing WiFi network</div><div class="card-content">${
    ap.connected ? iconWiFiOn : iconWiFiOff
  }<div class="connection-details">${
    ap.connected
      ? `<div><p class="text-sm text-muted">Connected to</p><p class="font-medium">${ap.ssid}</p></div><div><p class="text-sm text-muted" style="margin-top: 0.25rem">IP Address</p><p class="font-medium">${ap.ip}</p></div>`
      : '<div><p class="text-sm text-muted">Not connected to any network</p><p class="font-medium">Go to Scan page to connect</p></div>'
  }</div></div></div>`;

function draw() {
  let cardsContainer = document.getElementById("cards-container");
  cardsContainer.innerHTML =
    getAPCard(connections_status.softAP) +
    getStationCard(connections_status.station);
}

function load() {
  apiCall({
    endpoint: "/api/status",
    callback: function (res) {
      connections_status = JSON.parse(res);
      draw();
    },
    draw: draw,
  });
}
