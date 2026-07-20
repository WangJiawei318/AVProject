#!/usr/bin/env python3
"""End-to-end resumable download test for the AVProject stage 9 protocol."""

import argparse
import json
import os
import socket
import struct
import sys
import tempfile
import time
from pathlib import Path


PACK_BASE = 20000
DOWNLOAD_INIT_RQ = PACK_BASE + 13
DOWNLOAD_INIT_RS = PACK_BASE + 14
DOWNLOAD_BLOCK_RQ = PACK_BASE + 15
DOWNLOAD_BLOCK_RS = PACK_BASE + 16
DOWNLOAD_FINISH_RQ = PACK_BASE + 17
DOWNLOAD_FINISH_RS = PACK_BASE + 18

FILE_NAME_SIZE = 256
TEXT_SIZE = 128
BLOCK_SIZE = 64 * 1024
MAX_FRAME_SIZE = 256 * 1024

INIT_RQ_FORMAT = "<i256sqqq"
INIT_RS_FORMAT = "<ii256sqqq128s"
BLOCK_RQ_FORMAT = "<i256sqi"
BLOCK_RS_HEADER_FORMAT = "<ii256sqi128s"
FINISH_RQ_FORMAT = "<i256sq"
FINISH_RS_FORMAT = "<ii256s128s"


def fixed_text(value, size):
    encoded = value.encode("utf-8")
    if len(encoded) >= size:
        raise ValueError("text is too long for a {} byte field".format(size))
    return encoded + b"\0" * (size - len(encoded))


def decode_text(value):
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


def request(sock, body):
    sock.sendall(struct.pack("<i", len(body)) + body)
    frame_size = struct.unpack("<i", recv_exact(sock, 4))[0]
    if frame_size < 4 or frame_size > MAX_FRAME_SIZE:
        raise ValueError("invalid response length: {}".format(frame_size))
    return recv_exact(sock, frame_size)


def unpack_exact(packet, format_string, expected_type):
    expected_size = struct.calcsize(format_string)
    if len(packet) != expected_size:
        raise ValueError(
            "unexpected packet size: expected={} actual={}".format(
                expected_size, len(packet)
            )
        )
    values = struct.unpack(format_string, packet)
    if values[0] != expected_type:
        raise ValueError(
            "unexpected packet type: expected={} actual={}".format(
                expected_type, values[0]
            )
        )
    return values


def connect(host, port):
    sock = socket.create_connection((host, port), timeout=5.0)
    sock.settimeout(10.0)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def send_init(sock, file_name, offset=0, expected_size=0, expected_mtime=0):
    body = struct.pack(
        INIT_RQ_FORMAT,
        DOWNLOAD_INIT_RQ,
        fixed_text(file_name, FILE_NAME_SIZE),
        offset,
        expected_size,
        expected_mtime,
    )
    values = unpack_exact(request(sock, body), INIT_RS_FORMAT, DOWNLOAD_INIT_RS)
    _, result, file_raw, file_size, mtime, accepted, message_raw = values
    return {
        "ok": result != 0,
        "file_name": decode_text(file_raw),
        "file_size": file_size,
        "modified_time": mtime,
        "accepted_offset": accepted,
        "message": decode_text(message_raw),
    }


def request_block(sock, file_name, offset, request_size):
    body = struct.pack(
        BLOCK_RQ_FORMAT,
        DOWNLOAD_BLOCK_RQ,
        fixed_text(file_name, FILE_NAME_SIZE),
        offset,
        request_size,
    )
    packet = request(sock, body)
    header_size = struct.calcsize(BLOCK_RS_HEADER_FORMAT)
    if len(packet) < header_size:
        raise ValueError("short DOWNLOAD_BLOCK_RS")
    values = struct.unpack(BLOCK_RS_HEADER_FORMAT, packet[:header_size])
    packet_type, result, file_raw, response_offset, data_size, message_raw = values
    if packet_type != DOWNLOAD_BLOCK_RS:
        raise ValueError("unexpected download block response type")
    message = decode_text(message_raw)
    if not result:
        raise RuntimeError("download block rejected: {}".format(message))
    if decode_text(file_raw) != file_name or response_offset != offset:
        raise ValueError("download block metadata mismatch")
    data = packet[header_size:]
    if data_size <= 0 or data_size > BLOCK_SIZE or len(data) != data_size:
        raise ValueError("invalid download block payload")
    return data


def send_finish(sock, file_name, file_size):
    body = struct.pack(
        FINISH_RQ_FORMAT,
        DOWNLOAD_FINISH_RQ,
        fixed_text(file_name, FILE_NAME_SIZE),
        file_size,
    )
    values = unpack_exact(request(sock, body), FINISH_RS_FORMAT, DOWNLOAD_FINISH_RS)
    _, result, file_raw, message_raw = values
    if decode_text(file_raw) != file_name:
        raise ValueError("DOWNLOAD_FINISH_RS file name mismatch")
    message = decode_text(message_raw)
    if not result:
        raise RuntimeError("download finish failed: {}".format(message))


