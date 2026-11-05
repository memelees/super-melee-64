#!/bin/zsh
set -e
cd "${0:A:h}"
if (( $# )); then
  if [[ -x .venv/bin/python ]]; then
    exec .venv/bin/python outputs/sm64-melee-lab/native/play_embedded.py "$@"
  else
    exec python3.13 outputs/sm64-melee-lab/native/play_embedded.py "$@"
  fi
fi
exec /usr/bin/open "Super Melee 64.app"
