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

ESP32Time rtc(0);

// Config Wi-Fi
#define WIFI_SSID "SeuSSID"
#define WIFI_PASSWORD "SuaSenha"

// Firebase
#define API_KEY "SUA_API_KEY"
#define DATABASE_URL "https://seu-projeto.firebaseio.com/"
#define STORAGE_BUCKET_ID "seu-projeto.appspot.com"

FirebaseData fbdo;
FirebaseAuth auth; // deixado vazio para acesso sem autenticação
FirebaseConfig config;

String idCliente = "";
String idDispositivo = "";
String macAddress = "";

EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;
QueueHandle_t queueCapturarFoto = xQueueCreate(1, sizeof(int));

#define EV_WIFI (1 << 1)
#define FLASH_PIN 4

// GPIOs da câmera
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

void initCamera() {
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
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 2;

  pinMode(FLASH_PIN, OUTPUT);
  esp_camera_init(&config);
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado: " + WiFi.localIP().toString());
  xEventGroupSetBits(xEventGroupKey, EV_WIFI);
}

void obterMAC() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macAddress = String(macStr);
}

void carregarConfiguracaoDoFirebase() {
  String path = "/CONFIGS_DISPOSITIVOS/" + macAddress;
  if (Firebase.RTDB.getJSON(&fbdo, path)) {
    FirebaseJson &json = fbdo.jsonObject();
    FirebaseJsonData result;

    if (json.get(result, "id_cliente")) idCliente = result.to<String>();
    if (json.get(result, "id_dispositivo")) idDispositivo = result.to<String>();

    Serial.println("idCliente: " + idCliente);
    Serial.println("idDispositivo: " + idDispositivo);
  } else {
    Serial.println("Falha ao carregar configuração: " + fbdo.errorReason());
  }
}

void enviarImagemParaFirebase(void *pvParameters) {
  for (;;) {
    int capturarFoto = 0;
    if (Firebase.ready() && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (xQueueReceive(queueCapturarFoto, &capturarFoto, pdMS_TO_TICKS(100)) == pdTRUE && capturarFoto == 1) {
        digitalWrite(FLASH_PIN, HIGH);
        camera_fb_t *fb = esp_camera_fb_get();
        delay(300);
        digitalWrite(FLASH_PIN, LOW);

        if (!fb) {
          Serial.println("Erro ao capturar imagem.");
          xSemaphoreGive(xSemaphore);
          continue;
        }

        struct tm timeinfo = rtc.getTimeStruct();
        char timestampStr[25];
        strftime(timestampStr, sizeof(timestampStr), "%Y-%m-%d_%H-%M-%S", &timeinfo);

        String caminhoStorage = "/clientes/" + idCliente + "/dispositivos/" + idDispositivo + "/" + String(timestampStr) + ".jpg";
        String contentType = "image/jpeg";

        if (Firebase.Storage.upload(&fbdo, caminhoStorage.c_str(), mem_storage_type_flash, fb->buf, fb->len, contentType.c_str())) {
          Serial.println("Upload concluído no Storage");

          String pathRTDB = String("/CLIENTES/") + idCliente + "/DISPOSITIVOS/" + idDispositivo + "/dados_coletados/coleta/" + timestampStr;


          FirebaseJson json;
          json.set("timestamp", timestampStr);
          json.set("imagem_ref", caminhoStorage);
          json.set("dados_temp_umidade", 0);
          json.set("dados_pos_processamento", "");

          if (Firebase.RTDB.setJSON(&fbdo, pathRTDB.c_str(), &json)) {
            Serial.println("Referência salva no RTDB");
          } else {
            Serial.println("Erro ao salvar RTDB: " + fbdo.errorReason());
          }

        } else {
          Serial.println("Erro no upload: " + fbdo.errorReason());
        }

        esp_camera_fb_return(fb);
      }
      xSemaphoreGive(xSemaphore);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  xEventGroupKey = xEventGroupCreate();
  xSemaphore = xSemaphoreCreateBinary();
  xSemaphoreGive(xSemaphore);

  connectWiFi();
  obterMAC();
  initCamera();

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.storage_bucket = STORAGE_BUCKET_ID;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  carregarConfiguracaoDoFirebase();

  xTaskCreate(enviarImagemParaFirebase, "enviarImagem", 8192, NULL, 1, NULL);

  int iniciar = 1;
  xQueueSend(queueCapturarFoto, &iniciar, 0); // dispara envio inicial
}

void loop() {
}