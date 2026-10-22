#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Kept for existing habits and scripts: the checker is scripts/check_format.py.
exec python3 "$(dirname "$0")/check_format.py" "$@"
