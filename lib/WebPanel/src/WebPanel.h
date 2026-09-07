#pragma once

// Panel WWW: sterownik tworzy wlasny punkt dostepu WiFi (AP), po zalogowaniu (HTTP Basic Auth)
// dostepny jest podglad na biezaco stanu wejsc/wyjsc i fazy blokady, formularz zmiany parametrow
// czasowych oraz strona do wgrania nowego firmware (OTA przez przeglądarke).
//
// WAZNE: ponizsze dane dostepowe sa wartosciami startowymi trafiajacymi do repozytorium -
// PRZED WDROZENIEM PRODUKCYJNYM NALEZY JE ZMIENIC.
#define AP_SSID            "BK-P26001"
#define AP_PASSWORD         "!Control4266"   // WPA2 wymaga min. 8 znakow

#define WEB_LOGIN_USER      "admin"
#define WEB_LOGIN_PASSWORD  "!Control4266"

void WebPanel_Init(void);
