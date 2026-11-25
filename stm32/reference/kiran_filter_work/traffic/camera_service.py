#!/usr/bin/env python3
import argparse
import json
import os
import signal
import socket
import threading
import time

stop = threading.Event()
metrics = {
    "udp_telemetry_received": 0,
    "udp_attack_like_received": 0,
    "tcp_connections": 0,
    "started_at": time.time(),
}


def handle_signal(_signum, _frame):
    stop.set()


def udp_loop(bind, port, label, camera_id):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind, port))
    sock.settimeout(0.2)
    while not stop.is_set():
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            break
        now = time.time()
        if label == "attack":
            metrics["udp_attack_like_received"] += 1
        else:
            metrics["udp_telemetry_received"] += 1
        reply = {
            "camera": camera_id,
            "service": label,
            "recv_time": now,
            "payload": data.decode("utf-8", errors="replace")[:128],
        }
        try:
            sock.sendto(json.dumps(reply).encode(), addr)
        except OSError:
            pass
    sock.close()


def tcp_loop(args):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.bind, args.service_port))
    srv.listen(128)
    srv.settimeout(0.2)
    while not stop.is_set():
        try:
            conn, _addr = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        metrics["tcp_connections"] += 1
        try:
            conn.sendall(b"camera-ok\n")
        except OSError:
            pass
        conn.close()
    srv.close()


def metrics_loop(path):
    while not stop.is_set():
        write_metrics(path)
        stop.wait(1.0)
    write_metrics(path)


def write_metrics(path):
    if not path:
        return
    tmp = f"{path}.tmp"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    body = dict(metrics)
    body["ended_at"] = time.time()
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(body, fh, indent=2, sort_keys=True)
    os.replace(tmp, path)


def main():
    parser = argparse.ArgumentParser(description="Namespace-local camera telemetry and service endpoint")
    parser.add_argument("--camera-id", required=True)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--telemetry-port", type=int, default=5001)
    parser.add_argument("--attack-port", type=int, default=9000)
    parser.add_argument("--service-port", type=int, default=8080)
    parser.add_argument("--metrics", default="")
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    threads = [
        threading.Thread(target=udp_loop, args=(args.bind, args.telemetry_port, "telemetry", args.camera_id), daemon=True),
        threading.Thread(target=udp_loop, args=(args.bind, args.attack_port, "attack", args.camera_id), daemon=True),
        threading.Thread(target=tcp_loop, args=(args,), daemon=True),
        threading.Thread(target=metrics_loop, args=(args.metrics,), daemon=True),
    ]
    for thread in threads:
        thread.start()
    while not stop.is_set():
        time.sleep(0.2)
    write_metrics(args.metrics)


if __name__ == "__main__":
    main()
