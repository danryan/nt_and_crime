#!/usr/bin/env bash
# harness/scripts/deploy.sh
# Wrapper for non-USB-MSC deploy mechanisms.
# Currently a stub; the Makefile deploy target handles USB MSC directly.
set -euo pipefail
echo "deploy.sh stub: not yet implemented" >&2
echo "Use 'make deploy DEVICE=/path/to/mount' for USB MSC deploy." >&2
exit 1
