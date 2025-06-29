
#include <WiFi.h>
#include <Arduino.h>
#include <ESP32Time.h>
#include <esp_camera.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>

#include <DHT.h>

#include <config.h>
#include <pins.h>
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
DHT dht(DHT_PIN, DHTTYPE);

// ================== Reset Físico ==================

void checarResetFisico() {
  DEBUG("Checando reset físico...");
  pinMode(RESET_PIN, INPUT_PULLUP);
  vTaskDelay(pdMS_TO_TICKS(1000));
  if (digitalRead(RESET_PIN) == LOW) {
    unsigned long tempo = millis();
    while (digitalRead(RESET_PIN) == LOW) {
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
  config.frame_size = FRAMESIZE_SXGA;
  config.jpeg_quality = 5;
  config.fb_count = 1;

  if (!psramFound()) {
    DEBUG("PSRAM não detectada!");
  }

  esp_err_t err = esp_camera_init(&config);
  sensor_t *s = esp_camera_sensor_get();
  if(s){
    s->set_contrast(s, 1);                  // -2 a 2
    s->set_brightness(s, 1);                // -2 a 2
    s->set_saturation(s, 0);                // -2 a 2
    s->set_whitebal(s, 1);                  // 1 = ligado
    s->set_awb_gain(s, 1);                  // 1 = ligado
    s->set_wb_mode(s, 0);                   // 0 = automático, 1 = nublado, 2 = fluorescente, 3 = incandescente
    s->set_gain_ctrl(s, 1);                 // Controle de ganho automático
    s->set_exposure_ctrl(s, 1);            // Controle de exposição automático
    s->set_gainceiling(s, GAINCEILING_32X); // Limite de ganho
    s->set_colorbar(s, 0);                  // 0 = sem barras de cor (desliga modo teste)
    //DEBUG:
    // DEBUG("=== CONFIGURAÇÕES ATUAIS DA CÂMERA ===");
    // DEBUGF("Brilho (brightness): %d\n", s->status.brightness);
    // DEBUGF("Contraste (contrast): %d\n", s->status.contrast);
    // DEBUGF("Saturação (saturation): %d\n", s->status.saturation);
    // DEBUGF("Efeito especial (special_effect): %d\n", s->status.special_effect);
    // DEBUGF("Auto White Balance (awb): %d\n", s->status.awb);
    // DEBUGF("Modo WB (wb_mode): %d\n", s->status.wb_mode);
    // DEBUGF("Auto Exposure Control 2 (aec2): %d\n", s->status.aec2);
    // DEBUGF("Ganho automático (agc): %d\n", s->status.agc);
    // DEBUGF("Colorbar ativado: %d\n", s->status.colorbar);
    // DEBUGF("Espelho horizontal (hmirror): %d\n", s->status.hmirror);
    // DEBUGF("Espelho vertical (vflip): %d\n", s->status.vflip);
    // DEBUGF("Denoise: %d\n", s->status.denoise);
    // DEBUGF("Correção de pixel ruim (bpc): %d\n", s->status.bpc);
    // DEBUGF("Correção de pixel branco (wpc): %d\n", s->status.wpc);
  }
  // Aguarda a câmera estabilizar

  for (int i = 0; i < 5; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

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
    initCamera();
    dht.begin();
    int intervaloEnvio = obterIntervaloEnvio(DATABASE_URL, clienteId, dispositivoUID, idToken, 60);
    DEBUG("Sensor DHT11 iniciado (se conectado).");

    for (;;) {
      if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (millis() - lastTokenUpdate > 3300000) {  // 55 minutos
          if (refreshIdToken(API_KEY, refreshToken, idToken, localId)) {
            DEBUG("Token renovado com sucesso!");
            lastTokenUpdate = millis(); 
          } else {
            DEBUG("Falha ao renovar o token.");
            xSemaphoreGive(xSemaphore);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
          }
        }
        struct tm timeinfo = rtc.getTimeStruct();
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);

        DEBUG("Capturando imagem...");

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          DEBUG("Erro ao capturar imagem");
          xSemaphoreGive(xSemaphore);
          vTaskDelay(pdMS_TO_TICKS(10000));
          continue;
        }

        DEBUGF("Imagem capturada. Tamanho: %d bytes\n", fb->len);

        String remotePath = String("clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/fotos/" + timestamp + ".jpg";
        DEBUGF("remotePath: %s\n", remotePath.c_str());
        DEBUG("Iniciando upload...");
        bool uploadSuccess = uploadToFirebaseStorage(STORAGE_BUCKET, remotePath, fb, idToken);

        if (uploadSuccess) {
          String dbPath = String("clientes/") + clienteId + "/dispositivos/" + dispositivoUID + "/dados/" + timestamp;
          
          StaticJsonDocument<1024> json;
          json["timestamp"] = String(timestamp).c_str();
          json["path"] = remotePath.c_str();

          float temperatura = dht.readTemperature();
          float umidade = dht.readHumidity();

          if (!isnan(temperatura) && temperatura != 0) {
            json["temperatura"] = temperatura;
          } else {
            //DEBUG("Sensor DHT11 ausente ou leitura de temperatura inválida.");
          }

          if (!isnan(umidade)&& umidade != 0) {
            json["umidade"] = umidade;
          } else {
            //DEBUG("Sensor DHT11 ausente ou leitura de umidade inválida.");
          }

          writeToFirebaseRTDB(DATABASE_URL, dbPath, idToken, json);

        } else {
          DEBUG("Falha no upload.");
        }

        esp_camera_fb_return(fb);

        DEBUGF("Aguardando próxima leitura em %d segundos...\n", intervaloEnvio);
        xSemaphoreGive(xSemaphore);
        vTaskDelay(pdMS_TO_TICKS(intervaloEnvio* 1000));
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
      xTaskCreate(initWiFi, "initWiFi", 4096, NULL, 3, NULL);
      if (clienteId == "" || clienteId == "null" || dispositivoUID == "" || dispositivoUID == "null") {
        DEBUG("Dados de provisionamento não encontrados. Executando provisionamento online...");
        xTaskCreate(taskProvisionarDispositivo, "taskProvisionarDispositivo", 8096, NULL, 3, NULL);
      }else{
        xTaskCreate(conectarFirebase, "conectarFirebase", 8096, NULL, 5, NULL);
        xTaskCreatePinnedToCore(enviarDadosFirebase, "enviarDadosFirebase", 30400, NULL, 4, NULL,1);
      }
    }
  }
void loop() {
}
