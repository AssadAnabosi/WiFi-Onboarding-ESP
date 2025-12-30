const handleInputChange = (isTextInputVisible, isHidden) => {
  if (isTextInputVisible) {
    if (getById("modal-input-text").value.length <= 0) {
      getById("modal-button").disabled = true;
      return;
    } else {
      getById("modal-button").disabled = isHidden
        ? false
        : getById("modal-input").value.length < 8;
    }
    return;
  }
  getById("modal-button").disabled = isHidden
    ? false
    : getById("modal-input").value.length < 8;
};

function handleModal({
  title,
  input,
  isEnterprise,
  isHidden,
  buttonText,
  onclick,
}) {
  const isTextInputVisible = isEnterprise || isHidden;
  getById("modal-title").innerText = title;
  getById("modal-button").innerText = buttonText;
  getById("modal-button").classList.remove("red");
  if (input) {
    getById("modal-input").value = "";
    getById("modal-input-text").value = "";
    getById("modal-input").style.display = "block";
    if (isTextInputVisible) {
      getById("modal-input-text").placeholder = isEnterprise
        ? "Enter Username"
        : "Enter SSID";
      getById("modal-input-text").style.display = "block";
      getById("modal-input-text").focus();
    } else {
      getById("modal-input-text").style.display = "none";
      getById("modal-input").focus();
    }
    if (isTextInputVisible) {
      // Move focus to password input on Enter in text field
      getById("modal-input-text").addEventListener("keydown", function (event) {
        if (event.key === "Enter") {
          getById("modal-input").focus();
        }
      });

      getById("modal-input-text").onkeyup = function (event) {
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
      handleInputChange(isTextInputVisible, isHidden);
    };

    getById("modal-input-text").oninput = function () {
      handleInputChange(isTextInputVisible, isHidden);
    };

    getById("modal-button").disabled = true;
  } else {
    getById("modal-input").style.display = "none";
    if (isTextInputVisible) {
      getById("modal-input-text").style.display = "none";
    }
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
