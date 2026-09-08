#include "Interlock.h"
#include <Preferences.h>

/*******************************************************  Pomocnicze funkcje sprzetowe  *******************************************************/

// Konwersja surowego odczytu DIN_Read_CHx() na stan logiczny "aktywny" (patrz DI_ACTIVE_LOW w Interlock.h).
static inline bool DI_Active(bool raw) {
  return DI_ACTIVE_LOW ? !raw : raw;
}

// Wlasny cache ostatnio zadanego stanu kazdego z 8 kanalow. Nie mozna do tego uzyc Relay_Flag[]
// z WS_Relay - ta tablica jest aktualizowana wylacznie przez Relay_Immediate*()/Relay_Analysis(),
// natomiast Relay_CHx()/Relay_Open()/Relay_Closs() (ktorych uzywamy tutaj) w ogole jej nie dotykaja,
// wiec pozostawalaby wiecznie na wartosci startowej {0} - w praktyce blokujac wszystkie zadania
// "wylacz" (on=false), bo cache zawsze "widzialby" juz false.
static bool outputState[8] = {false, false, false, false, false, false, false, false};

static inline void Lamp_Set(uint8_t ch, bool on) {
  if (outputState[ch - 1] != on) {
    Relay_CHx(ch, on);
    outputState[ch - 1] = on;
  }
}

// Elektrozaczep rewersyjny: stan wysoki = zamek zamkniety (patrz opis w Interlock.h).
// Relay_Open()/Relay_Closs() z WS_Relay operuja na surowym stanie pinu (HIGH/LOW), nie na
// znaczeniu fizycznym - dlatego owijamy je w jednoznacznie nazwane funkcje.
static inline void Lock_Engage(uint8_t ch) {
  if (!outputState[ch - 1]) {
    Relay_Open(ch);
    outputState[ch - 1] = true;
  }
}
static inline void Lock_Release(uint8_t ch) {
  if (outputState[ch - 1]) {
    Relay_Closs(ch);
    outputState[ch - 1] = false;
  }
}

/*******************************************************  Debounce  *******************************************************/

struct Debounce {
  bool stable = false;
  bool lastRaw = false;
  unsigned long lastChangeMs = 0;
};

static bool Debounce_Update(Debounce &d, bool rawActive, unsigned long debounceMs) {
  unsigned long now = millis();
  if (rawActive != d.lastRaw) {
    d.lastRaw = rawActive;
    d.lastChangeMs = now;
  }
  if ((now - d.lastChangeMs) >= debounceMs) {
    d.stable = d.lastRaw;
  }
  return d.stable;
}

/*******************************************************  Parametry czasowe (NVS)  *******************************************************/

static Preferences prefs;
static SemaphoreHandle_t g_mutex;

// "Prawda" o biezacych parametrach i statusie - dostep wylacznie pod oslona g_mutex.
static unsigned long g_lockReleaseMs = LOCK_RELEASE_MS_DEFAULT;
static unsigned long g_regenMs = REGEN_MS_DEFAULT;
static unsigned long g_debounceMs = DEBOUNCE_MS_DEFAULT;
static InterlockStatus g_status = {};

static const char *kPhaseNames[] = {"READY", "UNLOCKED", "OPEN", "REGEN", "ALARM"};

const char *Interlock_PhaseName(uint8_t phase) {
  if (phase >= (sizeof(kPhaseNames) / sizeof(kPhaseNames[0]))) return "UNKNOWN";
  return kPhaseNames[phase];
}

InterlockStatus Interlock_GetStatus(void) {
  InterlockStatus copy;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  copy = g_status;
  xSemaphoreGive(g_mutex);
  return copy;
}

