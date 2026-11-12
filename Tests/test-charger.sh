#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -euo pipefail
charger_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$charger_root"
export CCACHE_DISABLE=1
mkdir -p workspace
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fshort-wchar -fno-builtin -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -g \
    -I../edk2/MdePkg/Include -I../edk2/MdePkg/Include/X64 -IInclude \
    Tests/ChargerTest.c -Wl,--gc-sections -o workspace/charger-test
workspace/charger-test
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fshort-wchar -fno-builtin -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -g \
    -I../edk2/MdePkg/Include -I../edk2/MdePkg/Include/X64 -IInclude \
    Tests/ChargerHardwareTest.c -Wl,--gc-sections -o workspace/charger-hardware-test
workspace/charger-hardware-test
