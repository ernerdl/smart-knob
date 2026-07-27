#!/bin/zsh
# SmartKnob kontrol paneli — CIFT TIKLA ac.
# Tarayicida http://localhost:8787 acilir. Kapatmak icin bu Terminal penceresinde Ctrl+C.
cd "$(dirname "$0")/app" || exit 1
echo "SmartKnob paneli baslatiliyor... (kapatmak: Ctrl+C)"
exec /opt/homebrew/bin/python3 knob_web.py
