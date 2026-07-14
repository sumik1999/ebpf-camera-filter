#!/usr/bin/env python3
import argparse
import socket


def main():
    parser = argparse.ArgumentParser(description="Simple UDP telemetry echo server")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    while True:
        data, addr = sock.recvfrom(4096)
        sock.sendto(data, addr)


if __name__ == "__main__":
    main()

