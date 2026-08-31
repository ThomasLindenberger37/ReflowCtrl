#!/usr/bin/env python3

import argparse
import ipaddress
import socket
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import ifaddr
from zeroconf import IPVersion, ServiceInfo, Zeroconf

DEFAULT_HOSTNAME = "reflow-ota-server.local."
LEGACY_HOSTNAME = "reflow_ota_server.local."
DEFAULT_PORT = 8070
TRANSFER_CHUNK_SIZE = 64 * 1024


def show_download_progress(transferred: int, total: int) -> None:
    percentage = transferred * 100 / total
    print(
        f"\rDownloading firmware: {percentage:6.2f}% ({transferred:,}/{total:,} bytes)",
        end="",
        flush=True,
    )


def local_ipv4_addresses() -> list[str]:
    addresses: set[str] = set()
    for adapter in ifaddr.get_adapters():
        for interface_address in adapter.ips:
            if not isinstance(interface_address.ip, str):
                continue

            address = ipaddress.IPv4Address(interface_address.ip)
            if (
                address.is_loopback
                or address.is_link_local
                or address.is_unspecified
                or address.is_multicast
                or interface_address.network_prefix >= 31
            ):
                continue
            addresses.add(str(address))

    if not addresses:
        raise SystemExit("No usable IPv4 network interface found")
    return sorted(addresses)


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
        if firmware_size == 0:
            self.send_error(500, "Firmware file is empty")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(firmware_size))
        self.end_headers()

        try:
            transferred = 0
            show_download_progress(transferred, firmware_size)
            with self.server.firmware.open("rb") as firmware_file:
                while chunk := firmware_file.read(TRANSFER_CHUNK_SIZE):
                    self.wfile.write(chunk)
                    transferred += len(chunk)
                    show_download_progress(transferred, firmware_size)
            self.wfile.flush()
            print()
            self.server.download_finished = True
            print(f"Firmware transferred ({firmware_size} bytes); waiting for ESP confirmation")
        except (BrokenPipeError, ConnectionResetError):
            print()
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
    return parser.parse_args()


def register_services(address: str, port: int) -> tuple[Zeroconf, list[ServiceInfo]]:
    services = [
        ServiceInfo(
            "_http._tcp.local.",
            f"ReflowCtrl OTA ({address})._http._tcp.local.",
            addresses=[socket.inet_aton(address)],
            port=port,
            properties={"path": "/firmware.bin"},
            server=DEFAULT_HOSTNAME,
        ),
        ServiceInfo(
            "_http._tcp.local.",
            f"ReflowCtrl OTA legacy ({address})._http._tcp.local.",
            addresses=[socket.inet_aton(address)],
            port=port,
            properties={"path": "/firmware.bin"},
            server=LEGACY_HOSTNAME,
        ),
    ]
    zeroconf = Zeroconf(interfaces=[address], ip_version=IPVersion.V4Only)
    for service in services:
        zeroconf.register_service(service)
    return zeroconf, services


def main() -> None:
    args = parse_args()
    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise SystemExit(f"Firmware not found: {firmware}")

    advertised_addresses = local_ipv4_addresses()
    server = UpdateServer(("0.0.0.0", args.port), firmware)
    server.timeout = 1
    registrations = [register_services(address, args.port) for address in advertised_addresses]
    print(f"Serving {firmware} at http://{DEFAULT_HOSTNAME.rstrip('.')}:{args.port}/firmware.bin")
    print(f"Advertising on {', '.join(advertised_addresses)}; server exits after ESP confirmation")

    try:
        while not server.stop_requested.is_set():
            server.handle_request()
    except KeyboardInterrupt:
        print("Interrupted")
    finally:
        for zeroconf, services in registrations:
            for service in reversed(services):
                zeroconf.unregister_service(service)
            zeroconf.close()
        server.server_close()

    if server.stop_requested.is_set():
        print("ESP confirmed the update; server stopped")


if __name__ == "__main__":
    main()
