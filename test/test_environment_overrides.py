# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class EnvironmentOverridesTest(NanoInitTestCase):
    def test_environment_overrides_config_location(self):
        marker = self.tmp / "env.marker"
        app = self.write_marker_script("env-app.sh", marker, "env")
        config = self.write_config(
            "config.json",
            {
                "actual": {
                    "env-app": {
                        "path": str(app),
                    }
                }
            },
        )

        self.start_nanoinit(
            "-c",
            self.tmp / "missing.json",
            "-j",
            "/wrong",
            "-v0",
            env={
                "NANOINIT_CONFIG_FILE": str(config),
                "NANOINIT_CONFIG_JSON_OBJECT": "/actual",
            },
        )
        self.wait_for_contains(marker, "env")
