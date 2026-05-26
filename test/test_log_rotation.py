from pathlib import Path

from common import NanoInitTestCase


class LogRotationTest(NanoInitTestCase):
    def test_stdout_and_stderr_log_rotation(self):
        stdout_log = self.tmp / "app.out"
        stderr_log = self.tmp / "app.err"
        app = self.write_file(
            self.tmp / "rotate-app.sh",
            "#!/bin/sh\n"
            "i=0\n"
            "while [ \"$i\" -lt 80 ]; do\n"
            "    printf '012345678901234567890123456789\\n'\n"
            "    printf 'abcdefghijklmnopqrstuvwxyz123456\\n' >&2\n"
            "    i=$((i + 1))\n"
            "done\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "rotate": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stdout_rotate_size": 128,
                    "stdout_rotate_count": 2,
                    "stderr": str(stderr_log),
                    "stderr_rotate_size": 128,
                    "stderr_rotate_count": 2,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_file(f"{stdout_log}.2", timeout=8)
        self.wait_for_file(f"{stderr_log}.2", timeout=8)

        for path in [
            stdout_log,
            f"{stdout_log}.1",
            f"{stdout_log}.2",
            stderr_log,
            f"{stderr_log}.1",
            f"{stderr_log}.2",
        ]:
            self.assertTrue(Path(path).is_file(), f"expected file to exist: {path}")
            self.assertFileSizeLessEqual(path, 128)
