#!/usr/bin/env python3
# SmartKnob — Mac ses senkronu
# Mac'in ses seviyesini okur (0-100), 0-16'ya map eder, degisince knob'a "VOL n" gonderir.
# Knob UART/CH340 portundan dinler. HID (ses acma/kisma) native USB'den gelir.
#
# Kullanim: python3 volume_sync.py [port]
# Kablo: native USB -> Mac (HID) + UART/CH340 -> Mac (bu script)

import serial, subprocess, time, glob, sys

def mac_volume():
    r = subprocess.run(["osascript", "-e", "output volume of (get volume settings)"],
                       capture_output=True, text=True)
    try: return int(r.stdout.strip())
    except: return None

def mac_muted():
    r = subprocess.run(["osascript", "-e", "output muted of (get volume settings)"],
                       capture_output=True, text=True)
    return r.stdout.strip() == "true"

def find_port():
    if len(sys.argv) > 1: return sys.argv[1]
    p = glob.glob("/dev/cu.usbserial*")
    return p[0] if p else None

def main():
    port = find_port()
    if not port:
        print("UART portu yok (usbserial). Kablo takili mi?"); return
    s = serial.Serial(port, 115200, timeout=0.3)
    time.sleep(0.5)
    print(f"Baglandi: {port}. Ses senkronu basladi (Ctrl+C durdur).")
    last = -1
    while True:
        vol = mac_volume()
        if vol is not None:
            level = 0 if mac_muted() else round(vol / 100.0 * 16)   # 0..16
            if level != last:
                s.write(f"VOL {level}\n".encode()); s.flush()
                print(f"ses {vol}% -> VOL {level}")
                last = level
        time.sleep(0.2)

if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: print("\ndurduruldu")
