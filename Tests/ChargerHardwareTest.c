/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Drivers/ChargerDxe/ChargerDxe.c"
#include "../Drivers/ChargerDxe/Charger.c"

EFI_BOOT_SERVICES *gBS;
static UINT32 Wdt[3], AdcControl, AdcResult;
static BOOLEAN AdcStuck, ClockStuck;
static UINTN DelayCalls, WdtWrites, AdcSamples, Signals, TimerCancels;
static UINTN Modes[0x2400], Levels[0x2400];
static UINT8 Bits[32];
static UINTN BitCount, BitIndex, Edges, Driven[64];

UINT32 EFIAPI MmioRead32(UINTN Address)
{
  if (Address >= WDT_BASE && Address <= WDT_BASE + 8) return Wdt[(Address - WDT_BASE) / 4];
  if (Address == ADC_BASE) return AdcControl | (AdcStuck ? 0 : BIT15);
  assert(Address == ADC_BASE + 0x0C);
  ++AdcSamples;
  return AdcResult;
}

UINT32 EFIAPI MmioWrite32(UINTN Address, UINT32 Value)
{
  if (Address >= WDT_BASE && Address <= WDT_BASE + 8) {
    ++WdtWrites;
    Wdt[(Address - WDT_BASE) / 4] = Value;
  } else if (Address == ADC_BASE) {
    AdcControl = Value;
  } else {
    assert(Address == ADC_BASE + 0x1C || Address == ADC_BASE + 0x18);
    assert(Value == (Address == ADC_BASE + 0x1C ? 7U : 0U));
  }
  return Value;
}

UINTN EFIAPI MicroSecondDelay(UINTN Microseconds)
{
  ++DelayCalls;
  return Microseconds;
}

static EFI_STATUS EFIAPI Config(EXYNOS_GPIO_PROTOCOL *This, UINTN Pin, UINTN Mode, UINTN Pull)
{
  if ((Pin == mChargerBus.Sda || Pin == mChargerBus.Scl) && Mode == EXYNOS_GPIO_MODE_OUTPUT)
    assert(Levels[Pin] == 0);
  if (Pin == mChargerBus.Scl && Mode == EXYNOS_GPIO_MODE_INPUT) {
    if (Edges < ARRAY_SIZE(Driven)) Driven[Edges++] = Modes[mChargerBus.Sda];
  }
  Modes[Pin] = Mode;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Set(EXYNOS_GPIO_PROTOCOL *This, UINTN Pin, UINTN Value)
{
  Levels[Pin] = Value;
  return EFI_SUCCESS;
}

static UINTN EFIAPI Get(EXYNOS_GPIO_PROTOCOL *This, UINTN Pin)
{
  if (Modes[Pin] == EXYNOS_GPIO_MODE_OUTPUT) return Levels[Pin];
  if (Pin == mChargerBus.Scl) return !ClockStuck;
  assert(Pin == mChargerBus.Sda && BitIndex < BitCount);
  return Bits[BitIndex++];
}

BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN Level) { return FALSE; }
VOID EFIAPI DebugPrint(UINTN Level, CONST CHAR8 *Format, ...) { }

