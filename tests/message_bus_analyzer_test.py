from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[1] / "tools" / "analyze_message_bus_subscriptions.py"


class MessageBusAnalyzerTest(unittest.TestCase):
    def test_fails_when_static_subscription_count_exceeds_capacity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_directory = root / "main"
            source_directory.mkdir()
            header = source_directory / "message_bus.hpp"
            header.write_text("static constexpr std::size_t MAX_SUBSCRIPTIONS = 10;\n")
            source = source_directory / "subscriptions.cpp"
            source.write_text(
                "\n".join("bus.subscribe<ButtonPressed>(&handler, this);" for _ in range(11))
            )

            result = subprocess.run(
                [
                    "python3",
                    str(SCRIPT),
                    "--source",
                    str(source_directory),
                    "--message-bus-header",
                    str(header),
                ],
                check=False,
                capture_output=True,
                text=True,
            )

        self.assertEqual(result.returncode, 1)
        self.assertIn("Total", result.stdout)
        self.assertIn("11 / 10", result.stdout)
        self.assertIn("capacity exceeded", result.stderr)


if __name__ == "__main__":
    unittest.main()
