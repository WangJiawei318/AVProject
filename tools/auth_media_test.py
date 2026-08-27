#!/usr/bin/env python3
"""Stage 11 protocol smoke test using only the Python standard library."""

import argparse
import secrets
import socket
import struct
import sys


PACK_BASE = 20000
LOGIN_RQ = PACK_BASE + 3
LOGIN_RS = PACK_BASE + 4
MEDIA_LIST_RQ = PACK_BASE + 5
MEDIA_LIST_RS = PACK_BASE + 6
REGISTER_RQ = PACK_BASE + 21
REGISTER_RS = PACK_BASE + 22

ERROR_USERNAME_EXISTS = 1
ERROR_INVALID_USERNAME_OR_PASSWORD = 2
ERROR_AUTH_REQUIRED = 4
SCOPE_PUBLIC = 1
SCOPE_MINE = 2

NAME_SIZE = 33
PASSWORD_SIZE = 65
KEYWORD_SIZE = 128
TEXT_SIZE = 128
MAX_FRAME_SIZE = 256 * 1024

AUTH_RQ_FORMAT = "<i33s65s"
REGISTER_RS_FORMAT = "<iiiQ128s"
LOGIN_RS_FORMAT = "<iiiQ33s128s"
MEDIA_LIST_RQ_FORMAT = "<iiii128s"
MEDIA_LIST_RS_HEADER_FORMAT = "<iiii128s"


def fixed_text(value, size):
    encoded = value.encode("utf-8")
    if len(encoded) >= size:
        raise ValueError("text is too long for a {} byte field".format(size))
    return encoded + b"\0" * (size - len(encoded))


def decode_text(value):
    return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")


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
    sock.settimeout(15.0)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def register(sock, username, password):
    packet = request(
        sock,
        struct.pack(
            AUTH_RQ_FORMAT,
            REGISTER_RQ,
            fixed_text(username, NAME_SIZE),
            fixed_text(password, PASSWORD_SIZE),
        ),
    )
    _, result, error_code, user_id, message = unpack_exact(
        packet, REGISTER_RS_FORMAT, REGISTER_RS
    )
    return result != 0, error_code, user_id, decode_text(message)


def login(sock, username, password):
    packet = request(
        sock,
        struct.pack(
            AUTH_RQ_FORMAT,
            LOGIN_RQ,
            fixed_text(username, NAME_SIZE),
            fixed_text(password, PASSWORD_SIZE),
        ),
    )
    _, result, error_code, user_id, returned_name, message = unpack_exact(
        packet, LOGIN_RS_FORMAT, LOGIN_RS
    )
    return (
        result != 0,
        error_code,
        user_id,
        decode_text(returned_name),
        decode_text(message),
    )


def media_list(sock, scope):
    body = struct.pack(
        MEDIA_LIST_RQ_FORMAT,
        MEDIA_LIST_RQ,
        scope,
        1,
        100,
        fixed_text("", KEYWORD_SIZE),
    )
    packet = request(sock, body)
    header_size = struct.calcsize(MEDIA_LIST_RS_HEADER_FORMAT)
    if len(packet) < header_size:
        raise ValueError("short MEDIA_LIST_RS")
    header = struct.unpack(MEDIA_LIST_RS_HEADER_FORMAT, packet[:header_size])
    packet_type, result, error_code, payload_size, message = header
    if packet_type != MEDIA_LIST_RS:
        raise ValueError("unexpected MEDIA_LIST_RS type: {}".format(packet_type))
    if payload_size < 0 or len(packet) != header_size + payload_size:
        raise ValueError("invalid MEDIA_LIST_RS payload size")
    payload = packet[header_size:].decode("utf-8", errors="strict")
    return result != 0, error_code, payload, decode_text(message)


def require(condition, description):
    if not condition:
        raise AssertionError(description)
    print("PASS: {}".format(description))


def run(args):
    suffix = secrets.token_hex(4)
    user_a = "avtest_{}_a".format(suffix)
    user_b = "avtest_{}_b".format(suffix)
    password = "T9!" + secrets.token_hex(8)
    wrong_password = password + "x"

    unauthenticated = connect(args.host, args.port)
    try:
        ok, code, _, _ = media_list(unauthenticated, SCOPE_PUBLIC)
        require(not ok and code == ERROR_AUTH_REQUIRED,
                "unauthenticated MEDIA_LIST returns AUTH_REQUIRED")
    finally:
        unauthenticated.close()

    client_a = connect(args.host, args.port)
    try:
        ok, code, user_a_id, _ = register(client_a, user_a, password)
        require(ok and code == 0 and user_a_id > 0, "register userA")

        ok, code, _, _ = register(client_a, user_a, password)
        require(not ok and code == ERROR_USERNAME_EXISTS,
                "duplicate userA registration is rejected")

        ok, code, _, _, _ = login(client_a, user_a, wrong_password)
        require(not ok and code == ERROR_INVALID_USERNAME_OR_PASSWORD,
                "wrong password is rejected")

        ok, code, _, _, _ = login(client_a, "' OR 1=1 --", password)
        require(not ok and code == ERROR_INVALID_USERNAME_OR_PASSWORD,
                "SQL injection style username cannot bypass login")

        ok, code, login_id, returned_name, _ = login(client_a, user_a, password)
        require(ok and code == 0 and login_id == user_a_id and returned_name == user_a,
                "correct userA login")

        ok, code, _, _ = media_list(client_a, SCOPE_PUBLIC)
        require(ok and code == 0, "authenticated PUBLIC media list")
        ok, code, mine_payload, _ = media_list(client_a, SCOPE_MINE)
        require(ok and code == 0, "authenticated userA MINE media list")
    finally:
        client_a.close()

    client_b = connect(args.host, args.port)
    try:
        ok, code, user_b_id, _ = register(client_b, user_b, password)
        require(ok and code == 0 and user_b_id > 0, "register userB")
        ok, code, login_id, returned_name, _ = login(client_b, user_b, password)
        require(ok and code == 0 and login_id == user_b_id and returned_name == user_b,
                "correct userB login")
        ok, code, user_b_mine, _ = media_list(client_b, SCOPE_MINE)
        require(ok and code == 0, "authenticated userB MINE media list")
    finally:
        client_b.close()

    print("userA={} userB={}".format(user_a, user_b))
    print("userA MINE rows={} userB MINE rows={}".format(
        len([line for line in mine_payload.splitlines() if line]),
        len([line for line in user_b_mine.splitlines() if line]),
    ))
    print("Stage 11 authentication/media smoke test passed.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.44.130")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    try:
        run(args)
    except (OSError, ConnectionError, ValueError, AssertionError) as error:
        print("FAIL: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
