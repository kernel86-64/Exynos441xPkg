/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef EXYNOS_CHARGER_PROTOCOL_H_
#define EXYNOS_CHARGER_PROTOCOL_H_

#define EXYNOS_CHARGER_PROTOCOL_GUID \
  { 0xa8e54f12, 0x7e6b, 0x4be0, { 0x98, 0xb4, 0x14, 0x48, 0x70, 0x00, 0x00, 0x01 } }

typedef struct {
  UINT32      Millivolts;
  UINT16      ThermistorAdc;
  UINT8       Percent;
  UINT8       ChipAddress;
  UINT8       UsbType;
  UINT8       Status;
  BOOLEAN     Enabled;
  BOOLEAN     Charging;
  EFI_STATUS  Error;
} EXYNOS_CHARGER_STATE;

typedef struct _EXYNOS_CHARGER_PROTOCOL EXYNOS_CHARGER_PROTOCOL;
struct _EXYNOS_CHARGER_PROTOCOL {
  EFI_STATUS (EFIAPI *GetState)(EXYNOS_CHARGER_PROTOCOL *This, EXYNOS_CHARGER_STATE *State);
  VOID (EFIAPI *UsbConfigured)(EXYNOS_CHARGER_PROTOCOL *This, BOOLEAN Configured);
};

extern EFI_GUID gExynosChargerProtocolGuid;
#endif
