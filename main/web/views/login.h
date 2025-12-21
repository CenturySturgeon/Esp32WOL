#pragma once

static const char login_html[] = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Login Page</title>
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
            overflow: hidden; /* Disable vertical scrolling */
        }
        .container {
            background: #fff;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
            width: 300px;
            text-align: center;
            max-height: calc(100vh - 40px); /* Adjust max-height to fit the viewport */
            overflow-y: auto; /* Enable vertical scrolling if content exceeds viewport */
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
            font-weight: bold;
        }
        .input-group button[type="submit"]:hover {
            background: #555;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Login</h2>
        <form action="login" method="POST">
            <div class="input-group">
                <label for="username">Username</label>
                <input class="input" type="text" id="username" name="username" placeholder="Max 16 Characters" required>
            </div>
            <div class="input-group">
                <label for="password">Password</label>
                <input class="input" type="password" id="password" name="password" placeholder="Max 16 Characters" required>
            </div>
            <div class="input-group">
                <button class="input" type="submit">SUBMIT</button>
            </div>
        </form>
    </div>
</body>
</html>
)HTML";