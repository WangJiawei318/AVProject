#!/usr/bin/env python3
"""End-to-end resumable upload test for the AVProject stage 8 protocol."""

import argparse
import json
import socket
import struct
import sys
import tempfile
import time
from pathlib import Path


PACK_BASE = 20000
UPLOAD_INIT_RQ = PACK_BASE + 7
UPLOAD_INIT_RS = PACK_BASE + 8
UPLOAD_BLOCK_RQ = PACK_BASE + 9
UPLOAD_BLOCK_RS = PACK_BASE + 10
UPLOAD_FINISH_RQ = PACK_BASE + 11
UPLOAD_FINISH_RS = PACK_BASE + 12
UPLOAD_RESUME_RQ = PACK_BASE + 19
UPLOAD_RESUME_RS = PACK_BASE + 20

TRANSFER_ID_SIZE = 96
RESUME_TOKEN_SIZE = 128
FILE_NAME_SIZE = 256
EXTENSION_SIZE = 16
TEXT_SIZE = 128
BLOCK_SIZE = 64 * 1024
MAX_FRAME_SIZE = 256 * 1024

INIT_RQ_FORMAT = "<iq256s16s"
INIT_RS_FORMAT = "<ii96s128sq256s128s"
RESUME_RQ_FORMAT = "<i96s128s256sq"
RESUME_RS_FORMAT = "<ii96sq256s128s"
BLOCK_RQ_HEADER_FORMAT = "<i96sqi"
BLOCK_RS_FORMAT = "<ii96sq128s"
FINISH_RQ_FORMAT = "<i96s256sq"
FINISH_RS_FORMAT = "<ii256s128s"


def fixed_text(value, size):
    encoded = value.encode("utf-8")
    if len(encoded) >= size:
        raise ValueError("text is too long for a {} byte field".format(size))
    return encoded + b"\0" * (size - len(encoded))


def decode_text(value):
    return value.split(b"\0", 1)[0].decode("utf-8")


def encode_frame(body):
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
        raise ValueError("invalid response length: {}".format(packet_length))
    return recv_exact(sock, packet_length)


def request(sock, body):
    sock.sendall(encode_frame(body))
    return recv_frame(sock)


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


def create_test_file(directory, size_bytes):
    file_name = "resume_test_{}.mp4".format(int(time.time()))
    path = Path(directory) / file_name
    pattern = bytes((index % 251 for index in range(BLOCK_SIZE)))
    remaining = size_bytes
    with path.open("wb") as output:
        while remaining > 0:
            chunk = pattern[: min(len(pattern), remaining)]
            output.write(chunk)
            remaining -= len(chunk)
    return path


def parse_init_response(packet):
    values = unpack_exact(packet, INIT_RS_FORMAT, UPLOAD_INIT_RS)
    _, result, transfer_raw, token_raw, offset, final_raw, message_raw = values
    return {
        "ok": result != 0,
        "transfer_id": decode_text(transfer_raw),
        "resume_token": decode_text(token_raw),
        "offset": offset,
        "final_name": decode_text(final_raw),
        "message": decode_text(message_raw),
    }


def parse_resume_response(packet):
    values = unpack_exact(packet, RESUME_RS_FORMAT, UPLOAD_RESUME_RS)
    _, result, transfer_raw, offset, final_raw, message_raw = values
    return {
        "ok": result != 0,
        "transfer_id": decode_text(transfer_raw),
        "offset": offset,
        "final_name": decode_text(final_raw),
        "message": decode_text(message_raw),
    }


def send_block(sock, transfer_id, offset, data):
    header = struct.pack(
        BLOCK_RQ_HEADER_FORMAT,
        UPLOAD_BLOCK_RQ,
        fixed_text(transfer_id, TRANSFER_ID_SIZE),
        offset,
        len(data),
    )
    response = request(sock, header + data)
    values = unpack_exact(response, BLOCK_RS_FORMAT, UPLOAD_BLOCK_RS)
    _, result, transfer_raw, confirmed_offset, message_raw = values
    response_transfer_id = decode_text(transfer_raw)
    message = decode_text(message_raw)
    if response_transfer_id != transfer_id:
        raise ValueError("UPLOAD_BLOCK_RS transfer id mismatch")
    if not result:
        raise RuntimeError(
            "upload block rejected at {}: {} (server offset={})".format(
                offset, message, confirmed_offset
            )
        )
    return confirmed_offset


def parse_args():
    parser = argparse.ArgumentParser(
        description="Interrupt and resume one AVProject upload."
    )
    parser.add_argument("host", help="AVServer IPv4 address")
    parser.add_argument("port", type=int, help="AVServer TCP port")
    parser.add_argument(
        "--size-mb", type=int, default=2, help="Generated test file size in MiB"
    )
    parser.add_argument(
        "--blocks-before-disconnect",
        type=int,
        default=4,
        help="Confirmed blocks before the intentional disconnect",
    )
    parser.add_argument(
        "--server-media-dir",
        help="Optional locally accessible AVServer/media directory for size verification",
    )
    args = parser.parse_args()
    if args.port <= 0 or args.port > 65535:
        parser.error("port must be in range 1..65535")
    if args.size_mb <= 0:
        parser.error("--size-mb must be greater than zero")
    if args.blocks_before_disconnect <= 0:
        parser.error("--blocks-before-disconnect must be greater than zero")
    if args.size_mb * 1024 * 1024 <= args.blocks_before_disconnect * BLOCK_SIZE:
        parser.error("test file must contain data after the intentional disconnect")
    return args


