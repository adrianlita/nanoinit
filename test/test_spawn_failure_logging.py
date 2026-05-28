# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class SpawnFailureLoggingTest(NanoInitTestCase):
    def test_exec_failure_is_logged_as_nanoinit_not_app_output(self):
        missing_app = self.tmp / "missing-app"
        config = self.write_config(
            "config.json",
            {
                "mediator": {
                    "path": str(missing_app),
                    "prefix_logs": "[{device-name}:{app-name}] ",
                }
            },
        )

        self.start_nanoinit(
            "-c",
            config,
            "-v2",
            env={
                "DEVICE_NAME": "spawn-device",
                "NI_LOG_FORMAT": "[{app-name}] {message}",
            },
        )

        stderr_path = self.tmp / "nanoinit.stderr"
        self.wait_for_contains(stderr_path, "[nanoinit] supervisor_spawn() failed to spawn process")

        stderr = stderr_path.read_text(errors="replace")
        self.assertIn(str(missing_app), stderr)
        self.assertNotIn("[spawn-device:mediator] supervisor_spawn()", stderr)
        self.assertNotIn("[mediator] supervisor_spawn()", stderr)
