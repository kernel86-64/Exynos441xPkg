/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Charger.h"
#include <Protocol/ExynosGpio.h>
#include <Protocol/ExynosI2c.h>
#include <Protocol/ExynosClock.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define ADC_BASE  0x13910000
#define WDT_BASE  0x10060000
#define WDT_CONTROL  0xFF39
#define WDT_RELOAD   32767
#define CHG_ENABLE  EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPL2, 2)
#define OTG_ENABLE  EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPX3, 3)

typedef struct {
  UINTN Sda;
  UINTN Scl;
} GPIO_I2C;

STATIC GPIO_I2C mChargerBus = {
  EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPY0, 4), EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPY0, 5)
};
STATIC GPIO_I2C mGaugeBus = {
  EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPY4, 0), EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPY4, 1)
};
STATIC EXYNOS_GPIO_PROTOCOL *mGpio;
STATIC EXYNOS_I2C_PROTOCOL *mI2c;
STATIC CHARGER mCharger;
STATIC EFI_EVENT mPollEvent;
STATIC EFI_EVENT mExitEvent;
STATIC EFI_EVENT mUsbEvent;
STATIC BOOLEAN mOwnWatchdog;
STATIC UINT32 mLogTicks;

STATIC VOID
Line(UINTN Pin, BOOLEAN Release)
{
  if (!Release) {
    mGpio->Set(mGpio, Pin, 0);
  }
  mGpio->Config(mGpio, Pin, Release ? EXYNOS_GPIO_MODE_INPUT : EXYNOS_GPIO_MODE_OUTPUT,
                EXYNOS_GPIO_PULL_UP);
  MicroSecondDelay(5);
}

