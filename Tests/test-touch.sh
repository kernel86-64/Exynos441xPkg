#!/bin/bash
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -euo pipefail

touch_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$touch_root"
export CCACHE_DISABLE=1
mkdir -p workspace
clang -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fshort-wchar -fno-builtin -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -g \
    -I../edk2/MdePkg/Include -I../edk2/MdePkg/Include/X64 \
    -I../edk2/EmbeddedPkg/Include -IInclude \
    Tests/TouchStateTest.c -Wl,--gc-sections -o workspace/touch-state-test
workspace/touch-state-test
