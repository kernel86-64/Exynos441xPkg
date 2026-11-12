/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Drivers/ChargerDxe/Charger.c"

static UINT8 Registers[128][256];
static BOOLEAN Present[128];
static UINT16 Adc;
static BOOLEAN AdcFail, GateOn, WatchOn, Corrupt;
static UINTN Operations, FailAt;
static CHARGER C;

static EFI_STATUS Read(UINT8 Dev, UINT8 Reg, UINT8 *Data, UINTN Len)
{
  if (++Operations == FailAt || !Present[Dev]) return EFI_DEVICE_ERROR;
  if (Dev == 0x36 && Reg == 0xFF && Len == 2) {
    Data[0] = 0;
    Data[1] = Registers[Dev][Reg];
  } else {
    memcpy(Data, &Registers[Dev][Reg], Len);
  }
  if (Corrupt && Dev == C.State.ChipAddress && Reg == 2) Data[0] ^= 1;
  return EFI_SUCCESS;
}

static EFI_STATUS Write(UINT8 Dev, UINT8 Reg, UINT8 Data)
{
  if (++Operations == FailAt || !Present[Dev]) return EFI_DEVICE_ERROR;
  Registers[Dev][Reg] = Data;
  return EFI_SUCCESS;
}

static EFI_STATUS Gate(BOOLEAN Enable)
{
  GateOn = Enable;
  return EFI_SUCCESS;
}

static EFI_STATUS Thermal(UINT16 *Value)
{
  *Value = Adc;
  return AdcFail ? EFI_TIMEOUT : EFI_SUCCESS;
}

static EFI_STATUS Watch(BOOLEAN Enable)
{
  WatchOn = Enable;
  return EFI_SUCCESS;
}

static void Voltage(UINT32 Millivolts)
{
  UINT16 Raw = (UINT16)(Millivolts * 64 / 5);
  Registers[0x36][9] = (UINT8)Raw;
  Registers[0x36][10] = (UINT8)(Raw >> 8);
}

static void Init(UINT8 Chip)
{
  memset(Registers, 0, sizeof(Registers));
  memset(Present, 0, sizeof(Present));
  memset(&C, 0, sizeof(C));
  C.Io = (CHARGER_IO){Read, Write, Gate, Thermal, Watch};
  Present[Chip] = Present[0x66] = Present[0x25] = Present[0x36] = TRUE;
  Registers[0x66][0x51] = 0xC0;
  Registers[0x25][5] = BIT6 | 3;
  Registers[0x36][0xFF] = 50;
  Registers[Chip][0x36] = Chip == CHARGER_SMB328 ? 3 : 0;
  Registers[Chip][0x39] = 1;
  Adc = 989;
  AdcFail = GateOn = WatchOn = Corrupt = FALSE;
  Operations = 0;
  FailAt = MAX_UINTN;
  Voltage(3800);
  assert(ChargerDetect(&C) == EFI_SUCCESS);
  assert(Registers[0x66][0x51] == 0 && !GateOn && !WatchOn);
}

static void Disabled(void)
{
  assert(!C.State.Enabled && !C.State.Charging && !WatchOn && !GateOn);
}

