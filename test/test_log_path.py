from common import NanoInitTestCase


class LogPathTest(NanoInitTestCase):
    def test_log_path_receives_nanoinit_logs(self):
        marker = self.tmp / "log-path.marker"
        log_file = self.tmp / "nanoinit.log"
        app = self.write_marker_script("log-path-app.sh", marker, "log-path")
        config = self.write_config("config.json", {"log-path": {"path": str(app)}})

        self.start_nanoinit("-c", config, f"--log-path={log_file}", "-v0")
        self.wait_for_contains(marker, "log-path")
        self.wait_for_contains(log_file, "successfully spawned")
