/** @file
  Host checks of the production touch packet decoders and absolute pointer.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../Drivers/TouchDxe/TouchDxe.c"
#include "../Drivers/TouchDxe/MxtTouch.c"
#include "../Drivers/TouchDxe/WacomPen.c"

EFI_BOOT_SERVICES *gBS;
static EFI_TPL CurrentTpl;
static UINTN Signals;

static EFI_TPL EFIAPI Raise(EFI_TPL NewTpl)
{
  EFI_TPL Old = CurrentTpl;
  assert(NewTpl >= Old);
  CurrentTpl = NewTpl;
  return Old;
}

static VOID EFIAPI Restore(EFI_TPL OldTpl)
{
  assert(OldTpl <= CurrentTpl);
  CurrentTpl = OldTpl;
}

static EFI_STATUS EFIAPI Signal(EFI_EVENT Event)
{
  assert(Event == (EFI_EVENT)1);
  ++Signals;
  return EFI_SUCCESS;
}

VOID *EFIAPI CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length)
{
  return memcpy(Destination, Source, Length);
}

VOID *EFIAPI ZeroMem(VOID *Buffer, UINTN Length)
{
  return memset(Buffer, 0, Length);
}

static EFI_ABSOLUTE_POINTER_STATE State(VOID)
{
  EFI_ABSOLUTE_POINTER_STATE Value;
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_SUCCESS);
  assert(CurrentTpl == TPL_APPLICATION);
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_NOT_READY);
  return Value;
}

int main(void)
{
  EFI_BOOT_SERVICES Services = {0};
  EFI_ABSOLUTE_POINTER_STATE Value;
  UINT8 Finger[7] = {7, MXT_T9_DETECT, 0x64, 0x25, 8, 0, 42};
  UINT8 Pen[7] = {WACOM_IN_RANGE | WACOM_TIP | WACOM_SIDE, 6, 0x40, 0xa, 0x3c, 2, 0};

  Services.RaiseTPL = Raise;
  Services.RestoreTPL = Restore;
  Services.SignalEvent = Signal;
  gBS = &Services;
  CurrentTpl = TPL_APPLICATION;
  assert(TouchReset(&mAbsolutePointer, FALSE) == EFI_SUCCESS);
  assert(TouchGetState(&mAbsolutePointer, NULL) == EFI_INVALID_PARAMETER);
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_NOT_READY);
  TouchWaitForInput((EFI_EVENT)1, NULL);
  assert(Signals == 0);

  mT9FirstReport = 7;
  mXRange = 799;
  mYRange = 1279;
  mXDropBits = 2;
  mYDropBits = 0;
  MxtHandleMessage(Finger);
  TouchWaitForInput((EFI_EVENT)1, NULL);
  assert(Signals == 1);
  Value = State();
  assert(Value.CurrentX == 3560 && Value.CurrentY == 5340);
  assert(Value.CurrentZ == 42 && Value.ActiveButtons == EFI_ABSP_TouchActive);
  Finger[0] = 8;
  MxtHandleMessage(Finger);
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_NOT_READY);
  Finger[0] = 7;
  Finger[1] = MXT_T9_RELEASE;
  MxtHandleMessage(Finger);
  Value = State();
  assert(Value.CurrentX == 3560 && Value.CurrentY == 5340);
  assert(Value.CurrentZ == 0 && Value.ActiveButtons == 0);

  PenHandlePacket(Pen);
  Value = State();
  assert(Value.CurrentX == 2000 && Value.CurrentY == 10392);
  assert(Value.CurrentZ == 255);
  assert(Value.ActiveButtons == (EFI_ABSP_TouchActive | EFI_ABS_AltActive));
  Finger[1] = MXT_T9_DETECT;
  MxtHandleMessage(Finger);
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_NOT_READY);
  Pen[0] = WACOM_IN_RANGE;
  Pen[5] = Pen[6] = 0;
  PenHandlePacket(Pen);
  Value = State();
  assert(Value.CurrentX == 2000 && Value.CurrentY == 10392);
  assert(Value.CurrentZ == 0 && Value.ActiveButtons == 0);
  Pen[0] = 0;
  PenHandlePacket(Pen);
  Value = State();
  assert(Value.CurrentZ == 0 && Value.ActiveButtons == 0);
  MxtHandleMessage(Finger);
  Value = State();
  assert(Value.CurrentX == 3560 && Value.CurrentY == 5340);
  TouchReport(TouchSourceFinger, TRUE, TOUCH_MAX_X + 1, TOUCH_MAX_Y + 1, 1, EFI_ABSP_TouchActive);
  Value = State();
  assert(Value.CurrentX == TOUCH_MAX_X && Value.CurrentY == TOUCH_MAX_Y);
  assert(TouchReset(&mAbsolutePointer, FALSE) == EFI_SUCCESS);
  assert(TouchGetState(&mAbsolutePointer, &Value) == EFI_NOT_READY);
  puts("Touch packet/pointer tests: PASS (finger, pen, hover, priority, release, event consumption)");
  return 0;
}