bool Interlock_SetTimings(unsigned long lockReleaseMs, unsigned long regenMs, unsigned long debounceMs) {
  // Rozsadne granice bezpieczenstwa - zabezpieczenie przed wpisaniem z panelu WWW wartosci,
  // ktore zablokowalyby sluze (np. debounce=0) albo uczynily ja bezuzyteczna (regen=godziny).
  if (lockReleaseMs < 200 || lockReleaseMs > 60000) return false;
  if (regenMs > 300000) return false;
  if (debounceMs < 5 || debounceMs > 1000) return false;

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_lockReleaseMs = lockReleaseMs;
  g_regenMs = regenMs;
  g_debounceMs = debounceMs;
  xSemaphoreGive(g_mutex);

  prefs.putULong("lockRelMs", lockReleaseMs);
  prefs.putULong("regenMs", regenMs);
  prefs.putULong("debounceMs", debounceMs);
  printf("[Interlock] Nowe parametry: zwolnienie=%lums regeneracja=%lums debounce=%lums\r\n",
         lockReleaseMs, regenMs, debounceMs);
  return true;
}

/*******************************************************  Automat stanow  *******************************************************/

enum Phase { PHASE_READY, PHASE_UNLOCKED, PHASE_OPEN, PHASE_REGEN, PHASE_ALARM };
enum ActiveWindow { WIN_NONE, WIN_1, WIN_2 };

static Phase phase = PHASE_REGEN;
static ActiveWindow active = WIN_NONE;
static unsigned long phaseStartMs = 0;

static Debounce btn1Db, closed1Db, btn2Db, closed2Db;
static bool btn1Prev = false;
static bool btn2Prev = false;

static bool blinkOn = false;
static unsigned long lastBlinkToggleMs = 0;

static void EnterPhase(Phase p) {
  phase = p;
  phaseStartMs = millis();
  printf("[Interlock] Faza -> %s (aktywne okno: %d)\r\n", kPhaseNames[p], (int)active);
}

static void UpdateBlink() {
  unsigned long now = millis();
  if (now - lastBlinkToggleMs >= BLINK_PERIOD_MS) {
    lastBlinkToggleMs = now;
    blinkOn = !blinkOn;
  }
}

// Ustawia lampki aktywnego okna (miga zielona) oraz okna przeciwnego (czerwona ciagla).
static void ApplyActiveLamps(ActiveWindow who) {
  bool isWin1 = (who == WIN_1);
  uint8_t activeGreen = isWin1 ? WIN1_GREEN_CH : WIN2_GREEN_CH;
  uint8_t activeRed = isWin1 ? WIN1_RED_CH : WIN2_RED_CH;
  uint8_t otherGreen = isWin1 ? WIN2_GREEN_CH : WIN1_GREEN_CH;
  uint8_t otherRed = isWin1 ? WIN2_RED_CH : WIN1_RED_CH;

  Lamp_Set(activeGreen, blinkOn);
  Lamp_Set(activeRed, false);
  Lamp_Set(otherGreen, false);
  Lamp_Set(otherRed, true);
}

// Publikuje biezacy stan (do odczytu przez panel WWW) - wywolywane raz na kazdy cykl InterlockTask.
static void PublishStatus(bool btn1Stable, bool btn2Stable, bool closed1Stable, bool closed2Stable,
                           unsigned long lockReleaseMs, unsigned long regenMs, unsigned long debounceMs) {
  InterlockStatus s;
  s.phase = (uint8_t)phase;
  s.activeWindow = (uint8_t)active;
  s.btn1Active = btn1Stable;
  s.btn2Active = btn2Stable;
  s.win1Closed = closed1Stable;
  s.win2Closed = closed2Stable;
  for (int i = 0; i < 6; i++) s.out[i] = outputState[i];
  s.lockReleaseMs = lockReleaseMs;
  s.regenMs = regenMs;
  s.debounceMs = debounceMs;
  s.phaseElapsedMs = millis() - phaseStartMs;

  xSemaphoreTake(g_mutex, portMAX_DELAY);
  g_status = s;
  xSemaphoreGive(g_mutex);
}

