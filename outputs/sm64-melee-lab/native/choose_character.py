#!/usr/bin/env python3
"""Open the in-window roster selector, preserving the existing launcher name."""
from pathlib import Path
import os, sys

def main():
    os.execv(sys.executable,[sys.executable,str(Path(__file__).with_name('play_embedded.py')),'--select',*sys.argv[1:]])

if __name__=='__main__':sys.exit(main())
