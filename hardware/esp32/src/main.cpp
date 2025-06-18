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

#include "camera_pins.h"

// Debug
#define DEBUG_MODE true
#define DEBUG(x) do { if (DEBUG_MODE) Serial.println(x); } while (0)
#define DEBUGF(...) do { if (DEBUG_MODE) Serial.printf(__VA_ARGS__); } while (0)

//Reset
#define PIN_RESET 12

// Firebase e Wi-Fi
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
ESP32Time rtc(0);

String WIFI_SSID, WIFI_PASSWORD, API_KEY, DATABASE_URL, STORAGE_BUCKET;
String clienteId, dispositivoUID, sn, mac;

bool signupOK = false;

// Eventos e filas
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;

#define EV_WIFI (1 << 1)
#define EV_FIRE (1 << 2)

QueueHandle_t queueContador = xQueueCreate(8, sizeof(int));

// ================== Reset Físico ==================

void checarResetFisico() {
  pinMode(PIN_RESET, INPUT_PULLUP);
  if (digitalRead(PIN_RESET) == LOW) {
    unsigned long tempo = millis();
    while (digitalRead(PIN_RESET) == LOW) {
      if (millis() - tempo > 5000) {  // 5 segundos segurando
        DEBUG("🔧 Reset físico iniciado...");
        LittleFS.begin();
        LittleFS.remove("/config.json");
        DEBUG("Config apagado. Reiniciando...");
        delay(1000);
        ESP.restart();
      }
    }
  }
}

// ================== Reset Remoto ==================
void checarResetRemoto() {
  for (;;) {
    if (signupOK) {
      MB_String path = MB_String("/clientes/") + clienteId.c_str() + "/dispositivos/" + dispositivoUID.c_str() + "/comandos/reset";
      bool reset = false;

      if (Firebase.RTDB.getBool(&fbdo, path)) {
        reset = fbdo.boolData();
        if (reset) {
          DEBUG("🔧 Reset remoto recebido...");

          // Apaga o comando no RTDB
          Firebase.RTDB.setBool(&fbdo, path, false);

          // Apaga config
          LittleFS.begin();
          LittleFS.remove("/config.json");

          DEBUG("Config apagado. Reiniciando...");
          delay(1000);
          ESP.restart();
        }
      } else {
        DEBUGF("Erro lendo comando reset: %s\n", fbdo.errorReason().c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));  // Checa a cada 5 segundos
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
  API_KEY = doc["apiKey"].as<String>();
  DATABASE_URL = doc["databaseURL"].as<String>();
  STORAGE_BUCKET = doc["storageBucket"].as<String>();
  clienteId = doc["clienteId"].as<String>();
  dispositivoUID = doc["dispositivoUID"].as<String>();
  sn = doc["sn"].as<String>();
  mac = doc["mac"].as<String>();

  DEBUGF("Config carregado: clienteId=%s, dispositivoUID=%s\n", clienteId.c_str(), dispositivoUID.c_str());
}


//================== Portal de cadastro do dispositivo para config. inicial ==================

WebServer server(80);
DNSServer dnsServer;

void startConfigPortal() {
  if (!LittleFS.begin()) {
    Serial.println("❌ Falha ao montar LittleFS.");
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP("AgroScan-Setup");

  IPAddress IP = WiFi.softAPIP();
  Serial.printf("🌐 AP IP address: %s\n", IP.toString().c_str());

  dnsServer.start(53, "*", IP);

  server.on("/", HTTP_GET, []() {
    if (LittleFS.exists("/index.html")) {
      File file = LittleFS.open("/index.html", "r");
      server.streamFile(file, "text/html");
      file.close();
      Serial.println("✅ Página index.html servida.");
    } else {
      server.send(500, "text/plain", "❌ index.html não encontrado.");
      Serial.println("❌ index.html não encontrado.");
    }
  });

  server.on("/save", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "text/plain", "❌ Bad Request - Dados não recebidos.");
      Serial.println("❌ Dados não recebidos.");
      return;
    }

    Serial.println("👉 Dados recebidos:");
    Serial.println(server.arg("plain"));

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, server.arg("plain"));
    if (error) {
      server.send(400, "text/plain", "❌ JSON Parse Failed.");
      Serial.print("❌ Erro JSON: ");
      Serial.println(error.f_str());
      return;
    }

    File file = LittleFS.open("/config.json", "w");
    if (!file) {
      server.send(500, "text/plain", "❌ Failed to open config.json.");
      Serial.println("❌ Falha ao abrir config.json.");
      return;
    }

    serializeJson(doc, file);
    file.close();

    server.send(200, "text/plain", "✅ Configuração salva.");
    Serial.println("✅ Configuração salva. Reiniciando...");
    delay(1000);
    ESP.restart();
  });

  server.begin();
  Serial.println("🚀 Servidor iniciado em http://192.168.4.1");
}


