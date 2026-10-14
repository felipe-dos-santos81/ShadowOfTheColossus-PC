#!/usr/bin/env bash
# Starts the Shadow of the Colossus Tk launcher (launcher/sotc_launcher_win.py) with a Python
# that has Tk (macOS: brew install python-tk@3.13).
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
for python in /opt/homebrew/bin/python3.13 /opt/homebrew/bin/python3.14 python3.13 python3.14 python3 /usr/bin/python3; do
    if command -v "$python" >/dev/null && "$python" -c 'import tkinter' 2>/dev/null; then
        exec "$python" "$here/sotc_launcher_win.py" "$@"
    fi
done
echo 'Need a Python with Tk: brew install python-tk@3.13' >&2
exit 1