static void Test(UINT8 Chip)
{
  UINTN Failure;
  UINTN EnableOperations;
  Init(Chip);
  Operations = 0;
  ChargerPoll(&C);
  EnableOperations = Operations;
  assert(C.State.Enabled && C.State.Charging && WatchOn);
  assert(C.State.Millivolts == 3800 && C.State.Percent == 50);
  assert(Registers[Chip][2] == (Chip == CHARGER_SMB136 ? 0x4A : 0xCA));
  assert(Registers[Chip][0x31] == 0x88);
  Voltage(4200);
  ChargerPoll(&C);
  Disabled();
  Voltage(4150);
  ChargerPoll(&C);
  Disabled();
  Voltage(4100);
  ChargerPoll(&C);
  assert(C.State.Enabled);
  Adc = 629;
  ChargerPoll(&C);
  Disabled();
  Adc = 1558;
  ChargerPoll(&C);
  Disabled();
  Adc = 1557;
  ChargerPoll(&C);
  assert(C.State.Enabled);
  AdcFail = TRUE;
  ChargerPoll(&C);
  Disabled();
  assert(C.State.Error == EFI_TIMEOUT);
  AdcFail = FALSE;
  Registers[0x25][5] = BIT6 | 1;
  ChargerPoll(&C);
  Disabled();
  C.UsbConfigured = TRUE;
  ChargerPoll(&C);
  assert(C.State.Enabled);
  C.UsbConfigured = FALSE;
  ChargerPoll(&C);
  Disabled();
  for (UINT8 Source = 0; Source <= 7; ++Source) {
    Registers[0x25][5] = BIT6 | Source;
    ChargerPoll(&C);
    assert(C.State.Enabled == (Source >= 2 && Source <= 5));
  }
  Registers[0x25][5] = BIT6 | BIT3 | 3;
  ChargerPoll(&C);
  Disabled();
  Registers[0x25][5] = BIT6 | 3;
  ChargerPoll(&C);
  Registers[0x66][0x10] = BIT2;
  ChargerPoll(&C);
  Disabled();
  Registers[0x66][0x10] = 0;
  ChargerPoll(&C);
  Registers[0x25][5] = 0;
  ChargerPoll(&C);
  Disabled();

  for (Failure = 1; Failure <= EnableOperations; ++Failure) {
    Init(Chip);
    Operations = 0;
    FailAt = Failure;
    ChargerPoll(&C);
    Disabled();
    assert(EFI_ERROR(C.State.Error));
  }
  Init(Chip);
  Corrupt = TRUE;
  ChargerPoll(&C);
  Disabled();
  assert(C.Fault && EFI_ERROR(C.State.Error));
  Corrupt = FALSE;
  ChargerPoll(&C);
  Disabled();

  Init(Chip);
  Registers[Chip][0x36] = Chip == CHARGER_SMB328 ? 0x23 : BIT3;
  ChargerPoll(&C);
  Disabled();
  assert(C.Fault);
  Init(Chip);
  Registers[Chip][0x36] = 0xC0;
  ChargerPoll(&C);
  Disabled();
  assert(C.Full);
  Init(Chip);
  C.ChargeSeconds = 4 * 60 * 60;
  ChargerPoll(&C);
  Disabled();
  Init(Chip);
  Registers[0x36][0xFF] = 101;
  ChargerPoll(&C);
  Disabled();
  Init(Chip);
  Voltage(2999);
  ChargerPoll(&C);
  Disabled();
  assert(C.State.Error == EFI_NOT_READY);
  Voltage(3000);
  ChargerPoll(&C);
  assert(C.State.Enabled);
  Init(Chip);
  Voltage(0);
  ChargerPoll(&C);
  Disabled();
  Init(Chip);
  ChargerPoll(&C);
  assert(ChargerStop(&C) == EFI_SUCCESS);
  C.Stopped = TRUE;
  Operations = 0;
  ChargerPoll(&C);
  Disabled();
  assert(Operations == 0);
}

int main(void)
{
  Test(CHARGER_SMB136);
  Test(CHARGER_SMB328);
  Init(CHARGER_SMB136);
  Present[CHARGER_SMB328] = TRUE;
  assert(ChargerDetect(&C) == EFI_UNSUPPORTED);
  assert(!GateOn && Registers[CHARGER_SMB328][0x31] == 0x98);
  Present[CHARGER_SMB136] = Present[CHARGER_SMB328] = FALSE;
  assert(ChargerDetect(&C) == EFI_UNSUPPORTED);
  puts("Charger policy, register readback, and every enable I2C failure: PASS");
  return 0;
}
