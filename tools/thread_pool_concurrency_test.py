#!/usr/bin/env python3
"""Concurrent protocol smoke test for the AVServer stage 10 thread pool."""

import argparse
import concurrent.futures
import socket
import struct
import sys
import threading
import time


PACK_BASE = 20000
PING_RQ = PACK_BASE + 1
PING_RS = PACK_BASE + 2
MEDIA_LIST_RQ = PACK_BASE + 5
MEDIA_LIST_RS = PACK_BASE + 6
DOWNLOAD_INIT_RQ = PACK_BASE + 13
DOWNLOAD_INIT_RS = PACK_BASE + 14
DOWNLOAD_BLOCK_RQ = PACK_BASE + 15
DOWNLOAD_BLOCK_RS = PACK_BASE + 16

MAX_FRAME_SIZE = 256 * 1024
BLOCK_SIZE = 64 * 1024
PING_FORMAT = "<i128s"
MEDIA_LIST_HEADER_FORMAT = "<ii"
DOWNLOAD_INIT_RQ_FORMAT = "<i256sqqq"
DOWNLOAD_INIT_RS_FORMAT = "<ii256sqqq128s"
DOWNLOAD_BLOCK_RQ_FORMAT = "<i256sqi"
DOWNLOAD_BLOCK_RS_HEADER_FORMAT = "<ii256sqi128s"


def fixed_text(value, size):
    encoded = value.encode("utf-8")
    if len(encoded) >= size:
        raise ValueError("text is too long for protocol field")
    return encoded + b"\0" * (size - len(encoded))


def decoded_text(value):
    return value.split(b"\0", 1)[0].decode("utf-8")


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


def send_frame(sock, body):
    sock.sendall(struct.pack("<i", len(body)) + body)


def recv_frame(sock):
    frame_size = struct.unpack("<i", recv_exact(sock, 4))[0]
    if frame_size < 4 or frame_size > MAX_FRAME_SIZE:
        raise ValueError("invalid response frame size: {}".format(frame_size))
    return recv_exact(sock, frame_size)


def request(sock, body):
    send_frame(sock, body)
    return recv_frame(sock)


def check_ping(sock, client_index, request_index):
    message = "client={} request={}".format(client_index, request_index)
    packet = request(sock, struct.pack(PING_FORMAT, PING_RQ, fixed_text(message, 128)))
    if len(packet) != struct.calcsize(PING_FORMAT):
        raise ValueError("invalid PING_RS size")
    packet_type, response_message = struct.unpack(PING_FORMAT, packet)
    if packet_type != PING_RS or "pong" not in decoded_text(response_message):
        raise ValueError("invalid PING_RS")


def check_media_list(sock):
    packet = request(sock, struct.pack("<i", MEDIA_LIST_RQ))
    header_size = struct.calcsize(MEDIA_LIST_HEADER_FORMAT)
    if len(packet) < header_size:
        raise ValueError("short MEDIA_LIST_RS")
    packet_type, payload_size = struct.unpack(
        MEDIA_LIST_HEADER_FORMAT, packet[:header_size]
    )
    payload = packet[header_size:]
    if packet_type != MEDIA_LIST_RS or payload_size != len(payload):
        raise ValueError("invalid MEDIA_LIST_RS")
    if payload == b"server busy":
        raise RuntimeError("server busy")


