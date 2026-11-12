/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "Charger.h"

STATIC EFI_STATUS
WriteChecked(CHARGER *C, UINT8 Reg, UINT8 Value)
{
  EFI_STATUS Status;
  UINT8 Readback;

  Status = C->Io.Write(C->State.ChipAddress, Reg, Value);
  if (!EFI_ERROR(Status)) {
    Status = C->Io.Read(C->State.ChipAddress, Reg, &Readback, 1);
    if (!EFI_ERROR(Status) && Readback != Value) {
      Status = EFI_DEVICE_ERROR;
    }
  }
  return Status;
}

EFI_STATUS
ChargerStop(CHARGER *C)
{
  EFI_STATUS Status;

  if (C->State.ChipAddress == CHARGER_SMB136) {
    Status = C->Io.Gate(FALSE);
  } else if (C->State.ChipAddress == CHARGER_SMB328) {
    Status = WriteChecked(C, 0x31, 0x98);
  } else {
    Status = EFI_UNSUPPORTED;
  }
  if (!EFI_ERROR(Status)) {
    C->State.Enabled = FALSE;
    C->State.Charging = FALSE;
    Status = C->Io.Watchdog(FALSE);
  }
  return Status;
}

EFI_STATUS
ChargerDetect(CHARGER *C)
{
  UINT8 Value;
  BOOLEAN Smb136;
  BOOLEAN Smb328;
  EFI_STATUS Status;

  Smb136 = !EFI_ERROR(C->Io.Read(CHARGER_SMB136, 0x31, &Value, 1));
  Smb328 = !EFI_ERROR(C->Io.Read(CHARGER_SMB328, 0x31, &Value, 1));
  if (Smb136 == Smb328) {
    if (Smb136) {
      C->Io.Gate(FALSE);
      C->Io.Write(CHARGER_SMB328, 0x31, 0x98);
    }
    return EFI_UNSUPPORTED;
  }
  C->State.ChipAddress = Smb136 ? CHARGER_SMB136 : CHARGER_SMB328;
  Status = ChargerStop(C);
  if (!EFI_ERROR(Status)) {
    Status = C->Io.Read(0x66, 0x51, &Value, 1);
  }
  if (!EFI_ERROR(Status)) {
    Status = C->Io.Write(0x66, 0x51, Value & ~0xC0);
  }
  if (!EFI_ERROR(Status)) {
    Status = C->Io.Read(0x66, 0x51, &Value, 1);
    if (!EFI_ERROR(Status) && (Value & 0xC0) != 0) {
      Status = EFI_DEVICE_ERROR;
    }
  }
  return Status;
}

STATIC EFI_STATUS
EnableCharging(CHARGER *C)
{
  STATIC CONST UINT8 Smb136[][2] = {
    {0x31, 0x88}, {0x05, 0x08}, {0x00, 0x14}, {0x01, 0x60},
    {0x02, 0x4A}, {0x03, 0x8C}, {0x04, 0x28}, {0x06, 0x28}, {0x09, 0x4B}
  };
  STATIC CONST UINT8 Smb328[][2] = {
    {0x31, 0x98}, {0x00, 0x17}, {0x01, 0x14}, {0x02, 0xCA},
    {0x03, 0x9A}, {0x04, 0x4E}, {0x05, 0x00}, {0x06, 0xCD},
    {0x07, 0xF6}, {0x08, 0x00}
  };
  CONST UINT8 (*Profile)[2];
  UINTN Count;
  UINTN Index;
  EFI_STATUS Status;

  Status = C->Io.Watchdog(TRUE);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Profile = C->State.ChipAddress == CHARGER_SMB136 ? Smb136 : Smb328;
  Count = C->State.ChipAddress == CHARGER_SMB136 ? ARRAY_SIZE(Smb136) : ARRAY_SIZE(Smb328);
  for (Index = 0; Index < Count; ++Index) {
    Status = WriteChecked(C, Profile[Index][0], Profile[Index][1]);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }
  if (C->State.ChipAddress == CHARGER_SMB136) {
    Status = C->Io.Gate(TRUE);
  } else {
    Status = C->Io.Write(CHARGER_SMB328, 0x30, 0xAA);
    if (!EFI_ERROR(Status)) {
      Status = WriteChecked(C, 0x31, 0x88);
    }
  }
  if (!EFI_ERROR(Status)) {
    C->State.Enabled = TRUE;
  }
  return Status;
}