static void InterlockTask(void *parameter) {
  (void)parameter;
  while (1) {
    unsigned long lockReleaseMs, regenMs, debounceMs;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    lockReleaseMs = g_lockReleaseMs;
    regenMs = g_regenMs;
    debounceMs = g_debounceMs;
    xSemaphoreGive(g_mutex);

    bool btn1Stable = Debounce_Update(btn1Db, DI_Active(DIN_Read_CH1()), debounceMs);
    bool closed1Stable = Debounce_Update(closed1Db, DI_Active(DIN_Read_CH2()), debounceMs);
    bool btn2Stable = Debounce_Update(btn2Db, DI_Active(DIN_Read_CH3()), debounceMs);
    bool closed2Stable = Debounce_Update(closed2Db, DI_Active(DIN_Read_CH4()), debounceMs);

    bool btn1Edge = btn1Stable && !btn1Prev;
    bool btn2Edge = btn2Stable && !btn2Prev;
    btn1Prev = btn1Stable;
    btn2Prev = btn2Stable;

    UpdateBlink();

    // Okno moze byc otwarte WYLACZNIE gdy jest aktywne i jest w trakcie autoryzowanego cyklu
    // (UNLOCKED/OPEN) - w kazdej innej sytuacji (READY, REGEN, cudzy cykl, a takze oba naraz)
    // otwarte okno jest anomalia: zaczep powinien trzymac je zablokowane, wiec fizyczne otwarcie
    // oznacza usterke zaczepu albo wymuszenie z zewnatrz. Sprawdzamy to w kazdym cyklu, a nie
    // tylko w chwili zamkniecia, wiec ponowne otwarcie w trakcie regeneracji/gotowosci tez zostanie
    // wykryte, a nie dopiero przy nastepnym starcie cyklu.
    bool win1AuthorizedOpen = (active == WIN_1) && (phase == PHASE_UNLOCKED || phase == PHASE_OPEN);
    bool win2AuthorizedOpen = (active == WIN_2) && (phase == PHASE_UNLOCKED || phase == PHASE_OPEN);
    bool win1UnexpectedOpen = !closed1Stable && !win1AuthorizedOpen;
    bool win2UnexpectedOpen = !closed2Stable && !win2AuthorizedOpen;
    bool anomaly = win1UnexpectedOpen || win2UnexpectedOpen;

    // Stan awaryjny ma pierwszenstwo nad normalnym automatem - blokada nie powinna do niego
    // dopuscic, ale jesli mimo to wykryjemy nieautoryzowane otwarcie, sygnalizujemy to natychmiast
    // niezaleznie od tego, w jakiej fazie byl system wczesniej.
    if (anomaly && phase != PHASE_ALARM) {
      active = WIN_NONE;
      EnterPhase(PHASE_ALARM);
    }

    switch (phase) {
      case PHASE_READY:
        Lock_Engage(WIN1_LOCK_CH);
        Lock_Engage(WIN2_LOCK_CH);
        Lamp_Set(WIN1_GREEN_CH, true);
        Lamp_Set(WIN1_RED_CH, false);
        Lamp_Set(WIN2_GREEN_CH, true);
        Lamp_Set(WIN2_RED_CH, false);

        if (btn1Edge) {
          active = WIN_1;
          Lock_Release(WIN1_LOCK_CH);
          EnterPhase(PHASE_UNLOCKED);
        } else if (btn2Edge) {
          active = WIN_2;
          Lock_Release(WIN2_LOCK_CH);
          EnterPhase(PHASE_UNLOCKED);
        }
        break;

      case PHASE_UNLOCKED: {
        ApplyActiveLamps(active);
        bool activeClosed = (active == WIN_1) ? closed1Stable : closed2Stable;
        uint8_t lockCh = (active == WIN_1) ? WIN1_LOCK_CH : WIN2_LOCK_CH;

        if (!activeClosed) {
          // Okno zostalo fizycznie otwarte - zaczep moze od razu wrocic do stanu zablokowanego,
          // fizyczne polozenie okna samo w sobie uniemozliwia jego domkniecie na zamek.
          Lock_Engage(lockCh);
          EnterPhase(PHASE_OPEN);
        } else if (millis() - phaseStartMs >= lockReleaseMs) {
          // Czas na wejscie minal, a okno nigdy nie zostalo otwarte - zaczep wraca do stanu
          // zablokowanego i wracamy od razu do gotowosci, bez naliczania regeneracji.
          Lock_Engage(lockCh);
          active = WIN_NONE;
          EnterPhase(PHASE_READY);
        }
        break;
      }

      case PHASE_OPEN: {
        ApplyActiveLamps(active);
        bool activeClosed = (active == WIN_1) ? closed1Stable : closed2Stable;
        if (activeClosed) {
          EnterPhase(PHASE_REGEN);
        }
        break;
      }

      case PHASE_REGEN:
        Lock_Engage(WIN1_LOCK_CH);
        Lock_Engage(WIN2_LOCK_CH);
        Lamp_Set(WIN1_GREEN_CH, false);
        Lamp_Set(WIN1_RED_CH, true);
        Lamp_Set(WIN2_GREEN_CH, false);
        Lamp_Set(WIN2_RED_CH, true);

        if (millis() - phaseStartMs >= regenMs) {
          active = WIN_NONE;
          EnterPhase(PHASE_READY);
        }
        break;

      case PHASE_ALARM:
        Lock_Engage(WIN1_LOCK_CH);
        Lock_Engage(WIN2_LOCK_CH);
        // Naprzemienne miganie: gdy blinkOn - zielona okna 1 i czerwona okna 2, w drugiej
        // polowie cyklu odwrotnie. Ten sam wzor niezaleznie od tego, czy anomalia dotyczy
        // jednego czy obu okien - w kazdym przypadku oznacza "nie podchodz, cos jest nie tak".
        Lamp_Set(WIN1_GREEN_CH, blinkOn);
        Lamp_Set(WIN1_RED_CH, !blinkOn);
        Lamp_Set(WIN2_GREEN_CH, !blinkOn);
        Lamp_Set(WIN2_RED_CH, blinkOn);
        if (ALARM_BUZZER_ENABLED) {
          if (blinkOn) Buzzer_Open(); else Buzzer_Closs();
        }

        if (!anomaly) {
          if (ALARM_BUZZER_ENABLED) Buzzer_Closs();
          // Po zdarzeniu awaryjnym wymuszamy pelny czas regeneracji jako dodatkowy margines
          // bezpieczenstwa, zamiast wracac od razu do gotowosci.
          EnterPhase(PHASE_REGEN);
        }
        break;
    }

    PublishStatus(btn1Stable, btn2Stable, closed1Stable, closed2Stable, lockReleaseMs, regenMs, debounceMs);

    vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
  }
  vTaskDelete(NULL);
}

