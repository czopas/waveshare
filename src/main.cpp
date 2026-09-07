#include "WS_GPIO.h"
#include "Interlock.h"
#include "WebPanel.h"

/********************************************************  Initializing  ********************************************************/
void setup() {
  delay(10000);          // czekamy na stabilizacje zasilania
  printf("[Main] Start\r\n");
  GPIO_Init();       // RGB (nieuzywane) + buzzer, wykorzystywany do sygnalizacji alarmu
  Interlock_Init();  // I2C, wejscia, przekazniki, automat stanow blokady krzyzowej + zadanie FreeRTOS
  WebPanel_Init();   // AP WiFi + panel WWW (podglad, konfiguracja czasow, OTA)
}

/**********************************************************  While  **********************************************************/
void loop() {
  vTaskDelay(portMAX_DELAY); // cala logika sterownika dziala w InterlockTask
}
