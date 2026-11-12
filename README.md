# Exynos441xPkg for GT-N7000

EDK II firmware for Samsung Exynos4, currently tested on the Galaxy Note
GT-N7000 (Exynos4210, Cortex-A9). This fork is maintained at
https://github.com/kernel86-64/Exynos441xPkg.

## RAM cacheability

The current firmware runs one processor. System DRAM uses
`ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_NONSHAREABLE`; the framebuffer
keeps its existing non-cacheable mapping. The SD/eMMC and USB-function
backends use PIO.

On the tested phone, ACTLR is zero. The Arm Cortex-A9 TRM, DDI 0388,
[Auxiliary Control Register](https://documentation-service.arm.com/static/5f035b2ddbdee951c1cd7172),
specifies that Inner Cacheable Shareable memory is treated as non-cacheable
when ACTLR.SMP is clear. A Shareable Write-Back mapping therefore prevented
normal RAM caching despite SCTLR.C being set. The firmware's uniprocessor
mapping avoids changing coherency state inherited from the bootloader.

## Build

Place this package and EDK II in the same parent directory. The tested EDK
II baseline is `edk2-stable202508`, commit
`d46aa46c8361194521391aa581593e556c707c6e`. Required tools include Clang/LLD,
LLVM ar/objcopy, GCC/G++, make, Python 3 and ACPICA `iasl` on PATH.

From the parent directory:

```sh
git clone --depth 1 --branch edk2-stable202508 https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init --depth 1 \
  BaseTools/Source/C/BrotliCompress/brotli \
  MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
  MdePkg/Library/BaseFdtLib/libfdt \
  MdePkg/Library/MipiSysTLib/mipisyst
make -C BaseTools -j8 CC='gcc -std=gnu17' \
  EXTRA_OPTFLAGS=-Wno-error=unused-but-set-variable
cd ../Exynos441xPkg
make -C BootShim UEFI_BASE=0x41000000 UEFI_SIZE=0x00100000 \
  CC='clang --target=arm-none-eabi' OBJCOPY=llvm-objcopy
CCACHE_DISABLE=1 ./build.sh
```

The GNU C dialect and warning option above allow these BaseTools to build
with GCC 16. The package's build script applies its existing GenFw ARM
relocation and English collation patches to the EDK II checkout. ACPI
linking explicitly uses LLD without PIE. `PACKAGES_PATH` can be supplied
by the caller; its default requires only EDK II and this package's parent.

The output is `Artifacts/bootpayload.bin`: the 576-byte BootShim followed
by the 1 MiB firmware device. The original known-working firmware was kept
separately during testing. To install from Samsung Download Mode:

```sh
heimdall flash --KERNEL Artifacts/bootpayload.bin
```

The built-in EFI Shell can run `startup.nsh` from the microSD without a
keyboard. LiberNT's EFI loader and firmware diagnostic applications can
be launched through this mechanism.

## Hardware validation

On 2026-11-10, the rebuilt firmware booted on GT-N7000 and automatically
ran the LiberNT firmware performance application from microSD. All service
status codes below were success; the application returned to EFI Shell.

| Observation | Supplied firmware | Rebuilt firmware |
| --- | ---: | ---: |
| ACTLR | 0 | 0 |
| RAM descriptor | `0x7a511c0e` (shared WB) | `0x7a501c0e` (non-shared WB) |
| Observed CPU frequency | 1200 MHz | 800 MHz |
| Stall requested for 100 ms | 102246 us | 100004 us |
| Four 1 MiB RAM copies | 57334 us | 21699 us |
| Sixteen serial lines | 980332 us | 48601 us |
| GOP full-frame read | 62927 us | 19248 us |
| GOP full-frame restore | 62691 us | 30459 us |

The framebuffer descriptor remained `0x5ec11c02`. The serial renderer and
its full-frame cache maintenance were unchanged. The maintainer also
observed a substantially faster interface.

The comparison uses different CPU frequencies and firmware builds, so it
is not a controlled same-frequency benchmark of a single source change.
That performance-test build omitted TouchDxe because the working binary's
touch driver was then absent from the published package sources. A subsequent hardware boot also started FreeLoader, mounted the SD FAT
volume and read its test boot configuration. Kernel startup was not part
of these firmware/loader tests.

## Touchscreen and S Pen

The N7000 build now includes the BSD-licensed `Drivers/TouchDxe` and
`Application/TouchTestApp` sources from upstream commit
`af2d2c53aec9fd52529cf1405c0e1f8449ec63e9`. The driver combines the Atmel
mXT540E touchscreen and Wacom S Pen into EFI Absolute Pointer. The pen takes
priority while in range. FreeLoader can use this protocol during Boot
Services; this is not a LiberNT kernel touchscreen driver.

System RAM retains `ARM_MEMORY_REGION_ATTRIBUTE_WRITE_BACK_NONSHAREABLE`,
the setting validated in the performance comparison above. The upstream
change to shared write-back RAM and the unrelated CpuFreqDxe were not imported.

`./build.sh` writes the N7000 firmware and `TouchTest.efi` into `Artifacts`.
To test touch without typing in EFI Shell, copy the application to microSD
and use this `startup.nsh`:

```text
@echo -off
TouchTest.efi
```

The application draws finger/pen contacts, shows coordinates and pressure,
and exits on a physical volume, power or home key. Physical finger/pen input
validation is still pending. The rebuilt firmware booted on N7000, detected
the mXT540E's 800x1280 configuration and the Wacom digitizer (firmware 0x0340),
and passed the firmware performance application. At an observed 800 MHz, its
RAM descriptor remained `0x7a501c0e`, four 1 MiB copies took 21640 us, sixteen
serial lines 48621 us, GOP read 19211 us and GOP restore 30451 us. All service
statuses were success. These results agree with the previous non-shareable
write-back measurements above.

`bash Tests/test-touch.sh` runs the production finger/pen packet decoders and
pointer-state handling under host ASan/UBSan. It covers first-finger selection,
coordinate scaling, pressure saturation, pen hover, pen priority, release and
event consumption. It does not emulate I2C transfers or hardware interrupts.

`./build.sh --smdkc210` selects the Cortex-A9 QEMU firmware profile and omits
phone-only touch, keypad and USB device drivers. This profile uses zero EFI
Shell startup delay for bounded headless tests. LiberNT's `arm_smdkc210_boot`
trampoline configures the emulated FIMD before this firmware starts; CPU-executed
MMIO writes replace the old generic-loader register presets that were not
retained. The `--stage startup` harness has reached the 800x1280 lock screen,
including native SD/FAT, paging-file creation, CSRSS, Winlogon and LSASS.
Keyboard/mouse input and login on SMDKC210 remain untested.
Rebuild without this option before flashing a phone. Check
`Artifacts/bootpayload.target`, `bootpayload.sha256` and `build-info.json`;
the latter records the selected DRAM attributes and whether TouchDxe is included.

### N7000 UEFI charging

The N7000 profile now builds `ChargerDxe` and `Artifacts/ChargeTest.efi` for the
SMB136/SMB328 external charger and MAX17042 gauge. Charging is supervised only
while Boot Services are available and is disabled before kernel handoff.
Hardware validation is pending; see [charging evidence, limits and tests](Docs/N7000Charging.md).
Run `bash Tests/test-charger.sh` for the host fault-injection and hardware-helper checks.