STATIC EFI_STATUS
ClockHigh(GPIO_I2C *Bus)
{
  UINTN Retry;

  Line(Bus->Scl, TRUE);
  for (Retry = 0; Retry < 1000; ++Retry) {
    if (mGpio->Get(mGpio, Bus->Scl)) {
      return EFI_SUCCESS;
    }
    MicroSecondDelay(1);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS
Start(GPIO_I2C *Bus)
{
  EFI_STATUS Status;

  Line(Bus->Sda, TRUE);
  Status = ClockHigh(Bus);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (!mGpio->Get(mGpio, Bus->Sda)) {
    return EFI_DEVICE_ERROR;
  }
  Line(Bus->Sda, FALSE);
  Line(Bus->Scl, FALSE);
  return EFI_SUCCESS;
}

STATIC VOID
Stop(GPIO_I2C *Bus)
{
  Line(Bus->Scl, FALSE);
  Line(Bus->Sda, FALSE);
  ClockHigh(Bus);
  Line(Bus->Sda, TRUE);
  Line(Bus->Scl, TRUE);
}

STATIC EFI_STATUS
Byte(GPIO_I2C *Bus, UINT8 *Value, BOOLEAN Receive, BOOLEAN Last)
{
  UINTN Bit;
  BOOLEAN Ack;
  EFI_STATUS Status;
  UINT8 Data;

  Data = Receive ? 0 : *Value;
  for (Bit = 0; Bit < 8; ++Bit) {
    Line(Bus->Sda, Receive || (Data & 0x80) != 0);
    Status = ClockHigh(Bus);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    Data = (UINT8)(Data << 1);
    if (Receive) {
      Data |= (UINT8)mGpio->Get(mGpio, Bus->Sda);
    }
    Line(Bus->Scl, FALSE);
  }
  Line(Bus->Sda, Receive ? Last : TRUE);
  Status = ClockHigh(Bus);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Ack = !mGpio->Get(mGpio, Bus->Sda);
  Line(Bus->Scl, FALSE);
  Line(Bus->Sda, TRUE);
  if (Receive) {
    *Value = Data;
  }
  return Receive || Ack ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC EFI_STATUS
SoftwareTransfer(UINT8 Device, UINT8 Register, UINT8 *Data, UINTN Length, BOOLEAN Read)
{
  GPIO_I2C *Bus;
  EFI_STATUS Status;
  UINT8 Address;
  UINTN Index;

  Bus = Device == 0x36 ? &mGaugeBus : &mChargerBus;
  Status = Start(Bus);
  Address = (UINT8)(Device << 1);
  if (!EFI_ERROR(Status)) {
    Status = Byte(Bus, &Address, FALSE, FALSE);
  }
  if (!EFI_ERROR(Status)) {
    Status = Byte(Bus, &Register, FALSE, FALSE);
  }
  if (!EFI_ERROR(Status) && Read) {
    Status = Start(Bus);
    Address = (UINT8)((Device << 1) | 1);
    if (!EFI_ERROR(Status)) {
      Status = Byte(Bus, &Address, FALSE, FALSE);
    }
  }
  for (Index = 0; Index < Length && !EFI_ERROR(Status); ++Index) {
    Status = Byte(Bus, &Data[Index], Read, Index + 1 == Length);
  }
  Stop(Bus);
  return Status;
}

STATIC EFI_STATUS
ReadRegister(UINT8 Device, UINT8 Register, UINT8 *Data, UINTN Length)
{
  if (Device == 0x66 || Device == 0x25) {
    return mI2c->Read(mI2c, 5, Device, Register, Data, Length) == (INTN)Length ?
           EFI_SUCCESS : EFI_DEVICE_ERROR;
  }
  return SoftwareTransfer(Device, Register, Data, Length, TRUE);
}

STATIC EFI_STATUS
WriteRegister(UINT8 Device, UINT8 Register, UINT8 Data)
{
  UINT8 Buffer[2];

  if (Device == 0x66 || Device == 0x25) {
    Buffer[0] = Register;
    Buffer[1] = Data;
    return mI2c->Write(mI2c, 5, Device, Buffer, 2) == 2 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  }
  return SoftwareTransfer(Device, Register, &Data, 1, FALSE);
}

STATIC EFI_STATUS
Gate(BOOLEAN Enable)
{
  EFI_STATUS Status;

  Status = mGpio->Set(mGpio, CHG_ENABLE, Enable ? 0 : 1);
  if (!EFI_ERROR(Status)) {
    Status = mGpio->Config(mGpio, CHG_ENABLE, EXYNOS_GPIO_MODE_OUTPUT, EXYNOS_GPIO_PULL_NONE);
  }
  if (!EFI_ERROR(Status) && mGpio->Get(mGpio, CHG_ENABLE) != (Enable ? 0U : 1U)) {
    Status = EFI_DEVICE_ERROR;
  }
  return Status;
}

STATIC EFI_STATUS
ReadAdc(UINT16 *Value)
{
  UINTN Sample;
  UINTN Retry;
  UINT32 Sum;
  UINT32 Data;
  UINT32 Minimum;
  UINT32 Maximum;

  Sum = 0;
  Minimum = 0xFFF;
  Maximum = 0;
  for (Sample = 0; Sample < 7; ++Sample) {
    MmioWrite32(ADC_BASE + 0x1C, 7);
    MmioWrite32(ADC_BASE, BIT16 | BIT14 | (49 << 6));
    MmioWrite32(ADC_BASE, BIT16 | BIT14 | (49 << 6) | BIT0);
    for (Retry = 0; Retry < 1000; ++Retry) {
      if ((MmioRead32(ADC_BASE) & BIT15) != 0) {
        break;
      }
      MicroSecondDelay(1);
    }
    if (Retry == 1000) {
      return EFI_TIMEOUT;
    }
    Data = MmioRead32(ADC_BASE + 0x0C) & 0xFFF;
    MmioWrite32(ADC_BASE + 0x18, 0);
    Sum += Data;
    Minimum = MIN(Minimum, Data);
    Maximum = MAX(Maximum, Data);
  }
  *Value = (UINT16)((Sum - Minimum - Maximum) / 5);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Watchdog(BOOLEAN Enable)
{
  UINT32 Control;

  Control = MmioRead32(WDT_BASE);
  if (mOwnWatchdog && (Control != WDT_CONTROL || MmioRead32(WDT_BASE + 4) != WDT_RELOAD)) {
    mOwnWatchdog = FALSE;
  }
  if (Enable) {
    if ((Control & BIT5) != 0 && !mOwnWatchdog) {
      return (Control & BIT0) != 0 ? EFI_SUCCESS : EFI_ACCESS_DENIED;
    }
    if (!mOwnWatchdog) {
      MmioWrite32(WDT_BASE, 0);
      MmioWrite32(WDT_BASE + 4, WDT_RELOAD);
      mOwnWatchdog = TRUE;
    }
    MmioWrite32(WDT_BASE + 8, WDT_RELOAD);
    MmioWrite32(WDT_BASE, WDT_CONTROL);
  } else if (mOwnWatchdog) {
    MmioWrite32(WDT_BASE, 0);
    mOwnWatchdog = FALSE;
  }
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI
Poll(EFI_EVENT Event, VOID *Context)
{
  ChargerPoll(&mCharger);
  if ((mLogTicks++ % 30) == 0) {
    DEBUG((DEBUG_ERROR, "Charger: chip=%02x source=%u battery=%umV %u%% adc=%u enabled=%u active=%u status=%02x error=%r\n",
           mCharger.State.ChipAddress, mCharger.State.UsbType, mCharger.State.Millivolts,
           mCharger.State.Percent, mCharger.State.ThermistorAdc, mCharger.State.Enabled,
           mCharger.State.Charging, mCharger.State.Status, mCharger.State.Error));
  }
}

STATIC VOID EFIAPI
Exit(EFI_EVENT Event, VOID *Context)
{
  EFI_STATUS Status;

  gBS->SetTimer(mPollEvent, TimerCancel, 0);
  mCharger.Stopped = TRUE;
  Status = ChargerStop(&mCharger);
  DEBUG((DEBUG_ERROR, "Charger: ExitBootServices shutdown %r\n", Status));
}

STATIC EFI_STATUS EFIAPI
GetState(EXYNOS_CHARGER_PROTOCOL *This, EXYNOS_CHARGER_STATE *State)
{
  EFI_TPL OldTpl;

  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL(TPL_NOTIFY);
  *State = mCharger.State;
  gBS->RestoreTPL(OldTpl);
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI
UsbChanged(EFI_EVENT Event, VOID *Context)
{
  if (!mCharger.Stopped && !mCharger.UsbConfigured && mCharger.State.UsbType == 1) {
    mCharger.State.Error = ChargerStop(&mCharger);
  }
}

STATIC VOID EFIAPI
UsbConfigured(EXYNOS_CHARGER_PROTOCOL *This, BOOLEAN Configured)
{
  mCharger.UsbConfigured = Configured;
  if (mUsbEvent != NULL && !Configured) {
    gBS->SignalEvent(mUsbEvent);
  }
}

STATIC EXYNOS_CHARGER_PROTOCOL mProtocol = {GetState, UsbConfigured};

EFI_STATUS EFIAPI
ChargerDxeInitialize(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EXYNOS_CLOCK_PROTOCOL *Clock;
  EFI_STATUS Status;

  Status = gBS->LocateProtocol(&gExynosGpioProtocolGuid, NULL, (VOID **)&mGpio);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = gBS->LocateProtocol(&gExynosI2cProtocolGuid, NULL, (VOID **)&mI2c);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = gBS->LocateProtocol(&gExynosClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  mCharger.Io.Read = ReadRegister;
  mCharger.Io.Write = WriteRegister;
  mCharger.Io.Gate = Gate;
  mCharger.Io.Adc = ReadAdc;
  mCharger.Io.Watchdog = Watchdog;
  mGpio->Config(mGpio, EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPB, 6), 3, EXYNOS_GPIO_PULL_UP);
  mGpio->Config(mGpio, EXYNOS_GPIO(EXYNOS_GPIO_BANK_GPB, 7), 3, EXYNOS_GPIO_PULL_UP);
  Status = ChargerDetect(&mCharger);
  mCharger.State.Error = Status;
  if (!EFI_ERROR(Status)) {
    mGpio->Set(mGpio, OTG_ENABLE, 0);
    mGpio->Config(mGpio, OTG_ENABLE, EXYNOS_GPIO_MODE_OUTPUT, EXYNOS_GPIO_PULL_NONE);
    Status = Clock->Enable(Clock, ExynosClkGateAdc);
  }
  if (EFI_ERROR(Status)) {
    mCharger.Stopped = TRUE;
    mCharger.State.Error = Status;
    DEBUG((DEBUG_ERROR, "Charger: initialization %r; charging unavailable\n", Status));
  } else {
    Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Poll, NULL, &mPollEvent);
    if (!EFI_ERROR(Status)) {
      Status = gBS->CreateEvent(EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_NOTIFY, Exit, NULL, &mExitEvent);
    }
    if (!EFI_ERROR(Status)) {
      Status = gBS->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_NOTIFY, UsbChanged, NULL, &mUsbEvent);
    }
    if (!EFI_ERROR(Status)) {
      Status = gBS->SetTimer(mPollEvent, TimerPeriodic, 10000000);
    }
    if (EFI_ERROR(Status)) {
      mCharger.Stopped = TRUE;
      mCharger.State.Error = Status;
      ChargerStop(&mCharger);
      if (mPollEvent != NULL) {
        gBS->CloseEvent(mPollEvent);
      }
      if (mExitEvent != NULL) {
        gBS->CloseEvent(mExitEvent);
      }
      if (mUsbEvent != NULL) {
        gBS->CloseEvent(mUsbEvent);
        mUsbEvent = NULL;
      }
    }
  }
  Status = gBS->InstallMultipleProtocolInterfaces(&ImageHandle, &gExynosChargerProtocolGuid, &mProtocol, NULL);
  if (EFI_ERROR(Status) && !mCharger.Stopped) {
    mCharger.Stopped = TRUE;
    gBS->SetTimer(mPollEvent, TimerCancel, 0);
    ChargerStop(&mCharger);
    gBS->CloseEvent(mPollEvent);
    gBS->CloseEvent(mExitEvent);
    gBS->CloseEvent(mUsbEvent);
    mUsbEvent = NULL;
  }
  return Status;
}
