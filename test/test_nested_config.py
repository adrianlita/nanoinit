from common import NanoInitTestCase


class NestedConfigTest(NanoInitTestCase):
    def test_nested_config_object(self):
        marker = self.tmp / "nested.marker"
        app = self.write_marker_script("nested-app.sh", marker, "nested")
        config = self.write_config(
            "config.json",
            {
                "ignored": {"app": {"path": "/bin/false"}},
                "nanoinit-rules": {"nested": {"path": str(app)}},
            },
        )

        self.start_nanoinit("-c", config, "-j", "/nanoinit-rules", "-v0")
        self.wait_for_contains(marker, "nested")
