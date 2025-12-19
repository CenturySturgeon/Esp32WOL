// envVariables.h
#ifndef ENVVARIABLES_H
#define ENVVARIABLES_H

// Include the types for the user sessions and the ESP8266WiFi.h to use the IPAddress class
#include "./utils/types.h"
#include <ESP8266WiFi.h>

// Your network credentials
const char* ssid = "{{WIFI_SSID}}";
const char* password = "{{WIFI_PASSWORD}}";

// Esp8266 static IP address
IPAddress staticIP{{STATIC_IP}};
// Your router's default local gateway
IPAddress gateway{{LOCAL_GATEWAY}};
// Your router's subnet
IPAddress subnet{{SUBNET}};

// Set the number of manageable user sessions (must match with the actual no. of sessions inside the userSessions array)
const int numUSessions = {{NUMBER_OF_SESSIONS}};

// User session array for the handling of session states
// User sessions variables are: Local username, hashed credentials, hmackey (for TOTP), and maximum session duration in seconds
UserSession userSessions[numUSessions] = {
  // Hash is made from the string "username:pin" (if the username is "admin" and the password is "1234" then you should get the hash for "admin:1234")
  {{USER_SESSIONS_ARRAY}}
};

// NOTE: Each user session has its own hmacKey, represented in the example above by the values inside the keys "{ 0xFF, 0xFF, ... 0xFF}". You can read more about this in the "Generating Requirements" section of the github repository.

// Address used to get your public IP
String ipSiteAddress = "{{IP_SITE_URL}}";
// POSIX Expiration date for the IP site certificate
const int ipSiteCertExp = {{IP_SITE_CERT_EXPIRATION}};

// POSIX Expiration date for the Esp8266 certificate
const int serverCertExp = {{SERVER_CERT_EXPIRATION}};

// POSIX Expiration date for the Telegram API certificate
const int telegramCertExp = {{TELEGRAM_CERT_EXPIRATION}};

// Initial value for the dailyChecks marker, this is used to perform functions that need to run once per day
unsigned long dailyCheckTime = millis();
// Warning time interval before certificate expiration (604800 seconds = 1 week)
const int certExpWarnInterval = 604800;
bool ipSiteWarnSent = false;
bool serverWarnSent = false;
bool telegramWarnSent = false;

// Your telegram bot API token
String BOT_TOKEN = "{{BOT_TOKEN}}";
// Your telegram user id
String CHAT_ID = "{{CHAT_ID}}";

// Cert for ssl connection over HTTPS
static const char serverCert[] PROGMEM = R"EOF(
{{SERVER_CERTIFICATE}}
)EOF";

// Private key for ssl connection over HTTPS
static const char serverKey[] PROGMEM =  R"EOF(
{{PRIVATE_KEY}}
)EOF";


const char telegramRootCert [] PROGMEM = R"CERT(
{{TELEGRAM_CERT}}
)CERT";

const char ipSiteRootCert [] PROGMEM = R"CERT(
{{IP_SITE_CERT}}
)CERT";

#endif