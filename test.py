#!/usr/bin/python3

import re
import socket
import struct
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server = ("127.0.0.1", 10001)

def bs(b):
    return re.sub("(..)", r"\1 ", re.sub("(........)", r"\1  ", b.hex())).replace("    ", "  ")

def speak(*values, ps=1000):
    data = b"".join(struct.pack("<L", v) for v in values) + b"." * ps
    print(bs(data))
    sock.sendto(data, server)

V = 0
BLOCK, CONTROL = 0, 1
UNSUB, SUB = 0, 1

if sys.argv[1] == "sub":
    print("gsn:", sock.getsockname())
    speak(V, CONTROL, SUB, 123)
    print("gsn:", sock.getsockname())

    while True:
        data, (host, port) = sock.recvfrom(99999)
        print(f"{host}:{port} -> {bs(data)}")

elif sys.argv[1] == "publish":
    speak(V, BLOCK, 123, 456, 1555, 0, ps=1376)
    speak(V, BLOCK, 123, 456, 1555, 1, ps=1555-1376)

