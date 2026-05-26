import time

from common import NanoInitTestCase


class ManualModeFlagTest(NanoInitTestCase):
    def test_manual_mode_flag_skips_manual_apps(self):
        normal_marker = self.tmp / "normal.marker"
        manual_marker = self.tmp / "manual.marker"
        normal_app = self.write_marker_script("normal-app.sh", normal_marker, "normal")
        manual_app = self.write_marker_script("manual-app.sh", manual_marker, "manual")
        config = self.write_config(
            "config.json",
            {
                "normal": {"path": str(normal_app)},
                "manual": {"path": str(manual_app), "manual": True},
            },
        )

        self.start_nanoinit("-c", config, "--manual-mode", "-v0")
        self.wait_for_contains(normal_marker, "normal")
        time.sleep(0.3)
        self.assertFalse(manual_marker.exists())
