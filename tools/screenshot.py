#!/usr/bin/env python3
"""cockpit の今の画面を PNG で保存する（開発用）。

使い方:
    ~/.platformio/penv/bin/python tools/screenshot.py /dev/cu.usbserial-XXXX cockpit.png

cockpit は Serial に 'S' を受け取ると「PNG <バイト数>\\n」に続けて PNG を返す。
ポートを開くときに DTR / RTS を触ると ESP32 がリセットされて起動直後の画面になるので、触らずに開く。
PNG を送っている数秒の間は、cockpit の制御が止まる。
"""
import sys
import time

import serial


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    port, out_path = argv
    with serial.Serial(port, 115200, timeout=10) as s:
        s.reset_input_buffer()
        s.write(b"S")
        deadline = time.time() + 10
        # 画面を返すまでに流れてくるほかのログは読み飛ばす
        while time.time() < deadline:
            line = s.readline().decode(errors="replace").strip()
            if line.startswith("PNG "):
                break
        else:
            print("画面が返ってきませんでした", file=sys.stderr)
            return 1
        length = int(line.split()[1])
        if length == 0:
            print("cockpit で PNG を作れませんでした", file=sys.stderr)
            return 1
        data = s.read(length)
    if len(data) != length:
        print(f"途中で途切れました（{len(data)}/{length} バイト）", file=sys.stderr)
        return 1
    with open(out_path, "wb") as f:
        f.write(data)
    print(f"{out_path} に保存しました（{length} バイト）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
