import os
import json
import csv
from dotenv import load_dotenv
from CredentialUtils import UserSession

load_dotenv()

wifi_name = os.getenv('WIFI_NAME')
wifi_password = os.getenv('WIFI_PASSWORD')
telegram_bot_token = os.getenv('TELEGRAM_BOT_TOKEN')
telegram_chat_id = os.getenv('TELEGRAM_CHAT_ID')
user_sessions_data = json.loads(os.getenv('USER_SESSIONS'))


user_sessions = [
    UserSession(
        session['username'],
        int(session['timeout']),
        session['totp_label'],
        session['issuer']
    )
    for session in user_sessions_data
]

# Write CSV
with open("secrets.csv", "w", newline="") as csvfile:
    fieldnames = ["key", "type", "encoding", "value"]
    writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

    writer.writeheader()
    writer.writerow({"key":"storage", "type":"namespace", "encoding":"","value":""})
    writer.writerow({"key":"total_users", "type":"data", "encoding":"u8","value":len(user_sessions)})
    writer.writerow({"key":"wifi_ssid", "type":"data", "encoding":"string","value":wifi_name})
    writer.writerow({"key":"wifi_pass", "type":"data", "encoding":"string","value":wifi_password})
    writer.writerow({"key":"bot_token", "type":"data", "encoding":"string","value":telegram_bot_token})
    writer.writerow({"key":"chat_id", "type":"data", "encoding":"string","value":telegram_chat_id})

    for i in range(len(user_sessions)):
        session = user_sessions[i]
        for row in session.to_csv_rows(user_index = i + 1):
            writer.writerow(row)

print("secrets.csv generated successfully")
