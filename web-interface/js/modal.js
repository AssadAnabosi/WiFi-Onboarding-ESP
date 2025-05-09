function handleModal({ title, input, buttonText, onclick }) {
  getById("modal-title").innerText = title;
  getById("modal-button").innerText = buttonText;
  getById("modal-button").classList.remove("red");
  if (input) {
    getById("modal-input").style.display = "block";
    getById("modal-input").value = "";
    getById("modal-input").focus();

    getById("modal-input").addEventListener("keydown", function (event) {
      if (event.key === "Enter") {
        getById("modal-button").click();
      }
    });

    getById("modal-input").onkeyup = function (event) {
      if (event.key === "Enter") {
        getById("modal-button").click();
      }
    };

    getById("modal-input").oninput = function () {
      if (getById("modal-input").value.length >= 8) {
        getById("modal-button").disabled = false;
      } else {
        getById("modal-button").disabled = true;
      }
    };

    getById("modal-button").disabled = true;
  } else {
    getById("modal-input").style.display = "none";
    getById("modal-button").disabled = false;
    getById("modal-button").classList.add("red");
  }

  // Display the modal
  getById("modal-container").style.display = "block";
  getById("close-modal").onclick = function () {
    getById("modal-container").style.display = "none";
  };
  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape") {
      getById("modal-container").style.display = "none";
    }
  });
  window.onclick = function (event) {
    if (event.target == getById("modal-container")) {
      getById("modal-container").style.display = "none";
    }
  };

  getById("modal-button").onclick = function () {
    if (onclick) {
      onclick();
    }
    getById("modal-container").style.display = "none";
  };
}
