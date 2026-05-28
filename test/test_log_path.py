# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
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

        content = log_file.read_text(errors="replace")
        self.assertNotIn("[nanoinit]", content)

    def test_log_format_can_be_set_with_environment(self):
        marker = self.tmp / "log-format.marker"
        log_file = self.tmp / "nanoinit-format.log"
        app = self.write_marker_script("log-format-app.sh", marker, "log-format")
        config = self.write_config("config.json", {"log-format": {"path": str(app)}})

        self.start_nanoinit(
            "-c",
            config,
            f"--log-path={log_file}",
            "-v0",
            env={
                "NI_LOG_FORMAT": "device={device-name} app={app-name} ts={timestamp} msg={message}",
                "DEVICE_NAME": "test-device",
            },
        )
        self.wait_for_contains(marker, "log-format")
        self.wait_for_contains(log_file, "msg=supervisor_start() successfully spawned")

        content = log_file.read_text(errors="replace")
        self.assertIn("device=test-device app=nanoinit ts=", content)

    def test_log_format_can_be_set_with_config(self):
        marker = self.tmp / "config-log-format.marker"
        log_file = self.tmp / "config-log-format.log"
        app = self.write_marker_script("config-log-format-app.sh", marker, "config-log-format")
        config = self.write_config(
            "config.json",
            {
                "config-log-format": {"path": str(app)},
                "ni_log_format": "config={app-name} device={device-name} msg={message}",
            },
        )

        self.start_nanoinit(
            "-c",
            config,
            f"--log-path={log_file}",
            "-v0",
            env={"DEVICE_NAME": "config-device"},
        )
        self.wait_for_contains(marker, "config-log-format")
        self.wait_for_contains(log_file, "msg=supervisor_start() successfully spawned")

        content = log_file.read_text(errors="replace")
        self.assertIn("config=nanoinit device=config-device msg=", content)

    def test_environment_log_format_overrides_config(self):
        marker = self.tmp / "env-over-config-log-format.marker"
        log_file = self.tmp / "env-over-config-log-format.log"
        app = self.write_marker_script("env-over-config-log-format-app.sh", marker, "env-over-config-log-format")
        config = self.write_config(
            "config.json",
            {
                "ni_log_format": "config={message}",
                "env-over-config-log-format": {"path": str(app)},
            },
        )

        self.start_nanoinit(
            "-c",
            config,
            f"--log-path={log_file}",
            "-v0",
            env={"NI_LOG_FORMAT": "env={message}"},
        )
        self.wait_for_contains(marker, "env-over-config-log-format")
        self.wait_for_contains(log_file, "env=supervisor_start() successfully spawned")

        content = log_file.read_text(errors="replace")
        self.assertNotIn("config=supervisor_start()", content)
