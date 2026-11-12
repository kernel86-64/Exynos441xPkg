/** @file
  Boot-critical USB and microSD devices for the GT-N7000.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

DefinitionBlock ("DSDT.aml", "DSDT", 2, "SAMSNG", "EXY4210 ", 1)
{
  Scope (\_SB)
  {
    //
    // DWC2 USB device controller (peripheral mode only), KDNET debug port.
    //
    Device (UFN0)
    {
      Name (_HID, "EXYN0001")
      Name (_UID, 0)
      Method (_STA) { Return (0xF) }
      Name (_CRS, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x12480000, 0x00020000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 103 }   // SPI 71
      })
    }

#ifdef SMDKC210_QEMU
    Device (EHC0)
    {
      Name (_HID, "PNP0D20")
      Name (_UID, 0)
      Name (_CCA, 1)
      Method (_STA) { Return (0xF) }
      Name (_CRS, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x12580000, 0x00001000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 102 }
      })
    }
#endif

    Device (SDC2)
    {
      Name (_HID, "EXYN0006")
      Name (_UID, 2)
      Method (_STA) { Return (0xF) }
      Name (_CRS, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x12530000, 0x00000100)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 107 }
      })
    }
  }
}
