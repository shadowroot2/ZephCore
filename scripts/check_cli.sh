#!/usr/bin/env bash
set -euo pipefail

python3 scripts/check_cli.py
python3 tests/run.py run --profile quick --case UNIT-CLI-001
python3 tests/run.py run --profile quick --case UNIT-CLI-002
python3 tests/run.py run --profile quick --case UNIT-CLI-003
