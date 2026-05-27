from common import NanoInitTestCase


class OutputPassthroughTest(NanoInitTestCase):
    def test_stdout_and_stderr_passthrough_write_to_file_and_original_streams(self):
        marker = self.tmp / "passthrough.marker"
        stdout_log = self.tmp / "passthrough.stdout"
        stderr_log = self.tmp / "passthrough.stderr"
        app = self.write_file(
            self.tmp / "passthrough-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'visible stdout\\n'\n"
            "printf 'visible stderr\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "passthrough": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stdout_passthrough": True,
                    "stderr": str(stderr_log),
                    "stderr_passthrough": True,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "done")
        self.wait_for_contains(stdout_log, "visible stdout")
        self.wait_for_contains(stderr_log, "visible stderr")
        self.wait_for_contains(self.tmp / "nanoinit.stdout", "visible stdout")
        self.wait_for_contains(self.tmp / "nanoinit.stderr", "visible stderr")

    def test_stdout_passthrough_works_with_rotation(self):
        stdout_log = self.tmp / "passthrough-rotate.stdout"
        app = self.write_file(
            self.tmp / "passthrough-rotate-app.sh",
            "#!/bin/sh\n"
            "printf 'rotated passthrough line\\n'\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "passthrough-rotate": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stdout_passthrough": True,
                    "stdout_rotate_size": 10,
                    "stdout_rotate_count": 1,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(f"{stdout_log}.1", "rotated passthrough line", timeout=8)
        self.wait_for_contains(self.tmp / "nanoinit.stdout", "rotated passthrough line")