void Interlock_Init(void) {
  g_mutex = xSemaphoreCreateMutex();

  prefs.begin("interlock", false);
  g_lockReleaseMs = prefs.getULong("lockRelMs", LOCK_RELEASE_MS_DEFAULT);
  g_regenMs = prefs.getULong("regenMs", REGEN_MS_DEFAULT);
  g_debounceMs = prefs.getULong("debounceMs", DEBOUNCE_MS_DEFAULT);
  printf("[Interlock] Parametry z NVS: zwolnienie=%lums regeneracja=%lums debounce=%lums\r\n",
         g_lockReleaseMs, g_regenMs, g_debounceMs);

  I2C_Init();
  DIN_Init();
  Relay_Init();

  // Zabezpieczenie przed przejsciowym stanem "wszystkie wyjscia LOW" ustawianym domyslnie przez
  // Relay_Init()/TCA9554PWR_Init() - natychmiast wymuszamy zaczepy w stanie zablokowanym.
  // Uzywamy tych samych helperow co InterlockTask, zeby outputState[] od razu bylo spojne
  // z rzeczywistym stanem wyjsc.
  Lock_Engage(WIN1_LOCK_CH);
  Lock_Engage(WIN2_LOCK_CH);
  Lamp_Set(WIN1_GREEN_CH, false);
  Lamp_Set(WIN1_RED_CH, false);
  Lamp_Set(WIN2_GREEN_CH, false);
  Lamp_Set(WIN2_RED_CH, false);

  lastBlinkToggleMs = millis();
  // Start w fazie regeneracji jako bezpieczny margines po wlaczeniu zasilania, zamiast od razu
  // zakladac, ze oba okna sa gotowe.
  EnterPhase(PHASE_REGEN);

  xTaskCreatePinnedToCore(
    InterlockTask,
    "InterlockTask",
    4096,
    NULL,
    4,
    NULL,
    0
  );
}
