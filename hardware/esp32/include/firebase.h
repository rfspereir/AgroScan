#ifndef FIREBASE_H
#define FIREBASE_H

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <config.h> 
#include <ca_bundle.h>

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

inline bool uploadToFirebaseStorage(const String &bucket, const String &remotePath, camera_fb_t* fb, const String &idToken) {
    WiFiClientSecure client;
    client.setCACert(CA_BUNDLE);
    client.setTimeout(5000);

    const char* host = "firebasestorage.googleapis.com";
    const int httpsPort = 443;

    if (!client.connect(host, httpsPort)) {
        DEBUG("Falha na conexão com o Firebase Storage");
        return false;
    }

    size_t fileSize = fb->len;
    DEBUGF("Enviando imagem. Tamanho: %d bytes\n", fileSize);

    String url = "/v0/b/" + bucket + "/o?name=" + remotePath + "&uploadType=media";

    String header = String("POST ") + url + " HTTP/1.1\r\n" +
                    "Host: " + host + "\r\n" +
                    "Authorization: Bearer " + idToken + "\r\n" +
                    "Content-Type: image/jpeg\r\n" +
                    "Content-Length: " + String(fileSize) + "\r\n" +
                    "Connection: close\r\n\r\n";

    client.print(header);

    const size_t chunkSize = 4096;
    size_t sent = 0;

    while (sent < fileSize) {
        size_t toSend = min(chunkSize, fileSize - sent);
        client.write(fb->buf + sent, toSend);
        sent += toSend;
        float progress = (sent * 100.0) / fileSize;
        DEBUGF("Upload: %.2f%%\n", progress);
        delay(5);
    }
    DEBUG("Upload enviado. Aguardando resposta...");

    // Lê status HTTP
    bool httpOk = false;
    bool erroEncontrado = false;
    String buffer = "";

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();

        if (line.startsWith("HTTP/1.1 200") || line.startsWith("HTTP/1.1 2")) {
            httpOk = true;
        }

        if (line.length() == 0) break;
    }

    while (client.available()) {
        char c = client.read();
        buffer += c;

        // Checagem incremental para "error"
        if (buffer.length() >= 7) {
            if (buffer.indexOf("\"error\"") >= 0) {
                erroEncontrado = true;
                break;
            }
            if (buffer.length() > 128) buffer = buffer.substring(buffer.length() - 7);  // mantenha só o final
        }
    }
    client.flush();
    delay(50);
    client.stop();

    if (!httpOk || erroEncontrado) {
        DEBUG("Erro no upload detectado.");
        return false;
    }
    DEBUG("Upload finalizado com sucesso.");
    return true;
}


// ================= Gravação no Realtime Database =================

inline bool writeToFirebaseRTDB(const String &databaseURL, const String &path, const String &idToken, const JsonDocument &json) {
  
    if (idToken.isEmpty()) {
    DEBUG("Token de autenticação vazio. Abortando escrita no RTDB.");
    return false;
  }
  
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  String url = databaseURL + "/" + path + ".json?auth=" + idToken;

  //  DEBUGF("URL RTDB: %s\n", url.c_str());

  HTTPClient https;
  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000);

  String requestBody;
  serializeJson(json, requestBody);

  int httpResponseCode = https.PATCH(requestBody);

  if (httpResponseCode > 0) {
    String response = https.getString();
    DEBUG("Resposta RTDB:");
    DEBUG(response);
    https.end();
    return true;
  } else {
    DEBUGF("Erro RTDB: Código %d - %s\n", httpResponseCode, https.getString().c_str());
    https.end();
    return false;
  }
}

inline int obterIntervaloEnvio(const String& databaseURL, const String& clienteId, const String& dispositivoUID, const String& idToken, int valorPadrao) {
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);

  String path = "clientes/" + clienteId + "/dispositivos/" + dispositivoUID + "/config/intervaloEnvioSegundos";
  String url = databaseURL + "/" + path + ".json?auth=" + idToken;

  HTTPClient https;
  https.begin(client, url);
  int httpCode = https.GET();

  int intervalo = valorPadrao;
  if (httpCode == 200) {
    String payload = https.getString();
    intervalo = payload.toInt();
    if (intervalo <= 0) {
      DEBUG("Intervalo inválido recebido, usando valor padrão.");
      intervalo = valorPadrao; 
    }
    DEBUGF("Intervalo obtido: %d segundos\n", intervalo);
  } else {
    DEBUGF("Erro ao obter intervalo: código %d\n", httpCode);
  }

  https.end();
  return intervalo;
}

#endif

