import time

from common import NanoInitTestCase


class DevNullRedirectionTest(NanoInitTestCase):
    def test_empty_stdout_and_stderr_redirect_to_dev_null(self):
        marker = self.tmp / "null.marker"
        app = self.write_file(
            self.tmp / "null-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'SHOULD_NOT_APPEAR_STDOUT\\n'\n"
            "printf 'SHOULD_NOT_APPEAR_STDERR\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "null-output": {
                    "path": str(app),
                    "stdout": "",
                    "stdout_passthrough": True,
                    "stderr": "",
                    "stderr_passthrough": True,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "done")
        time.sleep(0.3)
        self.assertFileNotContains(self.tmp / "nanoinit.stdout", "SHOULD_NOT_APPEAR_STDOUT")
        self.assertFileNotContains(self.tmp / "nanoinit.stderr", "SHOULD_NOT_APPEAR_STDERR")
