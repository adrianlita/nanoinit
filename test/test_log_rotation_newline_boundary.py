# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from pathlib import Path

from common import NanoInitTestCase


class LogRotationNewlineBoundaryTest(NanoInitTestCase):
    def test_rotation_waits_for_newline_after_size_limit(self):
        stdout_log = self.tmp / "newline-boundary.out"
        rotated_log = Path(f"{stdout_log}.1")
        marker = self.tmp / "newline-boundary.marker"
        first_message = "0123456789ABCDEFGHIJ"
        app = self.write_file(
            self.tmp / "newline-boundary-app.sh",
            "#!/bin/sh\n"
            f"printf '%s' '{first_message}'\n"
            f"touch '{marker}'\n"
            "sleep 3\n"
            "printf '\\n'\n"
            "printf 'ok\\n'\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "newline-boundary": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stdout_rotate_size": 10,
                    "stdout_rotate_count": 1,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_file(marker, timeout=5)
        self.wait_for_contains(stdout_log, first_message, timeout=5)
        self.assertFalse(rotated_log.exists())

        self.wait_for_contains(rotated_log, f"{first_message}\n", timeout=6)
        self.wait_for_contains(stdout_log, "ok\n", timeout=6)
        self.assertEqual(f"{first_message}\n", rotated_log.read_text())
