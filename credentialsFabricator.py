import os
import json
import csv
from dotenv import load_dotenv
from CredentialUtils import UserSession
import getpass

load_dotenv()

wifi_name = os.getenv('WIFI_NAME')
wifi_password = os.getenv('WIFI_PASSWORD')
static_ip = os.getenv('STATIC_IP')
gateway = os.getenv('ROUTER_GATEWAY_IP')
mask = os.getenv('ROUTER_MASK')

telegram_bot_token = os.getenv('TELEGRAM_BOT_TOKEN')
telegram_chat_id = os.getenv('TELEGRAM_CHAT_ID')

totp_label = os.getenv('TOTP_LABEL')
totp_issuer = os.getenv('TOTP_ISSUER')
random_passwords = bool(os.getenv('SET_AUTO_RANDOM_PASSWORDS') in ['true', 'True'])
user_sessions_data = json.loads(os.getenv('USER_SESSIONS'))

static_ip_enabled = 0 # False by default
user_sessions = [
    UserSession(
        session['username'],
        int(session['timeout']),
        totp_label,
        totp_issuer
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
    
    if static_ip and gateway and mask:
        static_ip_enabled = 1 # 1 = True in the Esp32 code
        writer.writerow({"key":"static_ip", "type":"data", "encoding":"string","value":static_ip})
        writer.writerow({"key":"router_gw", "type":"data", "encoding":"string","value":gateway})
        writer.writerow({"key":"router_mask", "type":"data", "encoding":"string","value":mask})

    writer.writerow({"key":"use_static_ip", "type":"data", "encoding":"u8","value":static_ip_enabled})
    writer.writerow({"key":"bot_token", "type":"data", "encoding":"string","value":telegram_bot_token})
    writer.writerow({"key":"chat_id", "type":"data", "encoding":"string","value":telegram_chat_id})

    if not random_passwords:
        for i in range(len(user_sessions)):
            session = user_sessions[i]
            new_user_password = getpass.getpass(f"Enter password for user '{session.uname}': ")
            hashed_password = session.generate_sha256_hash(new_user_password)
            session.accessHash = hashed_password

    # Write the user sessions
    for i in range(len(user_sessions)):
        session = user_sessions[i]
        for row in session.to_csv_rows(user_index = i + 1):
            writer.writerow(row)

print("secrets.csv generated successfully")