def parse_args():
    parser = argparse.ArgumentParser(
        description="Interrupt and resume one AVProject download."
    )
    parser.add_argument("host", help="AVServer IPv4 address")
    parser.add_argument("port", type=int, help="AVServer TCP port")
    parser.add_argument("remote_file", help="Remote media file name in AVServer/media")
    parser.add_argument(
        "--blocks-before-disconnect",
        type=int,
        default=4,
        help="Downloaded blocks before the intentional disconnect",
    )
    parser.add_argument(
        "--output-dir",
        help="Optional directory that keeps the completed test download",
    )
    args = parser.parse_args()
    if args.port <= 0 or args.port > 65535:
        parser.error("port must be in range 1..65535")
    if args.blocks_before_disconnect <= 0:
        parser.error("--blocks-before-disconnect must be greater than zero")
    if (
        not args.remote_file
        or Path(args.remote_file).name != args.remote_file
        or ".." in args.remote_file
    ):
        parser.error("remote_file must be a safe base file name")
    return args


def run_in_directory(args, output_dir):
    started = time.perf_counter()
    part_path = output_dir / (args.remote_file + ".part")
    final_path = output_dir / args.remote_file
    state_path = output_dir / "download_resume_state.json"

    with connect(args.host, args.port) as sock:
        initialized = send_init(sock, args.remote_file)
        if not initialized["ok"]:
            raise RuntimeError("download init failed: {}".format(initialized["message"]))
        if (
            initialized["file_name"] != args.remote_file
            or initialized["file_size"] <= 0
            or initialized["modified_time"] <= 0
            or initialized["accepted_offset"] != 0
        ):
            raise ValueError("server returned invalid initial download metadata")

        confirmed_offset = 0
        with part_path.open("wb") as output:
            for _ in range(args.blocks_before_disconnect):
                if confirmed_offset >= initialized["file_size"]:
                    raise ValueError("remote file is too small for the interruption point")
                request_size = min(BLOCK_SIZE, initialized["file_size"] - confirmed_offset)
                data = request_block(sock, args.remote_file, confirmed_offset, request_size)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
                confirmed_offset += len(data)

        if confirmed_offset >= initialized["file_size"]:
            raise ValueError("choose fewer blocks so the first connection stops early")
        state = {
            "remote_filename": args.remote_file,
            "server_ip": args.host,
            "server_port": args.port,
            "expected_file_size": initialized["file_size"],
            "expected_modified_time": initialized["modified_time"],
            "confirmed_offset": confirmed_offset,
            "local_part_path": str(part_path),
        }
        state_path.write_text(json.dumps(state, indent=2), encoding="utf-8")
        print("intentional disconnect at confirmed_offset={}".format(confirmed_offset))

    time.sleep(0.25)
    saved = json.loads(state_path.read_text(encoding="utf-8"))
    actual_part_size = part_path.stat().st_size
    safe_offset = min(saved["confirmed_offset"], actual_part_size)
    if actual_part_size > safe_offset:
        with part_path.open("r+b") as output:
            output.truncate(safe_offset)
    saved["confirmed_offset"] = safe_offset
    state_path.write_text(json.dumps(saved, indent=2), encoding="utf-8")
    if safe_offset <= 0:
        raise ValueError("resume offset must be greater than zero")

    with connect(args.host, args.port) as sock:
        resumed = send_init(
            sock,
            saved["remote_filename"],
            safe_offset,
            saved["expected_file_size"],
            saved["expected_modified_time"],
        )
        if not resumed["ok"]:
            raise RuntimeError("download resume failed: {}".format(resumed["message"]))
        if (
            resumed["file_size"] != saved["expected_file_size"]
            or resumed["modified_time"] != saved["expected_modified_time"]
            or resumed["accepted_offset"] != safe_offset
        ):
            raise ValueError("server returned an invalid accepted offset or file version")
        print("resume accepted offset={}".format(resumed["accepted_offset"]))

        confirmed_offset = resumed["accepted_offset"]
        with part_path.open("r+b") as output:
            output.seek(confirmed_offset)
            while confirmed_offset < resumed["file_size"]:
                request_size = min(BLOCK_SIZE, resumed["file_size"] - confirmed_offset)
                data = request_block(sock, args.remote_file, confirmed_offset, request_size)
                output.write(data)
                confirmed_offset += len(data)
            output.flush()
            os.fsync(output.fileno())
        send_finish(sock, args.remote_file, resumed["file_size"])

    if part_path.stat().st_size != resumed["file_size"]:
        raise ValueError("completed partial file size does not match server metadata")
    if final_path.exists():
        final_path.unlink()
    part_path.replace(final_path)
    state_path.unlink()
    if final_path.stat().st_size != resumed["file_size"]:
        raise ValueError("final file size verification failed")
    print("download completed path={} size={}".format(final_path, resumed["file_size"]))
    print("PASS elapsed={:.3f}s".format(time.perf_counter() - started))


def run(args):
    if args.output_dir:
        output_dir = Path(args.output_dir).resolve()
        output_dir.mkdir(parents=True, exist_ok=True)
        run_in_directory(args, output_dir)
        return
    with tempfile.TemporaryDirectory(prefix="avproject_download_resume_") as directory:
        run_in_directory(args, Path(directory))


def main():
    args = parse_args()
    try:
        run(args)
        return 0
    except Exception as error:
        print("FAIL: {}".format(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
