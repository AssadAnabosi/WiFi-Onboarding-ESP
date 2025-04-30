let toastContainer;

function toast(message, DISPLAY_DURATION = 3000) {
  if (!toastContainer) {
    toastContainer = document.createElement("div");
    toastContainer.classList.add("toast-container");
    document.body.appendChild(toastContainer);
  }

  const toastDiv = document.createElement("div");
  toastDiv.classList.add("toast");
  toastDiv.innerText = message;
  toastContainer.prepend(toastDiv);

  setTimeout(() => toastDiv.classList.add("show"), 10);
  setTimeout(() => toastDiv.classList.remove("show"), DISPLAY_DURATION);
  setTimeout(
    () => toastContainer.removeChild(toastDiv),
    DISPLAY_DURATION + 500
  );
}
