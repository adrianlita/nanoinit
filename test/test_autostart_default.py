# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class AutostartDefaultTest(NanoInitTestCase):
    def test_autostart_defaults_to_true(self):
        marker = self.tmp / "autostart-default.marker"
        app = self.write_marker_script("autostart-default-app.sh", marker, "autostart-default")
        config = self.write_config(
            "config.json",
            {
                "default-autostart": {
                    "path": str(app),
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "autostart-default")
