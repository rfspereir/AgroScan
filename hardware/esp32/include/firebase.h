#ifndef FIREBASE_H
#define FIREBASE_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <config.h> 
#include <ca_bundle.h>
#include <ca_bundle_rtdb.h> 

//================== Provisionamento online ============

inline bool provisionarDispositivo(String &WIFI_SSID, String  &WIFI_PASSWORD,
                                    String &clienteId, String &dispositivoUID,
                                    String &email, String &senha, String &sn, String &mac) {
  
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  String url = CREATE_DEVICE_URL; 

  HTTPClient https;
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000); 

  StaticJsonDocument<512> json;
  json["sn"] = sn;
  json["mac"] = mac;

  String requestBody;
  serializeJson(json, requestBody);

  DEBUG("Enviando dados para o backend...");
  int httpCode = https.POST(requestBody);

  if (httpCode > 0) {
    String payload = https.getString(); 

    if (httpCode == 200) {
      StaticJsonDocument<1024> response;
      DeserializationError error = deserializeJson(response, payload);

      if (error) {
        DEBUG("Erro ao parsear resposta JSON");
        https.end();
        return false;
      }

      dispositivoUID = response["uid"].as<String>();
      clienteId = response["clienteId"].as<String>();
      email = response["email"].as<String>();
      senha = response["senha"].as<String>();

      StaticJsonDocument<1024> config;
      config["wifiSSID"] = WIFI_SSID;
      config["wifiPassword"] = WIFI_PASSWORD;
      config["clienteId"] = clienteId;
      config["dispositivoUID"] = dispositivoUID;
      config["email"] = email;
      config["senha"] = senha;
      config["sn"] = sn;
      config["mac"] = mac;

      File file = LittleFS.open("/config.json", "w");
      if (!file) {
        DEBUG("Erro ao abrir config.json para escrita.");
        https.end();
        return false; 
      }
      serializeJson(config, file);
      file.close();

      DEBUG("Provisionamento concluído com sucesso.");
      https.end();
      return true;
    } else {
      DEBUGF("Erro na requisição HTTPS: %s\n", https.errorToString(httpCode));
    }
  } else {
    DEBUGF("Erro na requisição HTTPS: %s\n", https.errorToString(httpCode));
  }
  
  https.end();
  return false;
}

// ================= Login no Firebase =================

inline bool loginFirebase(String email, String senha, String &idToken, String &refreshToken, String &localId) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  HTTPClient https;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + API_KEY;

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000);

  StaticJsonDocument<512> json;
  json["email"] = email;
  json["password"] = senha;
  json["returnSecureToken"] = true;

  String requestBody;
  serializeJson(json, requestBody);

  DEBUG("Enviando requisição de login...");
  int httpCode = https.POST(requestBody);

  if (httpCode > 0) {
    String payload = https.getString();
    // DEBUG("Resposta do login:");
    // DEBUG(payload);

    StaticJsonDocument<1024> response;
    DeserializationError error = deserializeJson(response, payload);

    if (!error) {
      idToken = response["idToken"].as<String>();
      refreshToken = response["refreshToken"].as<String>();
      localId = response["localId"].as<String>();
      https.end();
      return true;
    } else {
      DEBUG("Erro ao parsear JSON do login.");
    }
  } else {
    DEBUGF("Erro HTTP na requisição de login: %s\n", https.errorToString(httpCode));
  }

  https.end();
  return false;
}

// ================= Refresh Firebase Token =================

inline bool refreshIdToken(const String &apiKey, String &refreshToken, String &idToken, String &localId) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  HTTPClient https;
  String url = String("https://securetoken.googleapis.com/v1/token?key=") + apiKey;

  https.begin(client, url);
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String requestBody = String("grant_type=refresh_token&refresh_token=") + refreshToken;

  int httpCode = https.POST(requestBody);

  if (httpCode > 0) {
    String payload = https.getString();
    // DEBUG("Resposta do refresh:");
    // DEBUG(payload);

    StaticJsonDocument<1024> response;
    DeserializationError error = deserializeJson(response, payload);

    if (!error) {
      idToken = response["access_token"].as<String>();
      refreshToken = response["refresh_token"].as<String>();
      localId = response["user_id"].as<String>();
      https.end();
      return true;
    } else {
      DEBUG("Erro parse JSON do refresh.");
    }
  } else {
    DEBUGF("Erro HTTP no refresh: %s\n", https.errorToString(httpCode).c_str());
  }

  https.end();
  return false;
}

// ================= Upload para Firebase Storage =================

inline bool uploadToFirebaseStorage(const String &bucket, const String &remotePath, const String &localFilePath, const String &idToken) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  String url = String("https://firebasestorage.googleapis.com/v0/b/") + bucket + "/o?name=" + remotePath;
  String authHeader = String("Bearer ") + String(idToken);
  HTTPClient https;
  https.begin(client, url);
  https.addHeader("Authorization", authHeader);
  https.addHeader("Content-Type", "image/jpeg");
  https.setTimeout(10000); 



  DEBUGF("Abrindo arquivo...: %s\n", localFilePath.c_str());
  File file = LittleFS.open(localFilePath, "r");
  if (!file) {
    DEBUG("Falha ao abrir arquivo para upload.");
    https.end();
    return false;
  }
  
  int tamanho = file.size();
  uint8_t *buffer = (uint8_t *)malloc(tamanho);
  if (!buffer) {
    DEBUG("Falha ao alocar buffer.");
    file.close();
    https.end();
    return false;
  }

  file.read(buffer, tamanho);
  file.close();
  DEBUG("Iniciando upload...");
  int httpResponseCode = https.sendRequest("POST", buffer, tamanho);
  free(buffer);

  if (httpResponseCode > 0) {
    String response = https.getString();
    // DEBUG("Resposta Storage:");
    // DEBUG(response);
    DEBUG("upload concluído com sucesso.");
    https.end();
    return true;
  } else {
    DEBUGF("Erro upload: %s\n", https.errorToString(httpResponseCode));
    https.end();
    return false;
  }
}

// ================= Gravação no Realtime Database =================

inline bool writeToFirebaseRTDB(const String &databaseURL, const String &path, const String &idToken, const JsonDocument &json) {
  WiFiClientSecure client;
  // client.setCACert(CA_BUNDLE);
  client.setInsecure();

  String url = databaseURL + "/" + path + ".json?auth=" + idToken;

  // DEBUGF("URL RTDB: %s\n", url.c_str());

  HTTPClient https;
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000);

  String requestBody;
  serializeJson(json, requestBody);

  int httpResponseCode = https.PUT(requestBody);

  if (httpResponseCode > 0) {
    String response = https.getString();
    DEBUG("Resposta RTDB:");
    DEBUG(response.c_str());
    https.end();
    return true;
  } else {
    DEBUGF("Erro RTDB: %s\n", https.errorToString(httpResponseCode).c_str());
    https.end();
    return false;
  }
}

#endif