void taskWebServer(void *pvParameters) {
  while (true) {
    dnsServer.processNextRequest();
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
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
  config.frame_size = FRAMESIZE_SVGA;
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
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  DEBUG("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    DEBUG(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  DEBUGF("\nConectado. IP: %s\n", WiFi.localIP().toString().c_str());

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo);
  }

  xEventGroupSetBits(xEventGroupKey, EV_WIFI);
  vTaskDelete(NULL);
}

// ================== Firebase ==================

void conectarFirebase(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_WIFI) {
    Serial.println("Conectando ao Firebase...");

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    Firebase.signUp(&config, &auth, "", "");
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    unsigned long start = millis();
    const unsigned long TIMEOUT = 15000; // 🔥 15 segundos máximo

    while (auth.token.uid == "" && millis() - start < TIMEOUT) {
      Serial.print(".");
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (auth.token.uid != "") {
      signupOK = true;
      Serial.printf("\n✅ Conectado no Firebase. UID: %s\n", auth.token.uid.c_str());
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
    } else {
      Serial.println("\n❌ Falha ao conectar no Firebase. Verifique API Key, Database URL e rede.");
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
  int contador = 0;

  for (;;) {
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      contador++;

      struct tm timeinfo = rtc.getTimeStruct();
      char timestamp[30];
      strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);

      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) {
        DEBUG("❌ Falha na captura");
        xSemaphoreGive(xSemaphore);
        vTaskDelay(pdMS_TO_TICKS(10000));
        continue;
      }

      const char *localPath = "/cap.jpg";
      File f = LittleFS.open(localPath, FILE_WRITE);
      f.write(fb->buf, fb->len);
      f.close();

      String remotePath = String("/clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/fotos/" + timestamp + ".jpg";

      if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET.c_str(), localPath, mem_storage_type_flash, remotePath.c_str(), "image/jpeg", fcsUploadCallback)) {
        String url = fbdo.downloadURL();

        String dbPath = String("/clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/dados/" + timestamp;
        FirebaseJson json;
        json.set("timestamp", timestamp);
        json.set("url", url);
        json.set("contador", contador);
        json.set("sn", sn);
        json.set("mac", mac);

        Firebase.RTDB.setJSON(&fbdo, dbPath.c_str(), &json);
      } else {
        DEBUGF("❌ Erro upload: %s\n", fbdo.errorReason().c_str());
      }

      LittleFS.remove(localPath);
      esp_camera_fb_return(fb);

      xSemaphoreGive(xSemaphore);
      vTaskDelay(pdMS_TO_TICKS(10000));
    }
  }
}

// ================== Setup e Loop ==================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Iniciando...");

  // checarResetFisico();
  // checarResetRemoto();

   if (!LittleFS.begin()) {
    Serial.println("❌ Falha ao montar LittleFS.");
    return;
  }


  if (!LittleFS.exists("/config.json")) {
    DEBUG("🔧 Configuração não encontrada. Iniciando Portal Wi-Fi...");
    startConfigPortal();
    xTaskCreate(taskWebServer, "taskWebServer", 4096, NULL, 1, NULL);
    } else {//inicializa normalmente
      DEBUG("🔧 Configuração encontrada. Iniciando sistema...");
      carregarConfig();
      initCamera();
      xEventGroupKey = xEventGroupCreate();
      xSemaphore = xSemaphoreCreateBinary();
      if (xSemaphore) xSemaphoreGive(xSemaphore);
        xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL);
        xTaskCreatePinnedToCore(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL, 1);
        // xTaskCreatePinnedToCore(enviarDadosFirebase, "enviarDadosFirebase", 8096, NULL, 1, NULL, 1);
      }

  
}

void loop() {
  // vazio
}
