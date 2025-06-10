#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include "time.h"
#include <ESP32Time.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <SPIFFS.h>
#include "config.h" // Certifique-se de que o arquivo contenha WIFI_SSID, WIFI_PASSWORD, API_KEY, DATABASE_URL, STORAGE_BUCKET_ID

// Definições da câmera
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22
#define FLASH_PIN 4

ESP32Time rtc(0);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String userUID = "";
bool signupOK = false;

// Eventos e filas
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;
QueueHandle_t queueCapturarFoto = xQueueCreate(1, sizeof(int));

// Bits de evento
#define EV_START (1 << 0)
#define EV_WIFI (1 << 1)
#define EV_FIRE (1 << 2)

void InicializaEsp(void *pvParameters) {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  pinMode(FLASH_PIN, OUTPUT);

  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = 40;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erro ao iniciar a câmera: 0x%x", err);
    vTaskDelete(NULL);
  }

  xEventGroupSetBits(xEventGroupKey, EV_START);
  vTaskDelete(NULL);
}

void initWiFi(void *pvParameters) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Conectando ao Wi-Fi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  Serial.println("\nConectado com IP: " + WiFi.localIP().toString());
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
    Serial.println("Conectando ao Firebase...");
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    signupOK = true;

    fbdo.setBSSLBufferSize(16384, 16384);
    fbdo.setResponseSize(4096);
    Firebase.signUp(&config, &auth, "", "");
    Firebase.reconnectWiFi(true);

    userUID = auth.token.uid.c_str();
    xEventGroupSetBits(xEventGroupKey, EV_FIRE);
  }
  vTaskDelete(NULL);
}

void enviarDadosFirebase(void *pvParameters) {
  for (;;) {
    int capturarFoto = 0;
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (xQueueReceive(queueCapturarFoto, &capturarFoto, pdMS_TO_TICKS(100)) && capturarFoto == 1) {
        Serial.println("Capturando imagem...");

        digitalWrite(FLASH_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(200));
        camera_fb_t *fb = esp_camera_fb_get();
        digitalWrite(FLASH_PIN, LOW);

        if (!fb) {
          Serial.println("Erro ao capturar imagem");
          xSemaphoreGive(xSemaphore);
          continue;
        }

        struct tm timeinfo = rtc.getTimeStruct();
        char timestamp[30];
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        String filename = "/img_" + String(timestamp) + ".jpg";

        if (!SPIFFS.begin(true)) {
          Serial.println("Erro ao iniciar SPIFFS");
          esp_camera_fb_return(fb);
          xSemaphoreGive(xSemaphore);
          continue;
        }

        File file = SPIFFS.open(filename, FILE_WRITE);
        if (!file) {
          Serial.println("Erro ao salvar arquivo local");
          esp_camera_fb_return(fb);
          xSemaphoreGive(xSemaphore);
          continue;
        }

        file.write(fb->buf, fb->len);
        file.close();
        esp_camera_fb_return(fb);

        String storagePath = "/users/" + userUID + "/camera/" + String(timestamp) + ".jpg";

        if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, storagePath.c_str(),
                                    mem_storage_type_flash, filename.c_str(), "image/jpeg")) {
          Serial.println("Upload Firebase OK");

          FirebaseJson json;
          json.set("timestamp", timestamp);
          json.set("url", fbdo.downloadURL());
          json.set("temperature", "N/A");
          json.set("humidity", "N/A");

          String dbPath = "/users/" + userUID + "/camera/cameraData/" + String(timestamp);
          if (Firebase.RTDB.setJSON(&fbdo, dbPath, &json)) {
            Serial.println("Metadados enviados");
          } else {
            Serial.println("Erro metadados: " + fbdo.errorReason());
          }
        } else {
          Serial.println("Erro upload: " + fbdo.errorReason());
        }

        SPIFFS.remove(filename);
      }
      xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void consultarEnviarValorInteiroFirebase(void *pvParameters) {
  String path = "/users/" + userUID + "/camera/control";
  for (;;) {
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (Firebase.RTDB.getInt(&fbdo, path.c_str()) && fbdo.dataType() == "int") {
        int valor = fbdo.intData();
        if (valor == 1) {
          xQueueSend(queueCapturarFoto, &valor, 0);
          Firebase.RTDB.setInt(&fbdo, path.c_str(), 0);
        }
      }
      xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  xEventGroupKey = xEventGroupCreate();
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore) xSemaphoreGive(xSemaphore);

  xTaskCreate(InicializaEsp, "InicializaEsp", 5000, NULL, 15, NULL);
  xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL);
  xTaskCreate(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL);
  xTaskCreate(enviarDadosFirebase, "enviarDadosFirebase", 16384, NULL, 1, NULL);
  xTaskCreate(consultarEnviarValorInteiroFirebase, "consultarEnviarValorInteiroFirebase", 8096, NULL, 1, NULL);
}

void loop() {
  // nada
}