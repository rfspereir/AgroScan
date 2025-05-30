#include <WiFi.h>
#include <Arduino.h>
#include <Firebase_ESP_Client.h>
#include <ESP32Time.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "config.h"  // WIFI_SSID, WIFI_PASSWORD, API_KEY, DATABASE_URL

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

QueueHandle_t queueContador = xQueueCreate(1, sizeof(int));

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

    Firebase.signUp(&config, &auth, "", "");
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    unsigned long timeout = millis();
    while (auth.token.uid == "" && millis() - timeout < 10000) {
      Serial.print(".");
      delay(200);
    }

    if (auth.token.uid != "") {
      userUID = auth.token.uid.c_str();
      signupOK = true;
      Serial.println("\nConectado ao Firebase como UID: " + userUID);
      xEventGroupSetBits(xEventGroupKey, EV_FIRE);
    } else {
      Serial.println("\nFalha ao obter UID.");
    }
  }
  vTaskDelete(NULL);
}

void enviarDadosFirebaseSimulado(void *pvParameters) {
  int contador = 0;

  for (;;) {
    if (signupOK && xSemaphoreTake(xSemaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
      contador++;
      struct tm timeinfo = rtc.getTimeStruct();
      char timestamp[30];
      strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);

      FirebaseJson json;
      json.set("timestamp", timestamp);
      json.set("url", "https://exemplo.com/fotoexemplo.jpg");
      json.set("temperature", "N/A");
      json.set("humidity", "N/A");
      json.set("contador", contador);
      json.set("simulado", true);

      String dbPath = "/users/" + userUID + "/camera/cameraData/" + String(timestamp);
      if (Firebase.RTDB.setJSON(&fbdo, dbPath, &json)) {
        Serial.printf("📤 [%d] Dados enviados com sucesso: %s\n", contador, dbPath.c_str());
      } else {
        Serial.println("Erro ao enviar JSON: " + fbdo.errorReason());
      }

      xSemaphoreGive(xSemaphore);
    }

    vTaskDelay(pdMS_TO_TICKS(10000));  // A cada 10 segundos
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  xEventGroupKey = xEventGroupCreate();
  xSemaphore = xSemaphoreCreateBinary();
  if (xSemaphore) xSemaphoreGive(xSemaphore);

  xTaskCreate(initWiFi, "initWiFi", 5000, NULL, 14, NULL);
  xTaskCreate(conectarFirebase, "conectarFirebase", 5000, NULL, 14, NULL);
  xTaskCreate(enviarDadosFirebaseSimulado, "enviarDadosFirebaseSimulado", 8096, NULL, 1, NULL);
}

void loop() {
  // vazio
}
