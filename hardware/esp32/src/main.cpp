#include <WiFi.h>
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Time.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <esp_camera.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <config.h>
#include <camera_pins.h>
#include <certificado.h>

// Debug
#define DEBUG_MODE true
#define DEBUG(x) do { if (DEBUG_MODE) Serial.println(x); } while (0)
#define DEBUGF(...) do { if (DEBUG_MODE) Serial.printf(__VA_ARGS__); } while (0)
#define DEBUGL(x) do { if (DEBUG_MODE) Serial.print(x); } while (0)

//Reset
#define PIN_RESET 12

ESP32Time rtc(0);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;

String WIFI_SSID, WIFI_PASSWORD;
// String clienteId, dispositivoUID, email, senha, sn, mac;
String clienteId, dispositivoUID, customToken, sn, mac;

// Eventos e filas
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;

#define EV_WIFI (1 << 1)
#define EV_FIRE (1 << 2)

QueueHandle_t queueContador = xQueueCreate(8, sizeof(int));

// ================== Reset Físico ==================

void checarResetFisico() {
  DEBUG("Checando reset físico...");
  pinMode(PIN_RESET, INPUT_PULLUP);
  vTaskDelay(pdMS_TO_TICKS(1000));
  if (digitalRead(PIN_RESET) == LOW) {
    unsigned long tempo = millis();
    while (digitalRead(PIN_RESET) == LOW) {
      if (millis() - tempo > 5000) {  // 5 segundos segurando
        DEBUG("Reset físico iniciado...");
        LittleFS.begin();
        LittleFS.remove("/config.json");
        DEBUG("Config apagado. Reiniciando...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
      }
    }
  }
}
// ================== Leitura de configuração ==================

void carregarConfig() {
  if (!LittleFS.begin()) {
    DEBUG("Erro ao montar LittleFS");
    return;
  }

  File file = LittleFS.open("/config.json", "r");
  if (!file) {
    DEBUG("Arquivo config.json não encontrado");
    return;
  }

  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);

  DynamicJsonDocument doc(1024);
  auto error = deserializeJson(doc, buf.get());
  if (error) {
    DEBUG("Erro ao fazer parse do config.json");
    return;
  }

  WIFI_SSID = doc["wifiSSID"].as<String>();
  WIFI_PASSWORD = doc["wifiPassword"].as<String>();
  clienteId = doc["clienteId"].as<String>();
  dispositivoUID = doc["dispositivoUID"].as<String>();
  // email = doc["email"].as<String>();
  // senha = doc["senha"].as<String>();
  customToken = doc["customToken"].as<String>();
  sn = doc["sn"].as<String>();
  mac = doc["mac"].as<String>();

  DEBUGF("Config carregado: clienteId=%s, dispositivoUID=%s\n", clienteId.c_str(), dispositivoUID.c_str());
}

//================== Portal de config. inicial ==================

WebServer server(80);
DNSServer dnsServer;

void startConfigPortal() {
  if (!LittleFS.begin()) {
    DEBUG("Falha ao montar LittleFS.");
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP("AgroScan-Setup");

  IPAddress IP = WiFi.softAPIP();
  DEBUGF("AP IP address: %s\n", IP.toString().c_str());

  dnsServer.start(53, "*", IP);

  server.on("/", HTTP_GET, []() {
    if (LittleFS.exists("/index.html")) {
      File file = LittleFS.open("/index.html", "r");
      server.streamFile(file, "text/html");
      file.close();
      DEBUG("Página index.html servida.");
    } else {
      server.send(500, "text/plain", "index.html não encontrado.");
      DEBUG("index.html não encontrado.");
    }
  });

  server.on("/save", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "Bad Request - Dados não recebidos.");
      DEBUG("Dados não recebidos.");
      return;
    }

    DEBUG("Dados recebidos:");
    DEBUG(server.arg("plain"));

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "text/plain", "JSON Parse Failed.");
      DEBUG("Erro JSON: ");
      DEBUG(error.f_str());
      return;
    }

    File file = LittleFS.open("/config.json", "w");
    if (!file) {
      server.send(500, "text/plain", "Failed to open config.json.");
      DEBUG("Falha ao abrir config.json.");
      return;
    }

    serializeJson(doc, file);
    file.close();

    server.send(200, "text/plain", "Configuração salva.");
    DEBUG("Configuração salva. Reiniciando...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP.restart();
  });

  server.begin();
  DEBUG("Servidor iniciado em http://192.168.4.1");
}

