#include <WiFi.h>
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Time.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "config.h"  // WIFI_SSID, WIFI_PASSWORD, API_KEY, DATABASE_URL
#include "camera_pins.h" // Camera pins for ESP32-CAM OR ESP32-S3-CAM
#include <esp_camera.h>
#include <LittleFS.h> 

#define DEBUG_MODE true  // Altere para false para desativar debug
#define DEBUG(x) do { if (DEBUG_MODE) Serial.println(x); } while (0)
#define DEBUGF(...) do { if (DEBUG_MODE) Serial.printf(__VA_ARGS__); } while (0)

ESP32Time rtc(0);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String userUID = "";
bool signupOK = false;

// Eventos e filas
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;

// Bits de evento
#define EV_WIFI (1 << 1)
#define EV_FIRE (1 << 2)

QueueHandle_t queueContador = xQueueCreate(8, sizeof(int));

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
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  #if CONFIG_IDF_TARGET_ESP32
  #define TARGET_ESP32CAM
  #elif CONFIG_IDF_TARGET_ESP32S3
    #define TARGET_ESP32S3
  #endif

  if (!psramFound()) {
  DEBUG("PSRAM não detectada!");
  } else {
    DEBUG("PSRAM detectada.");
  }
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUGF("Erro ao iniciar a câmera: 0x%X\n", err);
  } else {
    DEBUG("Câmera iniciada com sucesso");
  }
}

void initWiFi(void *pvParameters) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  DEBUG("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    DEBUG(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  DEBUG("\nConectado com IP: " + WiFi.localIP().toString());

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo);
  }

  xEventGroupSetBits(xEventGroupKey, EV_WIFI);
  vTaskDelete(NULL);
}

void conectarFirebase(void *pvParameters) {
  EventBits_t bits = xEventGroupWaitBits(xEventGroupKey, EV_WIFI, pdFALSE, pdTRUE, portMAX_DELAY);
  if (bits & EV_WIFI) {
    DEBUG("Conectando ao Firebase...");

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    Firebase.signUp(&config, &auth, "", "");
    Firebase.begin(&config, &auth);
    LittleFS.begin();   
    Firebase.reconnectWiFi(true);

    unsigned long timeout = millis();
    while (auth.token.uid == "" && millis() - timeout < 10000) {
      DEBUG(".");
      delay(200);
    }

    if (auth.token.uid != "") {
      userUID = auth.token.uid.c_str();
      signupOK = true;
      DEBUG("\nConectado ao Firebase como UID: " + userUID);
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
    } else {
      DEBUG("\nFalha ao obter UID.");
    }
  }
  vTaskDelete(NULL);
}

// The Firebase Storage upload callback function
void fcsUploadCallback(FCS_UploadStatusInfo info)
{
    if (info.status == firebase_fcs_upload_status_init)
    {
        Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize, info.remoteFileName.c_str());
    }
    else if (info.status == firebase_fcs_upload_status_upload)
    {
        Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
    }
    else if (info.status == firebase_fcs_upload_status_complete)
    {
        Serial.println("Upload completed\n");
        FileMetaInfo meta = fbdo.metaData();
        Serial.printf("Name: %s\n", meta.name.c_str());
        Serial.printf("Bucket: %s\n", meta.bucket.c_str());
        Serial.printf("contentType: %s\n", meta.contentType.c_str());
        Serial.printf("Size: %d\n", meta.size);
        Serial.printf("Generation: %lu\n", meta.generation);
        Serial.printf("Metageneration: %lu\n", meta.metageneration);
        Serial.printf("ETag: %s\n", meta.etag.c_str());
        Serial.printf("CRC32: %s\n", meta.crc32.c_str());
        Serial.printf("Tokens: %s\n", meta.downloadTokens.c_str());
        Serial.printf("Download URL: %s\n\n", fbdo.downloadURL().c_str());
    }
    else if (info.status == firebase_fcs_upload_status_error)
    {
        Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
    }
}

void enviarDadosFirebaseComFoto(void *pvParameters) {
  int contador = 0;
  for (;;) {
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      contador++;
      struct tm timeinfo = rtc.getTimeStruct();
      char timestamp[30];
      strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) { DEBUG("Falha na captura"); goto wait; }

      // 1) salva buffer em arquivo temporário
      const char *localPath = "/cap.jpg";
      File f = LittleFS.open(localPath, FILE_WRITE);
      f.write(fb->buf, fb->len);
      f.close();

      // 2) faz upload ao Storage
      String remotePath =
        "/users/" + userUID + "/camera/images/" + String(timestamp) + ".jpg";
      if (Firebase.Storage.upload(&fbdo,
                                  STORAGE_BUCKET_ID,    // bucket
                                  localPath,            // caminho local
                                  mem_storage_type_flash,
                                  remotePath.c_str(),   // caminho no Storage
                                  "image/jpeg",         // mime
                                  fcsUploadCallback     // callback opcional
                                 )) {
        DEBUGF("✅ [%d] Upload Storage: %s\n", contador, remotePath.c_str());
        String url = fbdo.downloadURL();  // pega URL pública
        // 3) envia metadados ao RTDB
        String dbPath = "/users/" + userUID + "/camera/cameraData/" + String(timestamp);
        FirebaseJson json;
        json.set("timestamp", timestamp);
        json.set("url", url);
        json.set("contador", contador);
        // …
        Firebase.RTDB.setJSON(&fbdo, dbPath, &json);
      } else {
        DEBUG("❌ Erro Storage: " + fbdo.errorReason());
      }

      // 4) cleanup
      LittleFS.remove(localPath);
      esp_camera_fb_return(fb);
    }
wait:
    xSemaphoreGive(xSemaphore);
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  initCamera();

  xEventGroupKey = xEventGroupCreate();
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore) xSemaphoreGive(xSemaphore);

  xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL);
  xTaskCreate(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL);
  xTaskCreate(enviarDadosFirebaseComFoto, "enviarDadosFirebaseSimulado", 8096, NULL, 1, NULL);
}

void loop() {
  // vazio
}
