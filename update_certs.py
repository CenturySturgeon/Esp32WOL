import base64
from dotenv import load_dotenv
import os
import requests
import sys

load_dotenv()

ESP32_HOST = os.getenv('ACCESS_DOMAIN')  # domain name | esp32.local | device IP
CERT_UPDATE_KEY = os.getenv("CERT_UPDATE_KEY")  # Paste the key from .env here

def push_certificates(cert_der_path, key_der_path):
    print("Loading DER files...")
    with open(cert_der_path, "rb") as f:
        cert_b64 = base64.b64encode(f.read()).decode()
    with open(key_der_path, "rb") as f:
        key_b64 = base64.b64encode(f.read()).decode()

    payload = {
        "certificate": cert_b64,
        "private_key": key_b64
    }

    headers = {
        "X-Cert-Key": CERT_UPDATE_KEY,
        "Content-Type": "application/json"
    }

    print(f"Sending to https://{ESP32_HOST}/admin/update-certs ...")
    # verify=False bypasses local cert validation during the transition. 
    # Once updated, you can remove it or point to your CA bundle.
    resp = requests.post(
        f"https://{ESP32_HOST}/admin/update-certs",
        json=payload,
        headers=headers,
        verify=False
    )

    if resp.status_code == 200:
        print("✅ Success:", resp.json())
    else:
        print(f"❌ Failed ({resp.status_code}):", resp.text)
        sys.exit(1)

if __name__ == "__main__":
    print(os.getcwd())
    push_certificates("./new_server.der", "./new_server_key.der")