def check_download_blocks(sock, remote_file, block_count):
    init_body = struct.pack(
        DOWNLOAD_INIT_RQ_FORMAT,
        DOWNLOAD_INIT_RQ,
        fixed_text(remote_file, 256),
        0,
        0,
        0,
    )
    init_packet = request(sock, init_body)
    if len(init_packet) != struct.calcsize(DOWNLOAD_INIT_RS_FORMAT):
        raise ValueError("invalid DOWNLOAD_INIT_RS size")
    values = struct.unpack(DOWNLOAD_INIT_RS_FORMAT, init_packet)
    packet_type, result, file_raw, file_size, _, accepted, message_raw = values
    if packet_type != DOWNLOAD_INIT_RS or not result:
        raise RuntimeError("download init failed: {}".format(decoded_text(message_raw)))
    if decoded_text(file_raw) != remote_file or file_size <= 0 or accepted != 0:
        raise ValueError("invalid download metadata")

    offset = 0
    header_size = struct.calcsize(DOWNLOAD_BLOCK_RS_HEADER_FORMAT)
    for _ in range(block_count):
        if offset >= file_size:
            break
        request_size = min(BLOCK_SIZE, file_size - offset)
        block_body = struct.pack(
            DOWNLOAD_BLOCK_RQ_FORMAT,
            DOWNLOAD_BLOCK_RQ,
            fixed_text(remote_file, 256),
            offset,
            request_size,
        )
        packet = request(sock, block_body)
        if len(packet) < header_size:
            raise ValueError("short DOWNLOAD_BLOCK_RS")
        values = struct.unpack(DOWNLOAD_BLOCK_RS_HEADER_FORMAT, packet[:header_size])
        packet_type, result, file_raw, response_offset, data_size, message_raw = values
        data = packet[header_size:]
        if not result:
            raise RuntimeError("download block failed: {}".format(decoded_text(message_raw)))
        if (
            packet_type != DOWNLOAD_BLOCK_RS
            or decoded_text(file_raw) != remote_file
            or response_offset != offset
            or data_size <= 0
            or data_size != len(data)
        ):
            raise ValueError("invalid DOWNLOAD_BLOCK_RS")
        offset += data_size


def run_client(args, client_index, start_barrier):
    with socket.create_connection((args.host, args.port), timeout=5.0) as sock:
        sock.settimeout(args.timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        start_barrier.wait(timeout=args.timeout)

        if client_index == 0:
            send_frame(sock, struct.pack("<i", MEDIA_LIST_RQ))
            return "client 0 intentionally disconnected during business request"

        check_media_list(sock)
        for request_index in range(args.requests):
            check_ping(sock, client_index, request_index)
            if request_index < args.business_requests:
                check_media_list(sock)

        if args.download_file:
            check_download_blocks(sock, args.download_file, args.download_blocks)
        return "client {} completed".format(client_index)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Exercise AVServer Ping and thread-pool business requests concurrently."
    )
    parser.add_argument("host", help="AVServer IPv4 address")
    parser.add_argument("port", type=int, help="AVServer TCP port")
    parser.add_argument("--clients", type=int, default=5)
    parser.add_argument("--requests", type=int, default=10,
                        help="Ping requests per non-disconnecting client")
    parser.add_argument("--business-requests", type=int, default=3,
                        help="Additional media-list requests per client")
    parser.add_argument("--download-file",
                        help="Optional safe file name in AVServer/media")
    parser.add_argument("--download-blocks", type=int, default=2)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()
    if args.port <= 0 or args.port > 65535:
        parser.error("port must be in range 1..65535")
    if args.clients < 5:
        parser.error("--clients must be at least 5")
    if args.requests <= 0 or args.business_requests < 0:
        parser.error("request counts are invalid")
    if args.download_blocks <= 0 or args.timeout <= 0:
        parser.error("download blocks and timeout must be positive")
    if args.download_file and (
        "/" in args.download_file
        or "\\" in args.download_file
        or ".." in args.download_file
    ):
        parser.error("--download-file must be a safe base file name")
    return args


def main():
    args = parse_args()
    started = time.perf_counter()
    start_barrier = threading.Barrier(args.clients)
    succeeded = 0
    failed = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.clients) as executor:
        futures = [
            executor.submit(run_client, args, index, start_barrier)
            for index in range(args.clients)
        ]
        for future in concurrent.futures.as_completed(futures):
            try:
                print("PASS {}".format(future.result()))
                succeeded += 1
            except Exception as error:
                print("FAIL {}".format(error), file=sys.stderr)
                failed += 1

    elapsed = time.perf_counter() - started
    print("SUMMARY succeeded={} failed={} elapsed={:.3f}s".format(
        succeeded, failed, elapsed
    ))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
