import time

from common import NanoInitTestCase


class ManualModeEnvironmentTest(NanoInitTestCase):
    def test_manual_mode_environment_skips_manual_apps(self):
        normal_marker = self.tmp / "normal-env.marker"
        manual_marker = self.tmp / "manual-env.marker"
        normal_app = self.write_marker_script("normal-env-app.sh", normal_marker, "normal-env")
        manual_app = self.write_marker_script("manual-env-app.sh", manual_marker, "manual-env")
        config = self.write_config(
            "config.json",
            {
                "normal": {"path": str(normal_app)},
                "manual": {"path": str(manual_app), "manual": True},
            },
        )

        self.start_nanoinit("-c", config, "-v0", env={"NANOINIT_MANUAL_MODE": "1"})
        self.wait_for_contains(normal_marker, "normal-env")
        time.sleep(0.3)
        self.assertFalse(manual_marker.exists())
