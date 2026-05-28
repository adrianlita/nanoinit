# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
import time

from common import NanoInitTestCase


class AutostartFalseTest(NanoInitTestCase):
    def test_autostart_false_skips_application(self):
        skipped_marker = self.tmp / "autostart-false.marker"
        started_marker = self.tmp / "autostart-true.marker"
        skipped_app = self.write_marker_script("autostart-false-app.sh", skipped_marker, "skipped")
        started_app = self.write_marker_script("autostart-true-app.sh", started_marker, "started")
        config = self.write_config(
            "config.json",
            {
                "skipped": {"path": str(skipped_app), "autostart": False},
                "started": {"path": str(started_app), "autostart": True},
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(started_marker, "started")
        time.sleep(0.3)
        self.assertFalse(skipped_marker.exists())
