#!/usr/bin/env python3
"""Concurrent Ping/media-list smoke test for the AVProject epoll server."""

import argparse
import socket
import struct
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


PACK_BASE = 20000
PING_RQ = PACK_BASE + 1
PING_RS = PACK_BASE + 2
MEDIA_LIST_RQ = PACK_BASE + 5
MEDIA_LIST_RS = PACK_BASE + 6
MAX_FRAME_SIZE = 1024 * 1024


def encode_frame(body):
    # The current C++ protocol uses little-endian host order on Windows/Ubuntu x86.
    return struct.pack("<i", len(body)) + body


def recv_exact(sock, size):
    chunks = []
    received = 0
    while received < size:
        chunk = sock.recv(size - received)
        if not chunk:
            raise ConnectionError("server closed the connection")
        chunks.append(chunk)
        received += len(chunk)
    return b"".join(chunks)


def recv_frame(sock):
    packet_length = struct.unpack("<i", recv_exact(sock, 4))[0]
    if packet_length < 4 or packet_length > MAX_FRAME_SIZE:
        raise ValueError("invalid response packet length: {}".format(packet_length))
    return recv_exact(sock, packet_length)


def run_client(index, host, port, ping_count, request_media_list, start_barrier):
    started = time.perf_counter()
    ping_responses = 0
    media_rows = None
    try:
        with socket.create_connection((host, port), timeout=5.0) as sock:
            sock.settimeout(5.0)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            start_barrier.wait(timeout=10.0)

            for sequence in range(ping_count):
                message = "python client={} ping={}".format(index, sequence).encode("utf-8")
                request = struct.pack("<i128s", PING_RQ, message)
                sock.sendall(encode_frame(request))

                response = recv_frame(sock)
                if len(response) != struct.calcsize("<i128s"):
                    raise ValueError("unexpected PING_RS size: {}".format(len(response)))
                response_type, _ = struct.unpack("<i128s", response)
                if response_type != PING_RS:
                    raise ValueError("expected PING_RS, received {}".format(response_type))
                ping_responses += 1

            if request_media_list:
                sock.sendall(encode_frame(struct.pack("<i", MEDIA_LIST_RQ)))
                response = recv_frame(sock)
                if len(response) < 8:
                    raise ValueError("MEDIA_LIST_RS is too short")
                response_type, payload_size = struct.unpack("<ii", response[:8])
                if response_type != MEDIA_LIST_RS:
                    raise ValueError(
                        "expected MEDIA_LIST_RS, received {}".format(response_type)
                    )
                payload = response[8:]
                if payload_size != len(payload):
                    raise ValueError(
                        "MEDIA_LIST_RS payload mismatch: declared={} actual={}".format(
                            payload_size, len(payload)
                        )
                    )
                text = payload.decode("utf-8")
                media_rows = len([line for line in text.splitlines() if line])

        return {
            "index": index,
            "ok": True,
            "pings": ping_responses,
            "media_rows": media_rows,
            "elapsed": time.perf_counter() - started,
            "error": "",
        }
    except Exception as error:  # Each client reports its own failure without hiding peers.
        try:
            start_barrier.abort()
        except threading.BrokenBarrierError:
            pass
        return {
            "index": index,
            "ok": False,
            "pings": ping_responses,
            "media_rows": media_rows,
            "elapsed": time.perf_counter() - started,
            "error": str(error),
        }


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run concurrent AVProject Ping requests against AVServer."
    )
    parser.add_argument("host", help="AVServer IPv4 address")
    parser.add_argument("port", type=int, help="AVServer TCP port")
    parser.add_argument("clients", nargs="?", type=int, default=5)
    parser.add_argument("--pings", type=int, default=5, help="Ping requests per client")
    parser.add_argument(
        "--media-list",
        action="store_true",
        help="Request and validate one media list per client",
    )
    args = parser.parse_args()
    if args.clients <= 0:
        parser.error("clients must be greater than zero")
    if args.pings <= 0:
        parser.error("--pings must be greater than zero")
    if args.port <= 0 or args.port > 65535:
        parser.error("port must be in range 1..65535")
    return args


def main():
    args = parse_args()
    started = time.perf_counter()
    barrier = threading.Barrier(args.clients)
    results = []

    with ThreadPoolExecutor(max_workers=args.clients) as executor:
        futures = [
            executor.submit(
                run_client,
                index,
                args.host,
                args.port,
                args.pings,
                args.media_list,
                barrier,
            )
            for index in range(args.clients)
        ]
        for future in as_completed(futures):
            results.append(future.result())

    results.sort(key=lambda item: item["index"])
    for result in results:
        if result["ok"]:
            media_text = ""
            if result["media_rows"] is not None:
                media_text = " media_rows={}".format(result["media_rows"])
            print(
                "client={index} OK pings={pings}{media} elapsed={elapsed:.3f}s".format(
                    index=result["index"],
                    pings=result["pings"],
                    media=media_text,
                    elapsed=result["elapsed"],
                )
            )
        else:
            print(
                "client={index} FAILED pings={pings} error={error}".format(**result),
                file=sys.stderr,
            )

    success_count = sum(1 for result in results if result["ok"])
    failure_count = len(results) - success_count
    total_pings = sum(result["pings"] for result in results)
    print(
        "summary clients={} success={} failed={} ping_responses={} elapsed={:.3f}s".format(
            len(results),
            success_count,
            failure_count,
            total_pings,
            time.perf_counter() - started,
        )
    )
    return 0 if failure_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
