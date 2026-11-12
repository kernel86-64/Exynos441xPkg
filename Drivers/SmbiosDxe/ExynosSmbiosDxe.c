/** @file
  Publish SMBIOS 3.x system and processor identities for the Exynos4412.

  The firmware already identifies the SoC in the ACPI OEM fields.  This
  driver exposes the same platform identity through SMBIOS Type 1 and Type 4
  so the ARM32 kernel can use the marketing name instead of guessing it from
  MIDR alone.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <IndustryStandard/SmBios.h>
#include <Guid/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#define EXYNOS_SMBIOS_TABLE_SIZE  4096

STATIC
UINT8 *
SmbiosAppendString (
  IN UINT8        *Cursor,
  IN CONST CHAR8  *String
  )
{
  UINTN  Length;

  Length = AsciiStrLen (String) + 1;
  CopyMem (Cursor, String, Length);
  return Cursor + Length;
}

STATIC
UINT8 *
SmbiosEndStrings (
  IN UINT8  *Cursor
  )
{
  *Cursor++ = 0;
  return Cursor;
}

STATIC
UINT8
SmbiosChecksum (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT8  Sum;
  UINTN  Index;

  Sum = 0;
  for (Index = 0; Index < Length; Index++) {
    Sum = (UINT8)(Sum + Data[Index]);
  }
  return (UINT8)(0 - Sum);
}

STATIC
EFI_STATUS
BuildSmbiosTable (
  OUT VOID    **Table,
  OUT UINT32  *TableSize
  )
{
  UINT8                  *Cursor;
  SMBIOS_TABLE_TYPE1     *Type1;
  SMBIOS_TABLE_TYPE4     *Type4;
  SMBIOS_TABLE_TYPE127   *Type127;

  Cursor = AllocateRuntimeZeroPool (EXYNOS_SMBIOS_TABLE_SIZE);
  if (Cursor == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Type1 = (SMBIOS_TABLE_TYPE1 *)Cursor;
  Type1->Hdr.Type = SMBIOS_TYPE_SYSTEM_INFORMATION;
  Type1->Hdr.Length = sizeof (SMBIOS_TABLE_TYPE1);
  Type1->Hdr.Handle = 0x0001;
  Type1->Manufacturer = 1;
  Type1->ProductName = 2;
  Type1->Version = 3;
  Type1->SerialNumber = 4;
  Type1->WakeUpType = SystemWakeupTypePowerSwitch;
  Type1->SKUNumber = 5;
  Type1->Family = 6;
  Cursor += sizeof (SMBIOS_TABLE_TYPE1);
  Cursor = SmbiosAppendString (Cursor, "Samsung Electronics");
  Cursor = SmbiosAppendString (Cursor, "GT-N7000");
  Cursor = SmbiosAppendString (Cursor, "1.0");
  Cursor = SmbiosAppendString (Cursor, "Not Specified");
  Cursor = SmbiosAppendString (Cursor, "GT-N7000");
  Cursor = SmbiosAppendString (Cursor, "Galaxy Note");
  Cursor = SmbiosEndStrings (Cursor);

  Type4 = (SMBIOS_TABLE_TYPE4 *)Cursor;
  Type4->Hdr.Type = SMBIOS_TYPE_PROCESSOR_INFORMATION;
  Type4->Hdr.Length = sizeof (SMBIOS_TABLE_TYPE4);
  Type4->Hdr.Handle = 0x0002;
  Type4->Socket = 1;
  Type4->ProcessorType = CentralProcessor;
  Type4->ProcessorFamily = ProcessorFamilyIndicatorFamily2;
  Type4->ProcessorManufacturer = 2;
  Type4->ProcessorVersion = 3;
  Type4->MaxSpeed = 1400;
  Type4->CurrentSpeed = 1400;
  Type4->Status = 0x41;
  Type4->ProcessorUpgrade = ProcessorUpgradeUnknown;
  Type4->L1CacheHandle = 0xFFFF;
  Type4->L2CacheHandle = 0xFFFF;
  Type4->L3CacheHandle = 0xFFFF;
  Type4->SerialNumber = 4;
  Type4->AssetTag = 5;
  Type4->PartNumber = 6;
  Type4->CoreCount = 2;
  Type4->EnabledCoreCount = 2;
  Type4->ThreadCount = 2;
  Type4->ProcessorCharacteristics = 0x0008;
  Type4->ProcessorFamily2 = ProcessorFamilyARM;
  Type4->CoreCount2 = 2;
  Type4->EnabledCoreCount2 = 2;
  Type4->ThreadCount2 = 2;
  Type4->ThreadEnabled = 2;
  Type4->SocketType = 1;
  Cursor += sizeof (SMBIOS_TABLE_TYPE4);
  Cursor = SmbiosAppendString (Cursor, "CPU0");
  Cursor = SmbiosAppendString (Cursor, "Samsung");
  Cursor = SmbiosAppendString (Cursor, "Samsung Exynos 4210");
  Cursor = SmbiosAppendString (Cursor, "Not Specified");
  Cursor = SmbiosAppendString (Cursor, "Not Specified");
  Cursor = SmbiosAppendString (Cursor, "Exynos 4210");
  Cursor = SmbiosEndStrings (Cursor);

  Type127 = (SMBIOS_TABLE_TYPE127 *)Cursor;
  Type127->Hdr.Type = SMBIOS_TYPE_END_OF_TABLE;
  Type127->Hdr.Length = sizeof (SMBIOS_TABLE_TYPE127);
  Type127->Hdr.Handle = 0x0003;
  Cursor += sizeof (SMBIOS_TABLE_TYPE127);
  Cursor = SmbiosEndStrings (Cursor);
  Cursor = SmbiosEndStrings (Cursor);

  *Table = (VOID *)(UINTN)Type1;
  *TableSize = (UINT32)(Cursor - (UINT8 *)Type1);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ExynosSmbiosDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  SMBIOS_TABLE_3_0_ENTRY_POINT  *EntryPoint;
  VOID                          *Table;
  UINT32                        TableSize;

  Status = BuildSmbiosTable (&Table, &TableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ExynosSmbiosDxe: table allocation failed: %r\n", Status));
    return Status;
  }

  EntryPoint = AllocateRuntimeZeroPool (sizeof (*EntryPoint));
  if (EntryPoint == NULL) {
    FreePool (Table);
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (EntryPoint->AnchorString, SMBIOS_3_0_ANCHOR_STRING,
           SMBIOS_3_0_ANCHOR_STRING_LENGTH);
  EntryPoint->EntryPointLength = sizeof (*EntryPoint);
  EntryPoint->MajorVersion = 3;
  EntryPoint->MinorVersion = 8;
  EntryPoint->DocRev = 0;
  EntryPoint->EntryPointRevision = 1;
  EntryPoint->TableMaximumSize = TableSize;
  EntryPoint->TableAddress = (UINT64)(UINTN)Table;
  EntryPoint->EntryPointStructureChecksum =
    SmbiosChecksum ((UINT8 *)EntryPoint, sizeof (*EntryPoint));

  Status = gBS->InstallConfigurationTable (&gEfiSmbios3TableGuid, EntryPoint);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "ExynosSmbiosDxe: configuration table failed: %r\n", Status));
    FreePool (EntryPoint);
    FreePool (Table);
    return Status;
  }

  DEBUG ((DEBUG_INFO, "ExynosSmbiosDxe: installed %u-byte SMBIOS 3.8 table\n", TableSize));
  return EFI_SUCCESS;
}
