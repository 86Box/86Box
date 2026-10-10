#!/usr/bin/env bash
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          ATI Rage 128 Pro -- runs the register harness's vector groups
#          split across parallel processes, through the shared shard.sh
#          one directory up. Usage: shard.sh [N [-v]], where N is the
#          process count (the default is the host's CPU count).
#
# Authors: skiretic.
#
#          Copyright 2026 skiretic.
#

set -euo pipefail
harness_dir=$(cd "$(dirname "$0")" && pwd)
exec bash "$harness_dir/../shard.sh" reg "$harness_dir" "$@"