void taskWebServer(void *pvParameters) {
  startConfigPortal();
  vTaskDelay(pdMS_TO_TICKS(1000));
  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

//=================== Provisionamento online============
void provisionarDispositivo(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_WIFI) {
    WiFiClientSecure client;
    client.setCACert(root_ca);

    HTTPClient https;
    String url = CREATE_DEVICE_URL;
    https.begin(client, url);
    https.addHeader("Content-Type", "application/json");

    //Captura MAC e SN automaticamente
    String mac = WiFi.macAddress();
    uint64_t chipid = ESP.getEfuseMac();
    String sn = String((uint16_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
    sn.toUpperCase();
    sn.trim();
    mac.replace(":", "");
    mac.trim();

    DEBUGF("SN: %s\n", sn.c_str());

    // Monta o JSON
    StaticJsonDocument<512> json;
    json["sn"] = sn;
    json["mac"] = mac;

    String requestBody;
    serializeJson(json, requestBody);

    DEBUG("Enviando dados para backend...");
    int httpCode = https.POST(requestBody);

    if (httpCode > 0) {
      String payload = https.getString();
      DEBUG("Resposta do backend:");
      DEBUG(payload);

      if (httpCode == 200) {
        StaticJsonDocument<512> response;
        DeserializationError error = deserializeJson(response, payload);

        if (error) {
          DEBUG("Erro ao parsear resposta JSON");
          https.end();
          vTaskDelay(pdTICKS_TO_MS(2000));
          ESP.restart();
          vTaskDelete(NULL);
        }

        String dispositivoUID = response["uid"];
        String clienteId = response["clienteId"];
        String email = response["email"];
        String customToken = response["customToken"];

        // Salva no config.json
        StaticJsonDocument<1024> config;
        config["wifiSSID"] = WIFI_SSID;
        config["wifiPassword"] = WIFI_PASSWORD;
        config["clienteId"] = clienteId;
        config["dispositivoUID"] = dispositivoUID;
        // config["email"] = email;
        // config["senha"] = senha;
        config["customToken"] = customToken;
        config["sn"] = sn;
        config["mac"] = mac;

        File file = LittleFS.open("/config.json", "w");
        if (!file) {
          DEBUG("Erro ao abrir config.json para escrita.");
          https.end();
          vTaskDelay(pdTICKS_TO_MS(2000));
          ESP.restart();
          vTaskDelete(NULL);
        }
        serializeJson(config, file);
        file.close();

        DEBUG("Provisionamento concluído com sucesso.");
        https.end();
        vTaskDelay(pdTICKS_TO_MS(2000));
        ESP.restart();
        vTaskDelete(NULL);
      }
    } else {
      DEBUGF("Erro na requisição HTTPS: %s\n", https.errorToString(httpCode).c_str());
    }

    https.end();
    vTaskDelay(pdTICKS_TO_MS(2000));
    ESP.restart();
    vTaskDelete(NULL);
    return;
  }
}


// ================== Câmera ==================

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d7 = CAM_PIN_D7;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sscb_sda = CAM_PIN_SIOD;
  config.pin_sscb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QXGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (!psramFound()) {
    DEBUG("PSRAM não detectada!");
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUGF("Erro na inicialização da câmera: 0x%X\n", err);
  } else {
    DEBUG("Câmera inicializada.");
  }
}

// ================== Wi-Fi ==================

void initWiFi(void *pvParameters) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  DEBUGL("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    DEBUGL(".");
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  DEBUGF("\nConectado com IP: %s\n", WiFi.localIP().toString().c_str());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo);
  }
  DEBUGF("Hora atual: %s\n", rtc.getTime("%Y-%m-%d %H:%M:%S").c_str());
  xEventGroupSetBits(xEventGroupKey, EV_WIFI);
  vTaskDelete(NULL);
}
// ================== Firebase ==================

