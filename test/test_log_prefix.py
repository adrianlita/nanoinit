# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class LogPrefixTest(NanoInitTestCase):
    def test_prefix_logs_applies_to_files_and_passthrough(self):
        stdout_log = self.tmp / "prefix.stdout.log"
        stderr_log = self.tmp / "prefix.stderr.log"
        app = self.write_file(
            self.tmp / "prefix-app.sh",
            "#!/bin/sh\n"
            "printf 'out-one\\nout-two\\n'\n"
            "printf 'err-one\\nerr-two\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "prefix-files": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stderr": str(stderr_log),
                    "stdout_passthrough": True,
                    "stderr_passthrough": True,
                    "prefix_logs": "[{device-name}:{app-name}:{timestampISO}] ",
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0", env={"DEVICE_NAME": "prefix-device"})
        self.wait_for_contains(stdout_log, "] out-two")
        self.wait_for_contains(stderr_log, "] err-two")

        stdout_content = stdout_log.read_text(errors="replace")
        stderr_content = stderr_log.read_text(errors="replace")
        iso_pattern = r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z"
        self.assertRegex(stdout_content, rf"\[prefix-device:prefix-files:{iso_pattern}\] out-one\n")
        self.assertRegex(stdout_content, rf"\[prefix-device:prefix-files:{iso_pattern}\] out-two\n")
        self.assertRegex(stderr_content, rf"\[prefix-device:prefix-files:{iso_pattern}\] err-one\n")
        self.assertRegex(stderr_content, rf"\[prefix-device:prefix-files:{iso_pattern}\] err-two\n")

        self.wait_for_contains(self.tmp / "nanoinit.stdout", "] out-two")
        self.wait_for_contains(self.tmp / "nanoinit.stderr", "] err-two")

    def test_output_path_placeholders_are_rendered_for_prefixed_files(self):
        app = self.write_file(
            self.tmp / "prefix-placeholder-app.sh",
            "#!/bin/sh\n"
            "printf 'prefixed stdout\\n'\n"
            "printf 'prefixed stderr\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "prefix-placeholder": {
                    "path": str(app),
                    "stdout": str(self.tmp / "prefixed-{device-name}-{app-name}-{timestamp}.out"),
                    "stderr": str(self.tmp / "prefixed-{device-name}-{app-name}-{timestamp}.err"),
                    "stdout_passthrough": True,
                    "stderr_passthrough": True,
                    "prefix_logs": "[{device-name}:{app-name}] ",
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0", env={"DEVICE_NAME": "path-device"})
        self.wait_for(lambda: len(list(self.tmp.glob("prefixed-path-device-prefix-placeholder-*.out"))) == 1, "rendered prefixed stdout log")
        self.wait_for(lambda: len(list(self.tmp.glob("prefixed-path-device-prefix-placeholder-*.err"))) == 1, "rendered prefixed stderr log")

        stdout_logs = list(self.tmp.glob("prefixed-path-device-prefix-placeholder-*.out"))
        stderr_logs = list(self.tmp.glob("prefixed-path-device-prefix-placeholder-*.err"))
        self.assertFileContains(stdout_logs[0], "[path-device:prefix-placeholder] prefixed stdout")
        self.assertFileContains(stderr_logs[0], "[path-device:prefix-placeholder] prefixed stderr")
        self.wait_for_contains(self.tmp / "nanoinit.stdout", "[path-device:prefix-placeholder] prefixed stdout")
        self.wait_for_contains(self.tmp / "nanoinit.stderr", "[path-device:prefix-placeholder] prefixed stderr")

    def test_prefix_logs_applies_to_default_stdout_across_split_lines(self):
        app = self.write_file(
            self.tmp / "prefix-split-app.sh",
            "#!/bin/sh\n"
            "printf '%s' 'split'\n"
            "sleep 0.2\n"
            "printf '%s\\n' '-line'\n"
            "printf '%s\\n' 'second'\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "prefix-split": {
                    "path": str(app),
                    "prefix_logs": "[{device-name}:{app-name}] ",
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0", env={"DEVICE_NAME": "split-device"})
        self.wait_for_contains(self.tmp / "nanoinit.stdout", "[split-device:prefix-split] second")

        lines = (self.tmp / "nanoinit.stdout").read_text(errors="replace").splitlines()
        self.assertIn("[split-device:prefix-split] split-line", lines)
        self.assertIn("[split-device:prefix-split] second", lines)
