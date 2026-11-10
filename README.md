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
The rebuilt image currently omits TouchDxe because the working binary's
touch driver is absent from the published package sources. A subsequent hardware boot also started FreeLoader, mounted the SD FAT
volume and read its test boot configuration. Kernel startup was not part
of these firmware/loader tests.
