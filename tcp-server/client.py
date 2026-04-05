import socket, struct

def send_msg(sock, payload: bytes):
    sock.sendall(struct.pack(">I", len(payload)) + payload)

def recv_msg(sock) -> bytes:
    hdr = sock.recv(4, socket.MSG_WAITALL)
    length = struct.unpack(">I", hdr)[0]
    return sock.recv(length, socket.MSG_WAITALL)

with socket.create_connection(("localhost", 9090)) as s:
    send_msg(s, b"log_entry: txn_id=42 data=hello")
    ack = recv_msg(s)
    print("ACK:", ack.decode())