def run(args):
    started = time.perf_counter()
    size_bytes = args.size_mb * 1024 * 1024
    with tempfile.TemporaryDirectory(prefix="avproject_resume_") as directory:
        test_file = create_test_file(directory, size_bytes)
        file_name = test_file.name
        print("created test file name={} size={}".format(file_name, size_bytes))

        with connect(args.host, args.port) as sock:
            init_request = struct.pack(
                INIT_RQ_FORMAT,
                UPLOAD_INIT_RQ,
                size_bytes,
                fixed_text(file_name, FILE_NAME_SIZE),
                fixed_text("mp4", EXTENSION_SIZE),
            )
            init = parse_init_response(request(sock, init_request))
            if not init["ok"]:
                raise RuntimeError("upload init failed: {}".format(init["message"]))
            if not init["transfer_id"] or not init["resume_token"]:
                raise ValueError("server returned incomplete recovery credentials")
            if init["offset"] != 0:
                raise ValueError("new upload returned non-zero offset")
            print(
                "initialized transfer_id={} final_name={} token=<hidden>".format(
                    init["transfer_id"], init["final_name"]
                )
            )
            state_path = Path(directory) / "upload_resume_state.json"
            state_path.write_text(
                json.dumps(
                    {
                        "transfer_id": init["transfer_id"],
                        "resume_token": init["resume_token"],
                        "file_name": file_name,
                        "file_size": size_bytes,
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )

            confirmed_offset = 0
            with test_file.open("rb") as source:
                for _ in range(args.blocks_before_disconnect):
                    data = source.read(BLOCK_SIZE)
                    confirmed_offset = send_block(
                        sock, init["transfer_id"], confirmed_offset, data
                    )
            print("intentional disconnect at confirmed_offset={}".format(confirmed_offset))

        time.sleep(0.25)
        saved_state = json.loads(state_path.read_text(encoding="utf-8"))

        with connect(args.host, args.port) as sock:
            resume_request = struct.pack(
                RESUME_RQ_FORMAT,
                UPLOAD_RESUME_RQ,
                fixed_text(saved_state["transfer_id"], TRANSFER_ID_SIZE),
                fixed_text(saved_state["resume_token"], RESUME_TOKEN_SIZE),
                fixed_text(saved_state["file_name"], FILE_NAME_SIZE),
                saved_state["file_size"],
            )
            resumed = parse_resume_response(request(sock, resume_request))
            if not resumed["ok"]:
                raise RuntimeError("upload resume failed: {}".format(resumed["message"]))
            if resumed["transfer_id"] != init["transfer_id"]:
                raise ValueError("UPLOAD_RESUME_RS transfer id mismatch")
            if resumed["offset"] <= 0 or resumed["offset"] > size_bytes:
                raise ValueError("invalid resume offset: {}".format(resumed["offset"]))
            print("resume accepted offset={}".format(resumed["offset"]))

            confirmed_offset = resumed["offset"]
            with test_file.open("rb") as source:
                source.seek(confirmed_offset)
                while confirmed_offset < size_bytes:
                    data = source.read(min(BLOCK_SIZE, size_bytes - confirmed_offset))
                    if not data:
                        raise IOError("local test file ended unexpectedly")
                    confirmed_offset = send_block(
                        sock, init["transfer_id"], confirmed_offset, data
                    )

            finish_request = struct.pack(
                FINISH_RQ_FORMAT,
                UPLOAD_FINISH_RQ,
                fixed_text(init["transfer_id"], TRANSFER_ID_SIZE),
                fixed_text(file_name, FILE_NAME_SIZE),
                size_bytes,
            )
            finish_values = unpack_exact(
                request(sock, finish_request), FINISH_RS_FORMAT, UPLOAD_FINISH_RS
            )
            _, finish_result, final_raw, finish_message_raw = finish_values
            final_name = decode_text(final_raw)
            finish_message = decode_text(finish_message_raw)
            if not finish_result:
                raise RuntimeError("upload finish failed: {}".format(finish_message))
            if not final_name:
                raise ValueError("server returned an empty final file name")

        print("upload completed final_name={}".format(final_name))
        if args.server_media_dir:
            final_path = Path(args.server_media_dir) / final_name
            actual_size = final_path.stat().st_size
            if actual_size != size_bytes:
                raise ValueError(
                    "server file size mismatch: expected={} actual={}".format(
                        size_bytes, actual_size
                    )
                )
            print("server file size verified: {}".format(actual_size))
        else:
            print(
                "protocol completion verified; on Ubuntu run: stat -c %s media/{}".format(
                    final_name
                )
            )
        print("PASS elapsed={:.3f}s".format(time.perf_counter() - started))


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
