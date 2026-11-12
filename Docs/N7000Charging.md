# GT-N7000 UEFI charging

Status: implemented and host-tested; actual charging has not been tested on a
phone. Do not interpret a successful firmware build or QEMU boot as proof of
battery charging. `Artifacts/build-info.json` records this limitation.

`ChargerDxe` runs only in the N7000 firmware profile, including when USB logging
is disabled. It polls once per second while UEFI Boot Services are running.
It stops charging at ExitBootServices; LiberNT still needs a native charger
and battery driver. The Cortex-A9 nonshareable write-back RAM mapping remains
unchanged.

## Hardware evidence

The board wiring and register values were studied in Samsung's lawfully
published kernel, pinned to LineageOS commit
[`8466fa44184f44d84905094eed8f1413d1da2963`](https://github.com/LineageOS/android_kernel_samsung_smdk4412/tree/8466fa44184f44d84905094eed8f1413d1da2963).
No Linux implementation is included in this BSD firmware driver. The UEFI
transport, state machine, error handling and tests are independently written.

| Fact | Source at that commit |
| --- | --- |
| N7000/Q1 subcharger bus 19, SMB136 address 0x4d, SMB328 address 0x34; fuel gauge bus 9/address 0x36; ADC7 on subcharger boards | [mach-u1.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/arch/arm/mach-exynos/mach-u1.c) |
| GPY0(4/5) charger SDA/SCL, GPY4(0/1) gauge SDA/SCL, GPL2(2) active-low enable, GPX3(3) OTG | [gpio-q1.h](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/arch/arm/mach-exynos/include/mach/gpio-q1.h) |
| SMB136 USB500 mode, 500 mA charge current, 4.2 V float, status/enable registers | [smb136_charger_q1.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/power/smb136_charger_q1.c) |
| SMB328 USB500 mode, 450 mA input limit, 500 mA fast-charge setting, 4.2 V float; command 0x98 off/0x88 on; watchdog enabled by 0x4e at 0x04 and serviced by 0xaa at 0x30; charger termination option 0x9a at 0x03 | [smb328_charger.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/power/smb328_charger.c) |
| MAX8997 main charger disable bits, DC input and battery presence | [max8997_charger_u1.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/power/max8997_charger_u1.c) |
| MUIC STATUS2 source type, detection-running and VBUS bits | [max8997-muic.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/misc/max8997-muic.c) |
| MAX17042 little-endian VCELL units and VF_SOC; externally supplied temperature | [max17042_fuelgauge_u1.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/power/max17042_fuelgauge_u1.c) |
| Q1 recovery temperature 0..43 C, 4.13 V recharge threshold; MAX17042 temperature updated by software | [sec_battery_u1.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/drivers/power/sec_battery_u1.c) |
| ADC 12-bit resolution, prescaler 49, MUX7; clock PERIL bit 15 | [adc.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/arch/arm/plat-samsung/adc.c), [clock-exynos4.c](https://github.com/LineageOS/android_kernel_samsung_smdk4412/blob/8466fa44184f44d84905094eed8f1413d1da2963/arch/arm/mach-exynos/clock-exynos4.c) |

Only the external-charger board variants are supported. An old MAX8997-only
board or ambiguous detection fails with `EFI_UNSUPPORTED`; do not guess its
thermal calibration. A responding address is checked by register readback;
actual silicon selection still needs a phone run.

## Policy and shutdown

Both charger backends use their published USB profile for every accepted supply;
the driver does not request the higher AC current settings. An ordinary USB port
must have configured the USB function before charging is enabled. Reset,
suspend and controller stop revoke that permission and schedule a shutdown
callback. Dedicated/charging-downstream and the MUIC's classified special
chargers may use the same conservative profile without USB enumeration. Unknown,
100 mA dead-battery, detection-in-progress and absent sources do not enable it.
The existing USB log descriptor requests 500 mA. This is not a USB compliance
certification or a guarantee that the whole powered phone fits that budget.

The driver disables the MAX8997 main charger before configuring a subcharger.
It verifies every profile register, and only then enables charging. Battery
presence, fuel-gauge voltage/SOC and the physical ADC thermistor are required.
The MAX17042 temperature register is deliberately unused: Android writes it and
its value can be stale in UEFI. The EUR/Q1 ADC7 calibration maps ADC 630 to 43 C
and 1557 to 0 C; this narrower recovery band is used for admission and cutoff.

Software stops at 4200 mV and permits recharge below 4130 mV. As an explicit
conservative UEFI policy, voltages below 3000 mV are rejected; recovery of deeply
discharged cells is left to the stock charger/Android. This floor is a policy
choice, not an inferred MAX17042 register limit. A four-hour
cumulative charging-poll limit is an additional conservative UEFI policy;
unplugging the source resets that limit. It is not a claim about Samsung's
full Android charge algorithm. Chip safety faults or configuration failures
latch charging off until reboot. Failed sensor reads stop charging immediately.
The two-wire buses use open-drain GPIO transitions and bounded clock stretching.

The SMB328 hardware watchdog is enabled. Both backends also use the already
validated Exynos watchdog with reload 32767 (about 10.7 seconds with the current
clock) while charging, so a stalled polling loop resets firmware. The driver
neither feeds nor disables the external kernel-test watchdog (reload 65535),
and preserves any other enabled reset watchdog. A failed charger shutdown leaves
the watchdog running. Before ExitBootServices it disables charging and releases
only its own watchdog; the kernel-test watchdog remains armed.

## Tests and phone validation

Run `bash Tests/test-charger.sh`. ASan/UBSan checks execute the production charge
policy, inject failure at every I2C operation in an enable cycle, verify thermal
and voltage limits, USB admission, disconnect, presence, termination, readback,
latched faults and shutdown. Additional checks exercise production open-drain
I2C bit order, ACK/NACK, clock timeout, ADC timeout and watchdog ownership.
`bash Tests/test-touch.sh` retains the touch regression test.

Build N7000 with `CCACHE_DISABLE=1 ./build.sh`; the normal project host-tool PATH
may be required as described in the main README. `Artifacts/ChargeTest.efi`
prints voltage, SOC, thermal ADC, raw status and separate `enabled` and `active`
flags for 30 seconds. `enabled` is a successful enable request, whereas `active`
is the charger's own charging-status bit. UEFI USB logs print the same data every
30 seconds. Confirm the detected charger, reasonable ADC/voltage, actual charging
status and a sustained SOC/voltage rise on a known supply before calling this
hardware-tested. Check unplug/replug and ExitBootServices shutdown on the phone.

The SMDKC210 QEMU profile excludes this driver because QEMU does not model these
N7000 battery/charger chips. A successful QEMU kernel regression run only verifies
that the firmware profile and existing kernel boot still work.

The candidate was flashed successfully through Heimdall. `ChargeTest.efi` and
its autostart script were verified on the microSD. The deeply discharged phone
subsequently lost power during UEFI startup before its USB debugger appeared,
so no actual charger or battery measurements have been obtained. This does not
validate charger operation; a preliminary battery charge outside this startup
is required before the next hardware test. Do not relax the admission thresholds
or infer a battery voltage from the screen fading out.
