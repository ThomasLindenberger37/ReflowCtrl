import http.client
import tempfile
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from ota_server.server import (
    UpdateServer,
    format_byte_count,
    format_duration,
    local_ipv4_addresses,
    source_address_for,
)


class OtaServerFormattingTest(unittest.TestCase):
    def test_formats_byte_counts(self) -> None:
        self.assertEqual(format_byte_count(512), "512 B")
        self.assertEqual(format_byte_count(1536), "1.50 KiB")

    def test_formats_durations(self) -> None:
        self.assertEqual(format_duration(65), "01:05")
        self.assertEqual(format_duration(3661), "1:01:01")

    def test_selects_physical_ipv4_interfaces(self) -> None:
        adapters = [
            SimpleNamespace(
                name="eth0", ips=[SimpleNamespace(ip="192.168.1.20", network_prefix=24)]
            ),
            SimpleNamespace(
                name="docker0", ips=[SimpleNamespace(ip="172.17.0.1", network_prefix=16)]
            ),
            SimpleNamespace(name="lo", ips=[SimpleNamespace(ip="127.0.0.1", network_prefix=8)]),
        ]
        with patch("ota_server.server.ifaddr.get_adapters", return_value=adapters):
            self.assertEqual(local_ipv4_addresses(), ["192.168.1.20"])

    def test_determines_source_address_from_route(self) -> None:
        self.assertEqual(source_address_for("127.0.0.1"), "127.0.0.1")


class OtaServerHttpTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.firmware = Path(self.temporary_directory.name) / "firmware.bin"
        self.firmware.write_bytes(b"test firmware")
        self.server = UpdateServer(("127.0.0.1", 0), self.firmware)

    def tearDown(self) -> None:
        self.server.server_close()
        self.temporary_directory.cleanup()

    def perform_request(self, method: str, path: str) -> tuple[int, bytes]:
        server_thread = threading.Thread(target=self.server.handle_request)
        server_thread.start()
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=2)
        try:
            connection.request(method, path)
            response = connection.getresponse()
            return response.status, response.read()
        finally:
            connection.close()
            server_thread.join(timeout=2)

    def test_serves_firmware(self) -> None:
        status, body = self.perform_request("GET", "/firmware.bin")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"test firmware")
        self.assertTrue(self.server.download_finished)

    def test_rejects_completion_before_download(self) -> None:
        status, _ = self.perform_request("POST", "/complete")
        self.assertEqual(status, 409)

    def test_accepts_completion_after_download(self) -> None:
        self.perform_request("GET", "/firmware.bin")
        status, _ = self.perform_request("POST", "/complete")
        self.assertEqual(status, 200)
        self.assertTrue(self.server.stop_requested.is_set())

    def test_rejects_unknown_path(self) -> None:
        status, _ = self.perform_request("GET", "/missing")
        self.assertEqual(status, 404)

    def test_rejects_unknown_post_path(self) -> None:
        status, _ = self.perform_request("POST", "/missing")
        self.assertEqual(status, 404)

    def test_rejects_empty_firmware(self) -> None:
        self.firmware.write_bytes(b"")
        status, _ = self.perform_request("GET", "/firmware.bin")
        self.assertEqual(status, 500)


if __name__ == "__main__":
    unittest.main()
