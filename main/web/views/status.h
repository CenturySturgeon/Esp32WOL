#pragma once

static const char status_html[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>Request Status</title>
    <link
      rel="stylesheet"
      href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.3/css/all.min.css"
    />
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #282c34;
        margin: 0;
        padding: 0;
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100vh;
        overflow: hidden;
      }
      .container {
        background: #fff;
        padding: 20px;
        border-radius: 5px;
        box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
        width: 300px;
        text-align: center;
        position: relative;
        max-height: calc(100vh - 40px);
        overflow-y: auto;
      }
      .mark {
        width: 100px;
        height: 100px;
        border-radius: 50%;
        color: #fff;
        display: flex;
        justify-content: center;
        align-items: center;
        margin: 0 auto 20px;
      }
      .succes-mark {
        background-color: #4caf50;
      }
      .error-mark {
        background-color: #ff5733;
      }
      .mark i {
        font-size: 3em;
      }
      h1 {
        color: #333;
      }
      .hidden {
        display: none;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <div id="success" class="hidden">
        <div class="mark succes-mark"><i class="fas fa-check"></i></div>
        <h1>Success!</h1>
      </div>
      <div id="error" class="hidden">
        <div class="mark error-mark"><i class="fas fa-times"></i></div>
        <h1>Error</h1>
      </div>
      <p id="dynamic-message"></p>
    </div>
  </body>
  <script defer="defer">
    // Function to check if the URL contains the word "success"
    function urlContainsSuccess() {
      return window.location.href.includes("success");
    }

    // Function to show or hide elements based on the URL
    function toggleElementsVisibility() {
      const successClass = document.getElementById("success");
      const errorClass = document.getElementById("error");

      if (urlContainsSuccess()) {
        successClass.classList.remove("hidden");
        errorClass.classList.add("hidden");
      } else {
        successClass.classList.add("hidden");
        errorClass.classList.remove("hidden");
      }
    }

    toggleElementsVisibility();

    const urlParams = new URLSearchParams(window.location.search);
    const dynamicMessage = urlParams.get("message") || "";
    document.getElementById("dynamic-message").textContent = dynamicMessage;
  </script>
</html>
)HTML";