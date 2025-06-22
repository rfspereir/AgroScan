
#include <WiFi.h>
#include <Arduino.h>
#include <ESP32Time.h>
#include <esp_camera.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <config.h>
#include <camera_pins.h>
#include <firebase.h>
#include <ca_bundle.h>

unsigned long lastTokenUpdate = 0;

ESP32Time rtc(0);
bool signupOK = false;

String WIFI_SSID, WIFI_PASSWORD, clienteId, dispositivoUID, email, senha, sn, mac;
String idToken, refreshToken, localId;

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
    file.close();
    return;
  }

  WIFI_SSID = doc["wifiSSID"].as<String>();
  WIFI_PASSWORD = doc["wifiPassword"].as<String>();
  clienteId = doc["clienteId"].as<String>();
  dispositivoUID = doc["dispositivoUID"].as<String>();
  email = doc["email"].as<String>();
  senha = doc["senha"].as<String>();
  sn = doc["sn"].as<String>();
  mac = doc["mac"].as<String>();

  DEBUGF("Config carregado: clienteId=%s, dispositivoUID=%s\n", clienteId.c_str(), dispositivoUID.c_str());
  file.close();
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
  DEBUGF("AP IP address: %s\n", IP.toString());

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
  DEBUGF("Servidor iniciado em: %s\n", IP.toString());
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

void taskProvisionarDispositivo(void *pvParameters) {

  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_WIFI) {
    
    mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.trim();

    char snBuf[20];
    uint64_t chipid = ESP.getEfuseMac();
    sprintf(snBuf, "%04X%08X", (uint16_t)(chipid >> 32), (uint32_t)chipid);
    sn = String(snBuf);
    sn.toUpperCase();
    sn.trim();

    DEBUGF("SN: %s\n", sn);
    
    DEBUG("Iniciando provisionamento do dispositivo...");
    bool result = provisionarDispositivo(WIFI_SSID, WIFI_PASSWORD, clienteId, dispositivoUID, email, senha, sn, mac);
    
    if (result) {
      DEBUG("Provisionamento bem-sucedido!");
      vTaskDelay(pdTICKS_TO_MS(2000));
      ESP.restart();
    } else {
      DEBUG("Falha no provisionamento.");
      vTaskDelay(pdTICKS_TO_MS(2000));
      ESP.restart();

    }
  }
    vTaskDelete(NULL);
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
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
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

    if (loginFirebase(email, senha, idToken, refreshToken, localId)) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      DEBUG("Login bem-sucedido!");
      // DEBUGF("ID Token: %s\n", idToken.c_str());
      // DEBUGF("Refresh Token: %s\n", refreshToken.c_str());
      // DEBUGF("UID (localId): %s\n", localId.c_str());
      signupOK = true;
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
    } else {
        DEBUG("Falha no login.");
    }
  }
  vTaskDelete(NULL);
}

// ================== Loop de envio ==================

void enviarDadosFirebase(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_FIRE, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_FIRE) {
    int contador = 0;
    initCamera();
    for (;;) {
      if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (millis() - lastTokenUpdate > 3300000) {  // 55 minutos
          if (refreshIdToken(API_KEY, refreshToken, idToken, localId)) {
            Serial.println("Token renovado com sucesso!");
            lastTokenUpdate = millis(); 
          } else {
            Serial.println("Falha ao renovar o token.");
            xSemaphoreGive(xSemaphore);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
          }
        }
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

        String remotePath = String("clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/fotos/" + timestamp + ".jpg";
        DEBUGF("remotePath: %s\n", remotePath.c_str());

        bool uploadSuccess = uploadToFirebaseStorage(STORAGE_BUCKET, remotePath, localPath, idToken);
        
        if (uploadSuccess) {
          String dbPath = String("clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/dados/" + timestamp;
          
          StaticJsonDocument<1024> json;
          json["timestamp"] = String(timestamp).c_str();
          json["path"] = remotePath.c_str();
          json["contador"] = String(contador).c_str();

          writeToFirebaseRTDB(DATABASE_URL, dbPath, idToken, json);
        } else {
          DEBUG("Falha no upload.");
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
        xTaskCreate(taskProvisionarDispositivo, "taskProvisionarDispositivo", 8096, NULL, 14, NULL);
      }else{
        xTaskCreate(conectarFirebase, "conectarFirebase", 8096, NULL, 14, NULL);
        xTaskCreatePinnedToCore(enviarDadosFirebase, "enviarDadosFirebase", 30400, NULL, 1, NULL,1);
      }
    }
  }
void loop() {
}
