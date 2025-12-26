function handleModal({ title, input, isEnterprise, buttonText, onclick }) {
  getById("modal-title").innerText = title;
  getById("modal-button").innerText = buttonText;
  getById("modal-button").classList.remove("red");
  if (input) {
    getById("modal-input").value = "";
    getById("modal-input-username").value = "";
    getById("modal-input").style.display = "block";
    if (isEnterprise) {
      getById("modal-input-username").style.display = "block";
      getById("modal-input-username").focus();
    } else {
      getById("modal-input-username").style.display = "none";
      getById("modal-input").focus();
    }
    if (isEnterprise) {
      // Move focus to password input on Enter in username field
      getById("modal-input-username").addEventListener(
        "keydown",
        function (event) {
          if (event.key === "Enter") {
            getById("modal-input").focus();
          }
        }
      );

      getById("modal-input-username").onkeyup = function (event) {
        if (event.key === "Enter") {
          getById("modal-input").focus();
        }
      };
    }

    // Enable Enter key to submit
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
      if (isEnterprise) {
        // For enterprise, enable button if username and password are non-empty
        if (
          getById("modal-input-username").value.length > 0 &&
          getById("modal-input").value.length >= 8
        ) {
          getById("modal-button").disabled = false;
        } else {
          getById("modal-button").disabled = true;
        }
      } else {
        if (getById("modal-input").value.length >= 8) {
          getById("modal-button").disabled = false;
        } else {
          getById("modal-button").disabled = true;
        }
      }
    };

    getById("modal-button").disabled = true;
  } else {
    getById("modal-input").style.display = "none";
    getById("modal-input-username").style.display = "none";
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
