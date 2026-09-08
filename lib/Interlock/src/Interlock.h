#pragma once

#include <Arduino.h>
#include "WS_DIN.h"
#include "WS_Relay.h"
#include "WS_GPIO.h"
#include "I2C_Driver.h"

/*******************************************************  Mapowanie kanalow  *******************************************************/
// Wejscia (DIN_Read_CHx z WS_DIN)
#define WIN1_BTN_CH        1     // DI1 - przycisk zadania wejscia okna 1
#define WIN1_CLOSED_CH     2     // DI2 - kontaktron okna 1 (aktywny = okno zamkniete)
#define WIN2_BTN_CH        3     // DI3 - przycisk zadania wejscia okna 2
#define WIN2_CLOSED_CH     4     // DI4 - kontaktron okna 2 (aktywny = okno zamkniete)

// Wyjscia (Relay_* z WS_Relay, kanaly 1-8 przez ekspander TCA9554PWR)
#define WIN1_GREEN_CH      1     // CH1 - lampka zielona okna 1
#define WIN1_RED_CH        2     // CH2 - lampka czerwona okna 1
#define WIN1_LOCK_CH       3     // CH3 - elektrozaczep okna 1 (rewersyjny, HIGH = zamek zamkniety)
#define WIN2_GREEN_CH      4     // CH4 - lampka zielona okna 2
#define WIN2_RED_CH        5     // CH5 - lampka czerwona okna 2
#define WIN2_LOCK_CH       6     // CH6 - elektrozaczep okna 2 (rewersyjny, HIGH = zamek zamkniety)

/*******************************************************  Parametry czasowe  *******************************************************/
// Wartosci startowe - przy pierwszym uruchomieniu (brak zapisu w NVS) albo po przywroceniu
// domyslnych. Po zmianie z panelu WWW aktualne wartosci sa trzymane w Preferences (NVS) i
// przetrwaja restart - patrz Interlock_SetTimings()/Interlock_GetStatus().
#define LOCK_RELEASE_MS_DEFAULT   3000UL   // czas zwolnienia zaczepu po zadaniu wejscia
#define REGEN_MS_DEFAULT          5000UL   // czas regeneracji miedzy kolejnymi otwarciami
#define DEBOUNCE_MS_DEFAULT       50UL     // czas stabilizacji odczytu wejsc (przyciski + kontaktrony)

#define BLINK_PERIOD_MS    500UL    // pol-okres migania (1 Hz = 500 ms swieci / 500 ms nie swieci)
#define POLL_PERIOD_MS     20UL     // okres cyklu InterlockTask

// Polaryzacja wejsc: 1 = wejscie "aktywne" (przycisk wcisniety / kontaktron potwierdza opisany
// przez uzytkownika stan) odpowiada odczytowi GPIO LOW (typowe dla wejsc przemyslowych z
// optoizolacja + pull-up, zgodnie z konwencja DIN_Inverse_Enable juz obecna w WS_DIN).
#define DI_ACTIVE_LOW      1

// Buzzer alarmowy (stan "oba okna otwarte") - wylaczony na czas testow (bardzo glosny).
// Ustawic na 1, gdy reszta logiki bedzie juz dzialac poprawnie.
#define ALARM_BUZZER_ENABLED 1

/*******************************************************  Status / API  *******************************************************/
// Wartosci pola "phase" - patrz takze Interlock_PhaseName().
#define IL_PHASE_READY            0
#define IL_PHASE_UNLOCKED         1
#define IL_PHASE_OPEN             2
#define IL_PHASE_REGEN            3
#define IL_PHASE_ALARM             4

struct InterlockStatus {
  uint8_t phase;             // patrz IL_PHASE_* / Interlock_PhaseName()
  uint8_t activeWindow;      // 0 = brak, 1 = okno 1, 2 = okno 2
  bool btn1Active;           // DI1 (debounced)
  bool btn2Active;           // DI3 (debounced)
  bool win1Closed;           // DI2 (debounced, true = okno 1 zamkniete)
  bool win2Closed;           // DI4 (debounced, true = okno 2 zamkniete)
  bool out[6];                // CH1..CH6, indeks 0..5
  unsigned long lockReleaseMs;
  unsigned long regenMs;
  unsigned long debounceMs;
  unsigned long phaseElapsedMs; // czas od wejscia w biezaca faze [ms]
};

void Interlock_Init(void);

// Biezacy status - bezpieczne wywolanie z innego zadania (np. panelu WWW), zwraca kopie.
InterlockStatus Interlock_GetStatus(void);

// Zmiana parametrow czasowych w locie + zapis do NVS (przetrwa restart). Wartosci spoza
// rozsadnego zakresu sa odrzucane (patrz walidacja w Interlock.cpp) i funkcja zwraca false.
bool Interlock_SetTimings(unsigned long lockReleaseMs, unsigned long regenMs, unsigned long debounceMs);

const char *Interlock_PhaseName(uint8_t phase);
