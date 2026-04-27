#!/usr/bin/env python3
"""
DOSBox-X nullmodem-to-PTY bridge.

DOSBox-X's directserial backend fails on PTY devices because
TIOCMGET/TIOCMSET ioctls are unsupported on PTYs.  The nullmodem
backend works over plain TCP, but uses a lightweight escape protocol:

  0xFF 0xNN  = modem control update (not a data byte)
  0xFF 0xFF  = escaped literal 0xFF data byte
  <other>    = raw data byte

This bridge accepts DOSBox-X's TCP nullmodem connection, strips the
modem-control escapes, and exposes a clean bidirectional byte stream
through a PTY slave at /tmp/linux-com1.  Existing consumers
(echo_server.py, the Phase 4 daemon) open /tmp/linux-com1 via
pyserial unchanged.

Usage: python3 nullmodem_bridge.py [--port PORT] [--baud BAUD] [--link PATH]
"""

import sys
import os
import pty
import socket
import threading
import select
import argparse

DEFAULT_PORT = 5555
DEFAULT_BAUD = 9600
DEFAULT_LINK = '/tmp/linux-com1'


def set_raw(fd, baud):
    """Put a PTY master fd into raw mode at the requested baud."""
    import termios
    attrs = termios.tcgetattr(fd)
    import tty
    tty.setraw(fd)
    # Set baud rate (best-effort; PTYs often ignore it but be explicit)
    baud_map = {
        9600:   termios.B9600,
        19200:  termios.B19200,
        38400:  termios.B38400,
        57600:  termios.B57600,
        115200: termios.B115200,
    }
    b = baud_map.get(baud, termios.B9600)
    attrs[4] = b  # ispeed
    attrs[5] = b  # ospeed
    try:
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        pass  # PTYs may silently ignore baud setting


def handle_connection(conn, master_fd, stop_event):
    """Bridge one DOSBox-X TCP connection ↔ PTY master."""

    def tcp_to_pty():
        """Read from TCP, strip nullmodem escapes, write to PTY master."""
        prev_ff = False
        while not stop_event.is_set():
            try:
                ready, _, _ = select.select([conn], [], [], 0.2)
                if not ready:
                    continue
                data = conn.recv(256)
            except Exception:
                break
            if not data:
                break
            buf = bytearray()
            for b in data:
                if prev_ff:
                    if b == 0xFF:
                        buf.append(0xFF)   # escaped literal
                    # else: modem-control byte — discard
                    prev_ff = False
                elif b == 0xFF:
                    prev_ff = True
                else:
                    buf.append(b)
            if buf:
                try:
                    os.write(master_fd, bytes(buf))
                except OSError:
                    break
        stop_event.set()

    def pty_to_tcp():
        """Read from PTY master, escape 0xFF, write to TCP."""
        while not stop_event.is_set():
            try:
                ready, _, _ = select.select([master_fd], [], [], 0.2)
                if not ready:
                    continue
                data = os.read(master_fd, 256)
            except OSError:
                break
            if not data:
                break
            # Escape 0xFF bytes so DOSBox-X won't misread them as control
            escaped = bytearray()
            for b in data:
                if b == 0xFF:
                    escaped.extend(b'\xFF\xFF')
                else:
                    escaped.append(b)
            try:
                conn.sendall(bytes(escaped))
            except Exception:
                break
        stop_event.set()

    t1 = threading.Thread(target=tcp_to_pty, daemon=True)
    t2 = threading.Thread(target=pty_to_tcp, daemon=True)
    t1.start()
    t2.start()
    t1.join()
    t2.join()


def main():
    parser = argparse.ArgumentParser(description='DOSBox-X nullmodem → PTY bridge')
    parser.add_argument('--port', type=int, default=DEFAULT_PORT,
                        help='TCP port to listen on (default: %(default)s)')
    parser.add_argument('--baud', type=int, default=DEFAULT_BAUD,
                        help='Baud rate hint for PTY (default: %(default)s)')
    parser.add_argument('--link', default=DEFAULT_LINK,
                        help='Symlink path for PTY slave (default: %(default)s)')
    args = parser.parse_args()

    # Create PTY pair
    master_fd, slave_fd = pty.openpty()
    slave_path = os.ttyname(slave_fd)
    set_raw(master_fd, args.baud)

    # Symlink slave to the expected path
    try:
        os.unlink(args.link)
    except FileNotFoundError:
        pass
    os.symlink(slave_path, args.link)
    print(f'PTY slave: {slave_path} → {args.link}', flush=True)

    # Keep slave_fd open so the slave doesn't report EIO on open
    # (the consumer pyserial will open it separately; keeping our fd
    #  ensures the PTY stays alive when the consumer cycles open/close)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', args.port))
    server.listen(1)
    print(f'Listening on TCP 127.0.0.1:{args.port}', flush=True)

    try:
        while True:
            server.settimeout(None)
            conn, addr = server.accept()
            print(f'DOSBox-X connected from {addr}', flush=True)
            stop_event = threading.Event()
            handle_connection(conn, master_fd, stop_event)
            conn.close()
            print('DOSBox-X disconnected', flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            os.unlink(args.link)
        except Exception:
            pass
        os.close(master_fd)
        os.close(slave_fd)
        server.close()


if __name__ == '__main__':
    main()
