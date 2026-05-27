from pathlib import Path

from common import NanoInitTestCase


class LogRotationTest(NanoInitTestCase):
    def test_stdout_and_stderr_log_rotation(self):
        stdout_log = self.tmp / "app.out"
        stderr_log = self.tmp / "app.err"
        stdout_line = b"012345678901234567890123456789\n"
        stderr_line = b"abcdefghijklmnopqrstuvwxyz123456\n"
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

        for path, expected_line in [
            (stdout_log, stdout_line),
            (f"{stdout_log}.1", stdout_line),
            (f"{stdout_log}.2", stdout_line),
            (stderr_log, stderr_line),
            (f"{stderr_log}.1", stderr_line),
            (f"{stderr_log}.2", stderr_line),
        ]:
            path = Path(path)
            self.assertTrue(path.is_file(), f"expected file to exist: {path}")
            content = path.read_bytes()
            if content:
                self.assertTrue(content.endswith(b"\n"), f"expected file to end after a full log line: {path}")
                for line in content.splitlines(keepends=True):
                    self.assertEqual(expected_line, line)

        for path in [
            f"{stdout_log}.1",
            f"{stdout_log}.2",
            f"{stderr_log}.1",
            f"{stderr_log}.2",
        ]:
            self.assertGreater(Path(path).stat().st_size, 128)
