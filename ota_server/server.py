#!/usr/bin/env python3

import argparse
import socket
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

from zeroconf import IPVersion, ServiceInfo, Zeroconf

DEFAULT_HOSTNAME = "reflow-ota-server.local."
LEGACY_HOSTNAME = "reflow_ota_server.local."
DEFAULT_PORT = 8070


def local_ipv4_address() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.connect(("192.0.2.1", 9))
            return str(probe.getsockname()[0])
        except OSError:
            return socket.gethostbyname(socket.gethostname())


class UpdateServer(HTTPServer):
    def __init__(self, address: tuple[str, int], firmware: Path) -> None:
        super().__init__(address, UpdateRequestHandler)
        self.firmware = firmware
        self.download_finished = False
        self.stop_requested = threading.Event()


class UpdateRequestHandler(BaseHTTPRequestHandler):
    server: UpdateServer

    def do_GET(self) -> None:  # noqa: N802
        if self.path != "/firmware.bin":
            self.send_error(404)
            return

        firmware_size = self.server.firmware.stat().st_size
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(firmware_size))
        self.end_headers()

        try:
            with self.server.firmware.open("rb") as firmware_file:
                while chunk := firmware_file.read(64 * 1024):
                    self.wfile.write(chunk)
            self.wfile.flush()
            self.server.download_finished = True
            print(f"Firmware transferred ({firmware_size} bytes); waiting for ESP confirmation")
        except (BrokenPipeError, ConnectionResetError):
            self.server.download_finished = False
            print("ESP disconnected before the transfer completed")

    def do_POST(self) -> None:  # noqa: N802
        if self.path != "/complete":
            self.send_error(404)
            return
        if not self.server.download_finished:
            self.send_error(409, "No completed firmware transfer")
            return

        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()
        self.wfile.flush()
        self.server.stop_requested.set()

    def log_message(self, message: str, *args: object) -> None:
        print(f"{self.client_address[0]} - {message % args}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve one ReflowCtrl OTA firmware update")
    parser.add_argument(
        "firmware",
        nargs="?",
        type=Path,
        default=Path("build/reflowCtrl.bin"),
        help="firmware binary (default: build/reflowCtrl.bin)",
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--address", default=None, help="IPv4 address advertised over mDNS")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise SystemExit(f"Firmware not found: {firmware}")

    advertised_address = args.address or local_ipv4_address()
    server = UpdateServer(("0.0.0.0", args.port), firmware)
    server.timeout = 1
    service = ServiceInfo(
        "_http._tcp.local.",
        "ReflowCtrl OTA._http._tcp.local.",
        addresses=[socket.inet_aton(advertised_address)],
        port=args.port,
        properties={"path": "/firmware.bin"},
        server=DEFAULT_HOSTNAME,
    )
    legacy_service = ServiceInfo(
        "_http._tcp.local.",
        "ReflowCtrl OTA legacy._http._tcp.local.",
        addresses=[socket.inet_aton(advertised_address)],
        port=args.port,
        properties={"path": "/firmware.bin"},
        server=LEGACY_HOSTNAME,
    )

    zeroconf = Zeroconf(interfaces=[advertised_address], ip_version=IPVersion.V4Only)
    zeroconf.register_service(service)
    zeroconf.register_service(legacy_service)
    print(f"Serving {firmware} at http://{DEFAULT_HOSTNAME.rstrip('.')}:{args.port}/firmware.bin")
    print(f"Advertising {advertised_address}; server exits after ESP confirmation")

    try:
        while not server.stop_requested.is_set():
            server.handle_request()
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        zeroconf.unregister_service(legacy_service)
        zeroconf.unregister_service(service)
        zeroconf.close()
        server.server_close()

    if server.stop_requested.is_set():
        print("ESP confirmed the update; server stopped")


if __name__ == "__main__":
    main()