STATIC EFI_STATUS
ReadBattery(CHARGER *C, BOOLEAN *Allowed)
{
  UINT8 Presence;
  UINT8 Source;
  UINT8 Data[2];
  EFI_STATUS Status;
  UINT16 Voltage;

  *Allowed = FALSE;
  Status = C->Io.Read(0x66, 0x10, &Presence, 1);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Status = C->Io.Read(0x25, 0x05, &Source, 1);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  C->State.UsbType = Source & 7;
  if ((Presence & BIT2) != 0 || (Source & BIT6) == 0) {
    C->Full = FALSE;
    C->ChargeSeconds = 0;
    return EFI_SUCCESS;
  }
  Status = C->Io.Read(0x36, 0x09, Data, 2);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  Voltage = Data[0] | (Data[1] << 8);
  if (Voltage == 0 || Voltage == MAX_UINT16) {
    return EFI_DEVICE_ERROR;
  }
  C->State.Millivolts = ((UINT32)Voltage * 5) / 64;
  if (C->State.Millivolts < 3000) {
    return EFI_NOT_READY;
  }
  Status = C->Io.Read(0x36, 0xFF, Data, 2);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (Data[1] > 100) {
    return EFI_DEVICE_ERROR;
  }
  C->State.Percent = Data[1];
  Status = C->Io.Adc(&C->State.ThermistorAdc);
  if (EFI_ERROR(Status)) {
    return Status;
  }
  if (C->State.Millivolts >= 4200) {
    C->Full = TRUE;
  } else if (C->State.Millivolts < 4130) {
    C->Full = FALSE;
  }
  *Allowed = !C->Full && C->State.ThermistorAdc >= 630 &&
             C->State.ThermistorAdc <= 1557 && (Source & BIT3) == 0 &&
             ((C->State.UsbType == 1 && C->UsbConfigured) ||
              (C->State.UsbType >= 2 && C->State.UsbType <= 5));
  return EFI_SUCCESS;
}

VOID
ChargerPoll(CHARGER *C)
{
  EFI_STATUS Status;
  EFI_STATUS StopStatus;
  BOOLEAN Allowed;
  UINT8 Extra;

  if (C->Stopped) {
    return;
  }
  Status = ReadBattery(C, &Allowed);
  if (EFI_ERROR(Status) || !Allowed || C->Fault || C->ChargeSeconds >= 4 * 60 * 60) {
    StopStatus = ChargerStop(C);
    C->State.Error = EFI_ERROR(Status) ? Status : StopStatus;
    return;
  }
  if (!C->State.Enabled) {
    Status = EnableCharging(C);
  }
  if (!EFI_ERROR(Status)) {
    Status = C->Io.Read(C->State.ChipAddress, 0x36, &C->State.Status, 1);
  }
  if (!EFI_ERROR(Status)) {
    if (C->State.ChipAddress == CHARGER_SMB136) {
      Status = C->Io.Read(CHARGER_SMB136, 0x39, &Extra, 1);
      C->State.Charging = !EFI_ERROR(Status) && (Extra & BIT0) != 0;
      if (!EFI_ERROR(Status) && (C->State.Status & BIT3) != 0) {
        Status = EFI_DEVICE_ERROR;
      }
    } else if ((C->State.Status & 0x30) == 0x10 || (C->State.Status & 0x30) == 0x20) {
      Status = EFI_DEVICE_ERROR;
    } else {
      C->State.Charging = (C->State.Status & BIT0) != 0 &&
                          (C->State.Status & 0x30) == 0 && (C->State.Status & 0x06) != 0;
      Status = C->Io.Write(CHARGER_SMB328, 0x30, 0xAA);
    }
  }
  if (!EFI_ERROR(Status) && (C->State.Status & 0xC0) == 0xC0) {
    C->Full = TRUE;
    Status = ChargerStop(C);
  } else if (!EFI_ERROR(Status)) {
    Status = C->Io.Watchdog(TRUE);
    ++C->ChargeSeconds;
  }
  if (EFI_ERROR(Status)) {
    C->Fault = TRUE;
    ChargerStop(C);
  }
  C->State.Error = Status;
}
