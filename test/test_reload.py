# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class ReloadTest(NanoInitTestCase):
    def test_reload_reloads_configuration(self):
        v1_marker = self.tmp / "v1.marker"
        v2_marker = self.tmp / "v2.marker"
        app_v1 = self.write_file(
            self.tmp / "app-v1.sh",
            "#!/bin/sh\n"
            f"printf 'v1\\n' > '{v1_marker}'\n"
            "sleep 30\n",
            executable=True,
        )
        app_v2 = self.write_file(
            self.tmp / "app-v2.sh",
            "#!/bin/sh\n"
            f"printf 'v2\\n' > '{v2_marker}'\n"
            "sleep 30\n",
            executable=True,
        )
        config = self.write_config("config.json", {"reload-app": {"path": str(app_v1)}})

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(v1_marker, "v1")

        self.write_config("config.json", {"reload-app": {"path": str(app_v2)}})
        self.reload_nanoinit()
        self.wait_for_contains(v2_marker, "v2", timeout=8)
