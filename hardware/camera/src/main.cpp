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
#include "addons/TokenHelper.h" //Provide the token generation process info.
#include "addons/RTDBHelper.h"  //Provide the RTDB payload printing info and other helper functions.
#include <SPIFFS.h>
#include "config.h"
#include "DHT.h"
#define DHTPIN 15      // Escolha um pino digital disponível
#define DHTTYPE DHT22  // Tipo do sensor
DHT dht(DHTPIN, DHTTYPE);
   
ESP32Time rtc(0);
// Estrutura para manter o tempo
struct tm timeinfo;
unsigned long previousMillis = 0;

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
unsigned long sendDataPrevMillis = 0;
int count = 0;
bool signupOK = false;
String userUID = "";

// Variáveis controle de eventos
EventGroupHandle_t xEventGroupKey;
SemaphoreHandle_t xSemaphore;
TaskHandle_t taskHandlePorta, taskHandleVerificaPorta;
QueueHandle_t queuePortaStatus = xQueueCreate(1, sizeof(int));
QueueHandle_t queuePortaTimer = xQueueCreate(1, sizeof(unsigned long));

QueueHandle_t queueCapturarFoto = xQueueCreate(1, sizeof(int));

#define EV_START (1 << 0)
#define EV_WIFI (1 << 1) // Define o bit do evento Conectado ao WI-FI
#define EV_FIRE (1 << 2) // Define o bit do evento Conectado ao Firebase

#define EV_STATUS_PORTA (1 << 10) // Define o bit do evento porta aberta
#define EV_BUZZER (1 << 11)       // Define o bit do evento buzzer ativado

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


#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

void InicializaEsp(void *pvParameters)
{
  for (;;)
  {
    Serial.println("Inicializando ESP32");

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // desativa o detector de brownout

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
    pinMode(FLASH_PIN, OUTPUT); // Configura o pino do flash como saída

    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 40;
    config.fb_count = 2;

    // Inicia a câmera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
      Serial.printf("Camera init failed with error 0x%x", err);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    xEventGroupSetBits(xEventGroupKey, EV_START);
    vTaskDelete(NULL);
  }
}

void initWiFi(void *pvParameters)
{
  for (;;)
  {
    if (WiFi.status() != WL_CONNECTED)
    {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      Serial.print("Conectando-se na rede: ");
      Serial.println(WIFI_SSID);
      while (WiFi.status() != WL_CONNECTED)
      {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println();
        Serial.print("Conectado com o IP: ");
        Serial.println(WiFi.localIP());
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
        struct tm timeinfo;
        const int timeout = 30;
        int attempts = 0;
        Serial.print("Sincronizando a hora atual...");

        while (!getLocalTime(&timeinfo) && attempts < timeout)
        {

          Serial.print(".");

          attempts++;
          vTaskDelay(pdMS_TO_TICKS(100)); // Aguarde 1 segundo antes de tentar novamente
        }

        if (attempts < timeout)
        {
          Serial.println(&timeinfo, "Data e Hora iniciais: %Y-%m-%d %H:%M:%S");
          rtc.setTimeStruct(timeinfo);
        }
        else
        {
          Serial.println("Falha ao obter a hora NTP após várias tentativas.");
        }

        xEventGroupSetBits(xEventGroupKey, EV_WIFI);
        vTaskDelay(pdMS_TO_TICKS(500));
        vTaskDelete(NULL);
      }
    }
  }
}

