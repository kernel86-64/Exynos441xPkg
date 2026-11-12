/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef EXYNOS_CHARGER_H_
#define EXYNOS_CHARGER_H_

#include <Uefi.h>
#include <Protocol/ExynosCharger.h>

#define CHARGER_SMB136  0x4D
#define CHARGER_SMB328  0x34

typedef struct {
  EFI_STATUS (*Read)(UINT8 Device, UINT8 Register, UINT8 *Data, UINTN Length);
  EFI_STATUS (*Write)(UINT8 Device, UINT8 Register, UINT8 Data);
  EFI_STATUS (*Gate)(BOOLEAN Enable);
  EFI_STATUS (*Adc)(UINT16 *Value);
  EFI_STATUS (*Watchdog)(BOOLEAN Enable);
} CHARGER_IO;

typedef struct {
  CHARGER_IO            Io;
  EXYNOS_CHARGER_STATE   State;
  BOOLEAN               UsbConfigured;
  BOOLEAN               Full;
  BOOLEAN               Stopped;
  BOOLEAN               Fault;
  UINT32                ChargeSeconds;
} CHARGER;

EFI_STATUS ChargerDetect(CHARGER *Charger);
EFI_STATUS ChargerStop(CHARGER *Charger);
VOID ChargerPoll(CHARGER *Charger);

#endif
