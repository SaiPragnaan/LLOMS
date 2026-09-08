#!/usr/bin/env python3
"""
TitanMatch Interactive Python Client
Usage:
    python3 tools/client.py [host] [port]
"""

import socket
import sys
import threading

def receive_messages(sock):
    buffer = ""
    while True:
        try:
            data = sock.recv(4096)
            if not data:
                print("\n[INFO] Connection closed by server.")
                break
            buffer += data.decode("utf-8")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                print(f"<<< {line}")
        except Exception as e:
            break

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

    print("=========================================================")
    print("         TITANMATCH INTERACTIVE TRADING CLIENT           ")
    print("=========================================================")
    print(f"Connecting to {host}:{port}...")

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.connect((host, port))
        print("[CONNECTED] You can now type orders and press Enter.")
        print("Commands:")
        print("  NEW BUY <ID> <PRICE_IN_TICKS> <QTY> [LIMIT|MARKET]")
        print("  NEW SELL <ID> <PRICE_IN_TICKS> <QTY> [LIMIT|MARKET]")
        print("  CANCEL <ID>")
        print("  FLUSH")
        print("  quit")
        print("=========================================================")
    except Exception as e:
        print(f"[ERROR] Failed to connect: {e}")
        return

    rx_thread = threading.Thread(target=receive_messages, args=(sock,), daemon=True)
    rx_thread.start()

    while True:
        try:
            cmd = input().strip()
            if not cmd:
                continue
            if cmd.lower() in ("quit", "exit"):
                break
            sock.sendall((cmd + "\n").encode("utf-8"))
        except (KeyboardInterrupt, EOFError):
            break

    sock.close()
    print("[INFO] Disconnected.")

if __name__ == "__main__":
    main()