static EFI_STATUS EFIAPI Signal(EFI_EVENT Event)
{
  assert(Event == mUsbEvent);
  ++Signals;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI Timer(EFI_EVENT Event, EFI_TIMER_DELAY Type, UINT64 Trigger)
{
  assert(Event == mPollEvent && Type == TimerCancel && Trigger == 0);
  ++TimerCancels;
  return EFI_SUCCESS;
}

static EFI_STATUS BadGate(BOOLEAN Enable) { return EFI_DEVICE_ERROR; }

int main(void)
{
  UINT16 AdcValue;
  UINT8 Value;
  EXYNOS_GPIO_PROTOCOL Gpio = {Get, Set, Config};
  mGpio = &Gpio;
  mOwnWatchdog = FALSE;
  assert(Watchdog(TRUE) == EFI_SUCCESS);
  assert(mOwnWatchdog && Wdt[0] == WDT_CONTROL && Wdt[1] == WDT_RELOAD && Wdt[2] == WDT_RELOAD);
  Wdt[2] = 123;
  assert(Watchdog(TRUE) == EFI_SUCCESS && Wdt[2] == WDT_RELOAD);
  assert(Watchdog(FALSE) == EFI_SUCCESS && Wdt[0] == 0 && !mOwnWatchdog);
  Wdt[0] = WDT_CONTROL;
  Wdt[1] = Wdt[2] = 65535;
  WdtWrites = 0;
  assert(Watchdog(TRUE) == EFI_SUCCESS && !mOwnWatchdog && WdtWrites == 0);
  assert(Watchdog(FALSE) == EFI_SUCCESS && WdtWrites == 0);
  Wdt[0] = BIT5;
  assert(Watchdog(TRUE) == EFI_ACCESS_DENIED && WdtWrites == 0);
  Wdt[0] = 0;
  assert(Watchdog(TRUE) == EFI_SUCCESS && mOwnWatchdog);
  Wdt[1] = Wdt[2] = 65535;
  WdtWrites = 0;
  assert(Watchdog(FALSE) == EFI_SUCCESS && !mOwnWatchdog && WdtWrites == 0);

  AdcResult = 989;
  assert(ReadAdc(&AdcValue) == EFI_SUCCESS && AdcValue == 989 && AdcSamples == 7);
  assert((AdcControl & (BIT16 | BIT14)) == (BIT16 | BIT14));
  AdcStuck = TRUE;
  DelayCalls = 0;
  assert(ReadAdc(&AdcValue) == EFI_TIMEOUT && DelayCalls == 1000);

  Bits[0] = 0;
  BitCount = 1;
  Value = 0xA5;
  assert(Byte(&mChargerBus, &Value, FALSE, FALSE) == EFI_SUCCESS);
  assert(BitIndex == 1 && Edges == 9);
  for (UINTN Index = 0; Index < 8; ++Index)
    assert(Driven[Index] == ((0xA5 & (0x80 >> Index)) ? EXYNOS_GPIO_MODE_INPUT : EXYNOS_GPIO_MODE_OUTPUT));
  assert(Driven[8] == EXYNOS_GPIO_MODE_INPUT);
  BitIndex = Edges = 0;
  Bits[0] = 1;
  assert(Byte(&mChargerBus, &Value, FALSE, FALSE) == EFI_NOT_FOUND);
  BitIndex = Edges = 0;
  BitCount = 9;
  for (UINTN Index = 0; Index < 8; ++Index) Bits[Index] = !!(0x5A & (0x80 >> Index));
  Bits[8] = 1;
  assert(Byte(&mChargerBus, &Value, TRUE, TRUE) == EFI_SUCCESS && Value == 0x5A);
  assert(Driven[8] == EXYNOS_GPIO_MODE_INPUT && BitIndex == 9);
  BitIndex = Edges = 0;
  BitCount = 8;
  assert(Byte(&mChargerBus, &Value, TRUE, FALSE) == EFI_SUCCESS && Value == 0x5A);
  assert(Driven[8] == EXYNOS_GPIO_MODE_OUTPUT && BitIndex == 8);
  BitIndex = Edges = 0;
  BitCount = 2;
  Bits[0] = Bits[1] = 1;
  assert(SoftwareTransfer(CHARGER_SMB136, 0x31, &Value, 1, TRUE) == EFI_NOT_FOUND);
  assert(Modes[mChargerBus.Sda] == EXYNOS_GPIO_MODE_INPUT && Modes[mChargerBus.Scl] == EXYNOS_GPIO_MODE_INPUT);
  ClockStuck = TRUE;
  DelayCalls = 0;
  assert(ClockHigh(&mChargerBus) == EFI_TIMEOUT && DelayCalls == 1001);
  assert(Gate(FALSE) == EFI_SUCCESS && Levels[CHG_ENABLE] == 1);
  assert(Gate(TRUE) == EFI_SUCCESS && Levels[CHG_ENABLE] == 0);
  EFI_BOOT_SERVICES Services = {.SignalEvent = Signal, .SetTimer = Timer};
  gBS = &Services;
  mUsbEvent = (EFI_EVENT)1;
  mPollEvent = (EFI_EVENT)2;
  memset(&mCharger, 0, sizeof(mCharger));
  mCharger.Io.Gate = Gate;
  mCharger.Io.Watchdog = Watchdog;
  mCharger.State.ChipAddress = CHARGER_SMB136;
  mCharger.State.UsbType = 1;
  mCharger.State.Enabled = TRUE;
  UsbConfigured(&mProtocol, TRUE);
  assert(Signals == 0 && mCharger.UsbConfigured);
  UsbConfigured(&mProtocol, FALSE);
  assert(Signals == 1 && !mCharger.UsbConfigured);
  UsbChanged(mUsbEvent, NULL);
  assert(!mCharger.State.Enabled && Levels[CHG_ENABLE] == 1);
  Wdt[0] = 0;
  assert(Watchdog(TRUE) == EFI_SUCCESS);
  mCharger.State.Enabled = TRUE;
  Exit(NULL, NULL);
  assert(mCharger.Stopped && !mCharger.State.Enabled && Wdt[0] == 0 && TimerCancels == 1);
  Wdt[0] = WDT_CONTROL;
  Wdt[1] = Wdt[2] = 65535;
  WdtWrites = 0;
  Exit(NULL, NULL);
  assert(WdtWrites == 0 && Wdt[0] == WDT_CONTROL && TimerCancels == 2);
  Wdt[0] = 0;
  assert(Watchdog(TRUE) == EFI_SUCCESS);
  mCharger.State.Enabled = TRUE;
  mCharger.Io.Gate = BadGate;
  Exit(NULL, NULL);
  assert(Wdt[0] == WDT_CONTROL && mOwnWatchdog && mCharger.State.Enabled);
  puts("Charger USB revocation and ExitBootServices shutdown: PASS");
  puts("Charger GPIO open-drain I2C, ADC timeout, and watchdog ownership: PASS");
  return 0;
}
