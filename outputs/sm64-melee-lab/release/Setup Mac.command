#!/bin/zsh
set -e
cd "${0:A:h}"
if (( $# == 0 )) && [[ -d 'Install Super Melee 64.app' ]]; then
  exec /usr/bin/open 'Install Super Melee 64.app'
fi
for python in .venv/bin/python "$HOME/.super-melee64/.venv/bin/python" python3 python3.14 python3.13 python3.12; do
  if command -v "$python" >/dev/null 2>&1 && "$python" -c 'import sys; import PIL; sys.exit(0 if sys.version_info >= (3,12) else 1)' >/dev/null 2>&1; then
    exec "$python" Setup.py "$@"
  fi
done
if [[ -d 'Install Super Melee 64.app' ]]; then
  exec /usr/bin/open 'Install Super Melee 64.app' --args "$@"
fi
print -u2 'Open Install Super Melee 64 and choose Prepare Build Tools, or install Python 3.12+ with Pillow. See START HERE.txt.'
read '?Press Return to close.'
exit 1
