#pragma once

static const char wol_html[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title>Wake-on-LAN</title>
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
        padding: 10px;
        margin: 5px 0;
        border: 1px solid #ccc;
        border-radius: 3px;
        text-align: center;
      }
      .input-group button[type="submit"] {
        background: #333;
        color: #fff;
        cursor: pointer;
        width: 100%;
        font-size: medium;
        font-weight: 700;
      }
      .input-group button[type="submit"]:hover {
        background: #555;
      }
    </style>
  </head>
  <body>
    <div class="container">
      <h2>Turn On Device</h2>
      <form action="wol" method="POST">
        <div class="input-group">
          <label for="macAddress">MAC Address</label
          ><input
            class="input"
            type="text"
            id="macAddress"
            name="macAddress"
            required
            placeholder="XX:XX:XX:XX:XX:XX"
          />
        </div>
        <div class="input-group">
          <label for="secureOn">SecureOn (Optional)</label
          ><input
            class="input"
            type="text"
            id="secureOn"
            name="secureOn"
            placeholder="XX:XX:XX:XX:XX:XX"
          />
        </div>
        <div class="input-group">
          <label for="broadcastAddress">Broadcast IP (Optional)</label
          ><input
            class="input"
            type="text"
            id="broadcastAddress"
            name="broadcastAddress"
            placeholder="192.168.1.255"
          />
        </div>
        <div class="input-group">
          <label for="pin">Enter your PIN</label
          ><input class="input" type="password" id="pin" name="pin" required />
        </div>
        <div class="input-group">
          <button class="input" type="submit">WAKE UP</button>
        </div>
      </form>
    </div>
  </body>
</html>
)HTML";