#ifndef CONFIG_H
#define CONFIG_H

// Debug
#define DEBUG_MODE true
#define DEBUG(x) do { if (DEBUG_MODE) Serial.println(x); } while (0)
#define DEBUGF(...) do { if (DEBUG_MODE) Serial.printf(__VA_ARGS__); } while (0)
#define DEBUGL(x) do { if (DEBUG_MODE) Serial.print(x); } while (0)

// NTP Servers
#define NTP_SERVER_1 "a.st1.ntp.br"
#define NTP_SERVER_2 "time.nist.gov"
#define NTP_SERVER_3 "time.google.com"
#define GMT_OFFSET_SEC -10800
#define DAYLIGHT_OFFSET_SEC 0

// Firebase
#define API_KEY "AIzaSyAJtADhlZbuAywvHgIU5X9HOslAI8kRoyc"
#define DATABASE_URL "https://agroscan-c8a09-default-rtdb.firebaseio.com"
#define PROJECT_ID "agroscan-c8a09"
#define STORAGE_BUCKET "agroscan-c8a09.firebasestorage.app"
#define CREATE_DEVICE_URL "https://createdevicev2-5is4ontjcq-uc.a.run.app"

#endif
