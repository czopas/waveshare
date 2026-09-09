#include "ModbusServer.h"
#include <ETH.h>
#include <SPI.h>
#include <ModbusIP_ESP8266.h>
#include "Interlock.h"

// Piny modulu Ethernet W5500 - takie same jak w lib/WS_ETH/src/WS_ETH.h. Nie korzystamy tu z
// gotowego WS_ETH::ETH_Init() - poza uruchomieniem samego portu robi on tez pobranie czasu z NTP
// i zapis do zegara RTC (WS_ETH::EthernetTask/Acquisition_time), a ta petla probuje w kolko
// laczyc sie z pool.ntp.org bez zadnego backoffu. W sieci przemyslowej, w ktorej sterownik ma
// dzialac wylacznie jako serwer Modbus TCP (czesto bez wyjscia do internetu), oznaczaloby to
// nieskonczone, bezuzyteczne proby polaczenia - dlatego tutaj podnosimy tylko sam port sieciowy.
#ifndef ETH_PHY_TYPE
  #define ETH_PHY_TYPE ETH_PHY_W5500
  #define ETH_PHY_ADDR 1
  #define ETH_PHY_CS   16
  #define ETH_PHY_IRQ  12
  #define ETH_PHY_RST  39
#endif
#define ETH_SPI_SCK  15
#define ETH_SPI_MISO 14
#define ETH_SPI_MOSI 13

// Staly adres IP portu Ethernet - celowo taki sam jak domyslny adres AP WiFi panelu WWW
// (WebPanel.cpp, WiFi.softAP -> 192.168.4.1), zeby urzadzenie bylo dostepne pod tym samym
// adresem niezaleznie od tego, czy laczymy sie przez WiFi czy przez ETH. To dwa oddzielne,
// niemostkowane interfejsy sieciowe ESP32, wiec identyczny adres na obu nie koliduje - UWAGA:
// zewnetrzny sterownik/PLC podlaczony do portu ETH musi miec adres z tej samej podsieci
// (192.168.4.x/24), inaczej nie dobije do 192.168.4.1 po kablu.
static IPAddress ethIp(192, 168, 4, 1);
static IPAddress ethGateway(192, 168, 4, 1);
static IPAddress ethSubnet(255, 255, 255, 0);

static ModbusIP mb;
static bool ethGotIp = false;

static void OnEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      ETH.setHostname("esp32-interlock");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      IPAddress ip = ETH.localIP();
      printf("[Modbus] Port Ethernet polaczony, IP: %d.%d.%d.%d - serwer Modbus TCP nasluchuje na porcie 502\r\n",
             ip[0], ip[1], ip[2], ip[3]);
      ethGotIp = true;
      break;
    }
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_STOP:
      ethGotIp = false;
      printf("[Modbus] Port Ethernet rozlaczony\r\n");
      break;
    default:
      break;
  }
}

static void ModbusTask(void *parameter) {
  (void)parameter;
  for (;;) {
    if (ethGotIp) {
      InterlockStatus st = Interlock_GetStatus();
      mb.Ists(MB_ISTS_WIN1_CLOSED, st.win1Closed);
      mb.Ists(MB_ISTS_WIN2_CLOSED, st.win2Closed);
      mb.Ists(MB_ISTS_WIN1_BUTTON, st.btn1Active);
      mb.Ists(MB_ISTS_WIN2_BUTTON, st.btn2Active);
      mb.Ists(MB_ISTS_WIN1_LOCK, st.out[2]);   // CH3 - WIN1_LOCK_CH
      mb.Ists(MB_ISTS_WIN2_LOCK, st.out[5]);   // CH6 - WIN2_LOCK_CH
      mb.Ireg(MB_IREG_PHASE, st.phase);
      mb.task();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  vTaskDelete(NULL);
}

void ModbusServer_Init(void) {
  Network.onEvent(OnEthEvent);
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI);
  ETH.config(ethIp, ethGateway, ethSubnet);  // staly adres zamiast DHCP - patrz komentarz przy ethIp

  mb.server();
  mb.addIsts(MB_ISTS_WIN1_CLOSED);
  mb.addIsts(MB_ISTS_WIN2_CLOSED);
  mb.addIsts(MB_ISTS_WIN1_BUTTON);
  mb.addIsts(MB_ISTS_WIN2_BUTTON);
  mb.addIsts(MB_ISTS_WIN1_LOCK);
  mb.addIsts(MB_ISTS_WIN2_LOCK);
  mb.addIreg(MB_IREG_PHASE);

  xTaskCreatePinnedToCore(
    ModbusTask,
    "ModbusTask",
    4096,
    NULL,
    2,
    NULL,
    1
  );
}
