#pragma once

static const char wol_html[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>Wake-on-LAN</title>

    <!-- Font Awesome -->
    <link
      rel="stylesheet"
      href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.5.1/css/all.min.css"
      integrity="sha512-DTOQO9RWCH3ppGqcWaEA1BIZOC6xxalwEsw9c2QQeAIftl+Vegovlnee1c9QX4TctnWMn13TZye+giMm8e2LwA=="
      crossorigin="anonymous"
      referrerpolicy="no-referrer"
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
        position: relative;
        background: #fff;
        padding: 20px;
        border-radius: 5px;
        box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
        width: 300px;
        text-align: center;
        max-height: calc(100vh - 40px);
        overflow-y: auto;
      }
      .container h2 {
        margin: 0;
        padding: 0;
      }
      .input-group {
        margin: 15px 0;
      }
      .input-group label {
        display: block;
        text-align: left;
        margin: 5px;
      }
      .input {
        width: 100%;
        padding: 10px;
        border: 1px solid #ccc;
        border-radius: 3px;
        text-align: center;
        box-sizing: border-box;
      }
      .input-group button[type="submit"] {
        background: #333;
        color: #fff;
        cursor: pointer;
        width: 100%;
        font-size: medium;
        font-weight: 700;
        border: none;
        transition: background 0.2s ease;
      }
      .input-group button[type="submit"]:hover {
        background: #555;
      }
      /* PIN field with inline icon */
      .pin-wrapper {
        position: relative;
      }
      .pin-wrapper .input {
        padding-right: 38px; /* space for icon */
      }
      .paste-icon {
        position: absolute;
        right: 10px;
        top: 50%;
        transform: translateY(-50%);
        color: #aaa;
        cursor: pointer;
        font-size: 1rem;
        transition: color 0.2s ease;
      }
      .paste-icon:hover {
        color: #333;
      }
      /* Visit status page */
      .top-right-icon {
        position: absolute;
        top: 15px;
        right: 15px;
        color: #333;
        font-size: 1.2rem;
        text-decoration: none;
        transition: color 0.2s ease;
      }

      .top-right-icon:hover {
        color: #007bff; /* changes color on hover */
      }

    </style>
  </head>
  <body>
    <div class="container">
      <a href="serviceCheck" class="top-right-icon" title="Go to status page">
        <i class="fa-solid fa-arrow-right-to-bracket"></i>
      </a>
      <h2>Turn On Device</h2>
      <form action="wol" method="POST">
        <div class="input-group">
          <label for="macAddress">MAC Address</label>
          <input
            class="input"
            type="text"
            id="macAddress"
            name="macAddress"
            required
            placeholder="XX:XX:XX:XX:XX:XX"
          />
        </div>
        <div class="input-group">
          <label for="secureOn">SecureOn (Optional)</label>
          <input
            class="input"
            type="text"
            id="secureOn"
            name="secureOn"
            placeholder="XX:XX:XX:XX:XX:XX"
          />
        </div>
        <div class="input-group">
          <label for="broadcastAddress">Broadcast IP (Optional)</label>
          <input
            class="input"
            type="text"
            id="broadcastAddress"
            name="broadcastAddress"
            placeholder="192.168.1.255"
          />
        </div>
        <div class="input-group">
          <label for="pin">Enter your PIN</label>
          <div class="pin-wrapper">
            <input class="input" type="password" id="pin" name="pin" required />
            <i
              class="fa-solid fa-clipboard paste-icon"
              onclick="pastePin()"
              title="Paste PIN"
              aria-label="Paste PIN"
              role="button"
              tabindex="0"
            ></i>
          </div>
        </div>
        <div class="input-group">
          <button class="input" type="submit">WAKE UP</button>
        </div>
      </form>
    </div>
    <script>
      async function pastePin() {
        try {
          const text = await navigator.clipboard.readText();
          document.getElementById("pin").value = text;
        } catch (err) {
          alert("Clipboard access failed. Please paste manually.");
        }
      }
    </script>
  </body>
</html>
)HTML";