void conectarFirebase(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_WIFI) {
    DEBUG("Conectando ao Firebase...");

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
       
    Firebase.begin(&config, &auth);
    Firebase.setCustomToken(&config, customToken);   
    Firebase.reconnectWiFi(true);
    unsigned long timeout = millis();
    DEBUGL("Aguardando autenticação no Firebase...");
    while (auth.token.uid == "" && millis() - timeout < 10000) {
      DEBUGL(".");
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (auth.token.uid != "") {
      signupOK = true;
      DEBUGF("\nConectado no Firebase com o UID: %s\n",auth.token.uid.c_str());
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
    } else {
      DEBUG("\nFalha ao conectar no Firebase. Verifique API Key, Database URL e rede.");
    }
  }
  vTaskDelete(NULL);
}

// ================== Upload Callback ==================

void fcsUploadCallback(FCS_UploadStatusInfo info) {
  if (info.status == firebase_fcs_upload_status_upload) {
    DEBUGF("Upload: %d%%, Tempo: %dms\n", (int)info.progress, info.elapsedTime);
  }
  if (info.status == firebase_fcs_upload_status_complete) {
    DEBUG("Upload completo.");
    DEBUGF("URL: %s\n", fbdo.downloadURL().c_str());
  }
  if (info.status == firebase_fcs_upload_status_error) {
    DEBUGF("Erro upload: %s\n", info.errorMsg.c_str());
  }
}

// ================== Loop de envio ==================

void enviarDadosFirebase(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_FIRE, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_FIRE) {
    int contador = 0;
    initCamera();
    for (;;) {
      if (Firebase.ready() && signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
        contador++;
        DEBUGF("Contador: %d\n", contador);

        struct tm timeinfo = rtc.getTimeStruct();
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          DEBUG(" Falha na captura");
          xSemaphoreGive(xSemaphore);
          vTaskDelay(pdMS_TO_TICKS(10000));
          continue;
        }

        const char *localPath = "/cap.jpg";
        File f = LittleFS.open(localPath, FILE_WRITE);
        f.write(fb->buf, fb->len);
        f.close();

        MB_String remotePath = MB_String("/clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/fotos/" + timestamp + ".jpg";
        DEBUGF("remotePath: %s\n", remotePath.c_str());
        if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET, localPath, mem_storage_type_flash, remotePath.c_str(), "image/jpeg", fcsUploadCallback)) {
          MB_String url = fbdo.downloadURL();
          DEBUGF("url: %s\n", url.c_str());
          MB_String dbPath = MB_String("/clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/dados/" + timestamp;
          DEBUGF("dbPath: %s\n", url.c_str());
          FirebaseJson json;
          json.set("timestamp", timestamp);
          json.set("url", url);
          json.set("contador", contador);
          json.set("sn", sn);
          json.set("mac", mac);

          Firebase.RTDB.setJSON(&fbdo, dbPath.c_str(), &json);
        } else {
          DEBUGF("Erro upload: %s\n", fbdo.errorReason().c_str());
        }

        LittleFS.remove(localPath);
        esp_camera_fb_return(fb);

        xSemaphoreGive(xSemaphore);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
  }
}

// ================== Setup e Loop ==================

void setup() {
  Serial.begin(115200);
  delay(1000);
  DEBUG("Iniciando...");
  checarResetFisico();
  if (!LittleFS.begin()) {
    DEBUG(" Falha ao montar LittleFS.");
    return;
  }
  if (!LittleFS.exists("/config.json")) {
    DEBUG("Configuração não encontrada. Iniciando Portal Wi-Fi...");
    xTaskCreate(taskWebServer, "taskWebServer", 8096, NULL, 1, NULL);
    } else {
      DEBUG("Configuração encontrada. Iniciando sistema...");
      carregarConfig();
      xEventGroupKey = xEventGroupCreate();
      xSemaphore = xSemaphoreCreateBinary();
      if (xSemaphore) xSemaphoreGive(xSemaphore);
      xTaskCreate(initWiFi, "initWiFi", 4096, NULL, 14, NULL);
      if (clienteId == "" || clienteId == "null" || dispositivoUID == "" || dispositivoUID == "null") {
        DEBUG("Dados de provisionamento não encontrados. Executando provisionamento online...");
        xTaskCreate(provisionarDispositivo, "provisionarDispositivo", 8096, NULL, 14, NULL);
      }else{
        xTaskCreate(conectarFirebase, "conectarFirebase", 8096, NULL, 14, NULL);
        xTaskCreate(enviarDadosFirebase, "enviarDadosFirebase", 30400, NULL, 1, NULL);
      }
    }
  }
void loop() {
}