void monitorWiFi(void *pvParameters)
{
  for (;;)
  {
    EventBits_t xEventGroupValue = xEventGroupGetBits(xEventGroupKey);
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("WiFi desconectado. Tentando reconectar...");
      // Conexão com a internet

      if ((xEventGroupValue & EV_WIFI))
      {
        if (xTaskCreatePinnedToCore(initWiFi, "initWiFi", 5000, NULL, 14, NULL, 1) == pdPASS)
        {
          xEventGroupClearBits(xEventGroupKey, EV_WIFI); // Configura o BIT (EV_WIFI) em 0
        }
        else
        {
          Serial.println("Falha ao criar a tarefa initWiFi");
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(6000)); // Check every 60 seconds
  }
}

void conectarFirebase(void *pvParameters)
{
  const EventBits_t xBitsToWaitFor = (EV_WIFI);
  EventBits_t xEventGroupValue;
  config.api_key = API_KEY;
  // auth.user.email = USER_EMAIL;
  // auth.user.password = USER_PASSWORD;
  config.database_url = DATABASE_URL;

  for (;;)
  {
    xEventGroupValue = xEventGroupWaitBits(xEventGroupKey, xBitsToWaitFor, pdFALSE, pdTRUE, 0);
    if (xEventGroupValue & EV_WIFI)
    {
      Serial.println("Conectando ao Firebase...");
      signupOK = true;
      // config.token_status_callback = tokenStatusCallback; // Assign the callback function for the long running token generation task
      fbdo.setBSSLBufferSize(16384 /* Rx buffer size in bytes from 512 - 16384 */, 16384 /* Tx buffer size in bytes from 512 - 16384 */);
      fbdo.setResponseSize(4096);
      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      Serial.println("Conectado ao Firebase");
      Serial.println("Ready!");
      userUID = auth.token.uid.c_str();

      xEventGroupSetBits(xEventGroupKey, EV_FIRE); // Configura o BIT (EV_2SEG) em 1
      vTaskDelete(NULL);
    }
  }
}

void monitorFirebase(void *pvParameters)
{
  for (;;)
  {
    EventBits_t xEventGroupValue = xEventGroupGetBits(xEventGroupKey);
    if (!signupOK || !Firebase.ready())
    {
      Serial.println("Firebase desconectado. Tentando reconectar...");

      // Verifica se o evento EV_FIRE está desativado
      if ((xEventGroupValue & EV_FIRE))
      {
        if (xTaskCreatePinnedToCore(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL, 1) == pdPASS)
        {
          xEventGroupClearBits(xEventGroupKey, EV_FIRE); // Configura o BIT (EV_2SEG) em 0
        }
        else
        {
          Serial.println("Falha ao criar a tarefa conectarFirebase");
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(9000)); // Check every 60 seconds
  }
}

//-------------------------------------------------------------------

void enviarDadosFirebase(void *pvParameters)
{
  for (;;)
  {
    int capturarFoto = 0;
    if (signupOK)
    {
      if (xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        if ((xQueueReceive(queueCapturarFoto, &capturarFoto, pdMS_TO_TICKS(100))) == pdTRUE && capturarFoto == 1)
        {
          Serial.println("Capturando e enviando imagem...");

          digitalWrite(FLASH_PIN, HIGH);
          vTaskDelay(pdMS_TO_TICKS(200));
          camera_fb_t *fb = esp_camera_fb_get();
          digitalWrite(FLASH_PIN, LOW);

          if (!fb)
          {
            Serial.println("Erro ao capturar imagem");
            xSemaphoreGive(xSemaphore);
            continue;
          }

          // Monta timestamp
          struct tm timeinfo = rtc.getTimeStruct();
          char timestamp[30];
          strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
          String filename = "/img_" + String(timestamp) + ".jpg";

          // Salva imagem localmente
          if (!SPIFFS.begin(true)) {
            Serial.println("Erro ao iniciar SPIFFS");
            esp_camera_fb_return(fb);
            xSemaphoreGive(xSemaphore);
            continue;
          }

          File file = SPIFFS.open(filename, FILE_WRITE);
          if (!file)
          {
            Serial.println("Erro ao abrir arquivo local");
            esp_camera_fb_return(fb);
            xSemaphoreGive(xSemaphore);
            continue;
          }

          file.write(fb->buf, fb->len);
          file.close();
          esp_camera_fb_return(fb);

          // Caminho no Firebase Storage
          String storagePath = "/users/" + userUID + "/camera/" + String(timestamp) + ".jpg";

          if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, storagePath.c_str(),
                                      mem_storage_type_flash, filename.c_str(), "image/jpeg"))
          {
            Serial.println("Upload bem-sucedido no Firebase Storage!");

            // Leitura do DHT
            float temperatura = dht.readTemperature();
            float umidade = dht.readHumidity();

            FirebaseJson json;
            json.set("timestamp", timestamp);
            json.set("url", fbdo.downloadURL());
            json.set("temperature", isnan(temperatura) ? "NaN" : String(temperatura));
            json.set("humidity", isnan(umidade) ? "NaN" : String(umidade));

            String dbPath = "/users/" + userUID + "/camera/cameraData/" + String(timestamp);
            if (Firebase.RTDB.setJSON(&fbdo, dbPath, &json))
              Serial.println("Metadados enviados com sucesso");
            else
              Serial.println("Erro ao enviar metadados: " + fbdo.errorReason());
          }
          else
          {
            Serial.println("Erro ao fazer upload: " + fbdo.errorReason());
          }

          SPIFFS.remove(filename); // Limpeza
        }

        xSemaphoreGive(xSemaphore);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


void consultarEnviarValorInteiroFirebase(void *pvParameters)
{
  // Defina o caminho no Firebase RTDB onde você deseja consultar e enviar o valor
  String path = "/users/" + userUID + "/camera/control";
  int valorCapturado = 0;

  for (;;)
  {
    if (signupOK)
    {
      if (xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        // Consultar valor inteiro do Firebase
        if (Firebase.RTDB.getInt(&fbdo, path.c_str()))
        {
          if (fbdo.dataType() == "int")
          {
            int valorAtual = fbdo.intData();
            xQueueSend(queueCapturarFoto, &valorAtual, 0);
            Serial.printf("Valor atual consultado do Firebase: %d\n", valorAtual);

            if (valorAtual = 1)
            { // Configurar o valor como zero após a consulta
              if (Firebase.RTDB.setInt(&fbdo, path.c_str(), 0))
              {
                Serial.println("Valor configurado como zero no Firebase com sucesso");
                Serial.printf("Path: %s, Valor: 0\n", path.c_str());
              }
              else
              {
                Serial.print("Falha ao configurar o valor como zero: ");
                Serial.println(fbdo.errorReason());
              }
            }
          }
          else
          {
            Serial.println("Os dados consultados não são do tipo inteiro");
          }
        }
        else
        {
          Serial.print("Falha ao consultar valor inteiro: ");
          Serial.println(fbdo.errorReason());
        }
        xSemaphoreGive(xSemaphore);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000)); // Aguarde 5 segundos antes de consultar novamente
  }
}


void setup()
{

  Serial.begin(115200);
  delay(200);

  xEventGroupKey = xEventGroupCreate(); // Cria Event Group
  if (xEventGroupKey == NULL)
  {
    Serial.printf("\n\rFalha em criar a Event Group xEventGroupKey");
  }

  // Criação do semáforo
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore != NULL)
  {
    xSemaphoreGive(xSemaphore); // Libera o semáforo inicialmente
  }

  // Criação da tarefa InicializaEsp
  if (xTaskCreate(InicializaEsp, "InicializaEsp", 5000, NULL, 15, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa InicializaEsp");
  }
  delay(500);
  // Conexão com a internet
  if (xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa initWiFi");
  }
  // Criação da tarefa conectarFirebase
  if (xTaskCreate(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa conectarFirebase");
  }
  delay(2000);
  // Tarefa monitoraWiFi
  if (xTaskCreate(monitorWiFi, "monitorWiFi", 5000, NULL, 14, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa monitorWiFi");
  }

  if (xTaskCreate(enviarDadosFirebase, "enviarDadosFirebase", 16384, NULL, 1, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa enviarDadosFirebase");
  }

  if (xTaskCreate(consultarEnviarValorInteiroFirebase, "consultarEnviarValorInteiroFirebase", 8096, NULL, 1, NULL) != pdPASS)
  {
    Serial.println("Falha ao criar a tarefa consultarEnviarValorInteiroFirebase");
  }
}

void loop()
{
}