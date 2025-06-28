#pragma once

#ifdef CONFIG_TARGET_ESP32CAM
  // AI-Thinker ESP32-CAM (ESP32-WROOM-32)
  #define CAM_PIN_PWDN    32
  #define CAM_PIN_RESET   -1
  #define CAM_PIN_XCLK     0
  #define CAM_PIN_SIOD    26
  #define CAM_PIN_SIOC    27
  #define CAM_PIN_D7      35
  #define CAM_PIN_D6      34
  #define CAM_PIN_D5      39
  #define CAM_PIN_D4      36
  #define CAM_PIN_D3      21
  #define CAM_PIN_D2      19
  #define CAM_PIN_D1      18
  #define CAM_PIN_D0       5
  #define CAM_PIN_VSYNC   25
  #define CAM_PIN_HREF    23
  #define CAM_PIN_PCLK    22

  //outros pinos
  //Reset
  #define PIN_RESET       12

#elif defined(CONFIG_TARGET_ESP32S3)
  // GOOUUU Tech ESP32-S3-CAM (40 pinos, 16MB flash, 8MB PSRAM)
  #define CAM_PIN_PWDN    -1
  #define CAM_PIN_RESET   -1
  #define CAM_PIN_XCLK    15
  #define CAM_PIN_SIOD    4
  #define CAM_PIN_SIOC    5

  #define CAM_PIN_D7      16
  #define CAM_PIN_D6      17
  #define CAM_PIN_D5      18
  #define CAM_PIN_D4      12
  #define CAM_PIN_D3      10
  #define CAM_PIN_D2      8
  #define CAM_PIN_D1      9
  #define CAM_PIN_D0      11
  #define CAM_PIN_VSYNC   6
  #define CAM_PIN_HREF    7
  #define CAM_PIN_PCLK    13

  //outros pinos

  #define RESET_PIN       12
  #define DHT_PIN 13
  #define DHTTYPE DHT11

#else
  #error "Nenhum alvo de câmera definido. Use -D CONFIG_TARGET_ESP32CAM ou -D CONFIG_TARGET_ESP32S3CAM"
#endif