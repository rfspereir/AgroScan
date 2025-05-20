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
#include "base64.h"
#include "config.h" // <- Importa as configurações

ESP32Time rtc(0);
struct tm timeinfo;
unsigned long previousMillis = 0;

// Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;
String userUID = "";

// Controle de eventos
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;
TaskHandle_t taskHandlePorta, taskHandleVerificaPorta;
QueueHandle_t queuePortaStatus = xQueueCreate(1, sizeof(int));
QueueHandle_t queuePortaTimer = xQueueCreate(1, sizeof(unsigned long));
QueueHandle_t queueCapturarFoto = xQueueCreate(1, sizeof(int));

// Flags
#define EV_START (1 << 0)
#define EV_WIFI (1 << 1)
#define EV_FIRE (1 << 2)
#define EV_STATUS_PORTA (1 << 10)
#define EV_BUZZER (1 << 11)

// GPIO Câmera
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

// Stream (desativado neste exemplo)
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

// === Funções Principais === //

void InicializaEsp(void *pvParameters) {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  Serial.setDebugOutput(false);

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

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Erro ao iniciar a câmera.");
    return;
  }

  xEventGroupSetBits(xEventGroupKey, EV_START);
  vTaskDelete(NULL);
}

void initWiFi(void *pvParameters) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando-se na rede: ");
  Serial.println(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  Serial.println("\nConectado com o IP: " + WiFi.localIP().toString());
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts++ < 30) vTaskDelay(pdMS_TO_TICKS(100));
  if (attempts < 30) rtc.setTimeStruct(timeinfo);
  else Serial.println("Falha ao obter hora NTP.");
  xEventGroupSetBits(xEventGroupKey, EV_WIFI);
  vTaskDelete(NULL);
}

void monitorWiFi(void *pvParameters) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED && (xEventGroupGetBits(xEventGroupKey) & EV_WIFI)) {
      Serial.println("WiFi desconectado. Tentando reconectar...");
      if (xTaskCreatePinnedToCore(initWiFi, "initWiFi", 5000, NULL, 14, NULL, 1) == pdPASS)
        xEventGroupClearBits(xEventGroupKey, EV_WIFI);
    }
    vTaskDelay(pdMS_TO_TICKS(6000));
  }
}

void conectarFirebase(void *pvParameters) {
  config.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  for (;;) {
    if (xEventGroupGetBits(xEventGroupKey) & EV_WIFI) {
      Serial.println("Conectando ao Firebase...");
      signupOK = true;
      fbdo.setBSSLBufferSize(16384, 16384);
      fbdo.setResponseSize(4096);
      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      userUID = auth.token.uid.c_str();
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
      vTaskDelete(NULL);
    }
  }
}

void monitorFirebase(void *pvParameters) {
  for (;;) {
    if (!signupOK || !Firebase.ready()) {
      Serial.println("Firebase desconectado. Tentando reconectar...");
      if ((xEventGroupGetBits(xEventGroupKey) & EV_FIRE)) {
        if (xTaskCreatePinnedToCore(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL, 1) == pdPASS)
          xEventGroupClearBits(xEventGroupKey, EV_FIRE);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(9000));
  }
}

void enviarDadosFirebase(void *pvParameters) {
  for (;;) {
    int capturarFoto = 0;
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (xQueueReceive(queueCapturarFoto, &capturarFoto, pdMS_TO_TICKS(100)) == pdTRUE && capturarFoto == 1) {
        digitalWrite(FLASH_PIN, HIGH);
        camera_fb_t *fb = esp_camera_fb_get();
        vTaskDelay(pdMS_TO_TICKS(500));
        digitalWrite(FLASH_PIN, LOW);

        if (!fb) {
          Serial.println("Erro ao capturar imagem.");
          return;
        }

        String base64_image = base64::encode((uint8_t *)fb->buf, fb->len);
        char timestamp[20];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &rtc.getTimeStruct());

        FirebaseJson json;
        json.set("timestamp", timestamp);
        json.set("base64_image", base64_image);
        String path = "/users/" + String(userUID) + "/camera/cameraData/" + String(timestamp);
        if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json))
          Serial.println("Dados enviados para o Firebase");
        else {
          Serial.println("Erro ao enviar dados:");
          Serial.println(path);
          Serial.println(fbdo.errorReason());
        }
        esp_camera_fb_return(fb);
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
        xQueueSend(queueCapturarFoto, &valor, 0);
        if (valor == 1) Firebase.RTDB.setInt(&fbdo, path.c_str(), 0);
      }
      xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// === Setup & Loop === //

void setup() {
  Serial.begin(115200);
  xEventGroupKey = xEventGroupCreate();
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore) xSemaphoreGive(xSemaphore);

  xTaskCreate(InicializaEsp, "InicializaEsp", 5000, NULL, 15, NULL);
  vTaskDelay(pdMS_TO_TICKS(500));
  xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL);
  xTaskCreate(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL);
  vTaskDelay(pdMS_TO_TICKS(2000));
  xTaskCreate(monitorWiFi, "monitorWiFi", 5000, NULL, 14, NULL);
  xTaskCreate(enviarDadosFirebase, "enviarDadosFirebase", 16384, NULL, 1, NULL);
  xTaskCreate(consultarEnviarValorInteiroFirebase, "consultarEnviarValorInteiroFirebase", 8096, NULL, 1, NULL);
}

void loop() {
  // vazio, todas as operações são multitarefa
}
