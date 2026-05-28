import os
import subprocess
import time

from common import BIN, NanoInitTestCase


class ControlCommandsTest(NanoInitTestCase):
    def run_control(self, *args):
        env = os.environ.copy()
        env["NANOINIT_CONTROL_SOCKET"] = str(self.control_socket)
        return subprocess.run(
            [str(BIN), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
            timeout=3,
            check=False,
        )

    def setUp(self):
        super().setUp()
        self.control_socket = self.tmp / "nanoinit.sock"

    def start_with_control_socket(self, config):
        self.start_nanoinit(
            "-c",
            config,
            "-v0",
            env={"NANOINIT_CONTROL_SOCKET": str(self.control_socket)},
        )
        self.wait_for(lambda: self.control_socket.exists(), "control socket")

    def test_list_status_start_and_stop_application(self):
        marker = self.tmp / "manual-start.marker"
        stopped_marker = self.tmp / "manual-start.stopped"
        app = self.write_file(
            self.tmp / "manual-start-app.sh",
            "#!/bin/sh\n"
            f"printf 'started\\n' > '{marker}'\n"
            f"trap \"printf 'stopped\\n' > '{stopped_marker}'; exit 0\" TERM\n"
            "while true; do sleep 1; done\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "manual-start": {
                    "path": str(app),
                    "autostart": False,
                }
            },
        )
        self.start_with_control_socket(config)

        list_result = self.run_control("list")
        self.assertEqual(0, list_result.returncode, list_result.stderr)
        self.assertIn("manual-start\tstopped", list_result.stdout)

        ls_result = self.run_control("ls")
        self.assertEqual(0, ls_result.returncode, ls_result.stderr)
        self.assertIn("manual-start\tstopped", ls_result.stdout)

        status_result = self.run_control("status", "manual-start")
        self.assertEqual(0, status_result.returncode, status_result.stderr)
        self.assertIn("status: stopped", status_result.stdout)
        self.assertIn("uptime: -", status_result.stdout)

        start_result = self.run_control("start", "manual-start")
        self.assertEqual(0, start_result.returncode, start_result.stderr)
        self.wait_for_contains(marker, "started")

        status_result = self.run_control("status", "manual-start")
        self.assertEqual(0, status_result.returncode, status_result.stderr)
        self.assertIn("status: running", status_result.stdout)
        self.assertIn("pid: ", status_result.stdout)
        self.assertIn("uptime: ", status_result.stdout)

        stop_result = self.run_control("stop", "manual-start")
        self.assertEqual(0, stop_result.returncode, stop_result.stderr)
        self.wait_for_contains(stopped_marker, "stopped")
        self.wait_for(
            lambda: "status: stopped" in self.run_control("status", "manual-start").stdout,
            "manual-start to report stopped",
        )

    def test_stop_suppresses_autorestart_until_start_command(self):
        starts = self.tmp / "autorestart-control.starts"
        app = self.write_file(
            self.tmp / "autorestart-control-app.sh",
            "#!/bin/sh\n"
            f"count=0\n"
            f"if [ -f '{starts}' ]; then count=$(cat '{starts}'); fi\n"
            "count=$((count + 1))\n"
            f"printf '%s\\n' \"$count\" > '{starts}'\n"
            "while true; do sleep 1; done\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "autorestart-control": {
                    "path": str(app),
                    "autorestart": True,
                }
            },
        )
        self.start_with_control_socket(config)
        self.wait_for_contains(starts, "1")

        stop_result = self.run_control("stop", "autorestart-control")
        self.assertEqual(0, stop_result.returncode, stop_result.stderr)
        self.wait_for(
            lambda: "status: stopped" in self.run_control("status", "autorestart-control").stdout,
            "autorestart-control to report stopped",
        )
        time.sleep(1)
        self.assertEqual("1", starts.read_text().strip())

        start_result = self.run_control("start", "autorestart-control")
        self.assertEqual(0, start_result.returncode, start_result.stderr)
        self.wait_for_contains(starts, "2")

    def test_unknown_application_returns_error(self):
        config = self.write_config("config.json", {})
        self.start_with_control_socket(config)

        status_result = self.run_control("status", "missing")
        self.assertNotEqual(0, status_result.returncode)
        self.assertIn("app not found: missing", status_result.stderr)
