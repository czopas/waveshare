#pragma once

// Serwer Modbus TCP (port 502) na porcie Ethernet - udostepnia do odczytu przez zewnetrzne
// urzadzenie (PLC/SCADA) biezacy stan blokady krzyzowej.
//
// Discrete Inputs - odczyt, function code 02 (mb.Ists):
#define MB_ISTS_WIN1_CLOSED   0   // DI2 - kontaktron okna 1 (1 = zamkniete)
#define MB_ISTS_WIN2_CLOSED   1   // DI4 - kontaktron okna 2 (1 = zamkniete)
#define MB_ISTS_WIN1_BUTTON   2   // DI1 - przycisk zadania okna 1 (1 = wcisniety)
#define MB_ISTS_WIN2_BUTTON   3   // DI3 - przycisk zadania okna 2 (1 = wcisniety)
#define MB_ISTS_WIN1_LOCK     4   // CH3 - elektrozaczep okna 1 (1 = zablokowany/zamkniety)
#define MB_ISTS_WIN2_LOCK     5   // CH6 - elektrozaczep okna 2 (1 = zablokowany/zamkniety)

// Input Registers - odczyt, function code 04 (mb.Ireg):
#define MB_IREG_PHASE          0  // faza automatu: 0=READY 1=UNLOCKED 2=OPEN 3=REGEN 4=ALARM

void ModbusServer_Init(void);
