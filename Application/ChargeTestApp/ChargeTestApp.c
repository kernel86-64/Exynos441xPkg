/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <Protocol/ExynosCharger.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

EFI_STATUS EFIAPI
UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EXYNOS_CHARGER_PROTOCOL *Charger;
  EXYNOS_CHARGER_STATE State;
  EFI_STATUS Status;
  UINTN Index;

  Status = gBS->LocateProtocol(&gExynosChargerProtocolGuid, NULL, (VOID **)&Charger);
  if (EFI_ERROR(Status)) {
    Print(L"N7000 charger protocol: %r\n", Status);
    return Status;
  }
  Print(L"N7000 UEFI charging monitor (30 seconds)\n");
  Print(L"enabled = requested; active = chip reports charging\n");
  for (Index = 0; Index < 15; ++Index) {
    Status = Charger->GetState(Charger, &State);
    if (EFI_ERROR(Status)) {
      return Status;
    }
    Print(L"%02u: chip %02x USB %u %umV %u%% ADC %u\n  enabled %u active %u status %02x %r\n",
          (UINT32)(Index * 2), State.ChipAddress, State.UsbType, State.Millivolts,
          State.Percent, State.ThermistorAdc, State.Enabled, State.Charging, State.Status, State.Error);
    gBS->Stall(2000000);
  }
  return EFI_SUCCESS;
}
