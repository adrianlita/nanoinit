import time

from common import NanoInitTestCase


class RotateCountZeroTest(NanoInitTestCase):
    def test_rotate_count_zero_keeps_only_current_file(self):
        stdout_log = self.tmp / "app-zero.out"
        app = self.write_file(
            self.tmp / "rotate-zero-app.sh",
            "#!/bin/sh\n"
            "i=0\n"
            "while [ \"$i\" -lt 20 ]; do\n"
            "    printf '012345678901234567890123456789\\n'\n"
            "    i=$((i + 1))\n"
            "done\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "rotate-zero": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stdout_rotate_size": 64,
                    "stdout_rotate_count": 0,
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_file(stdout_log, timeout=8)
        time.sleep(0.5)
        self.assertTrue(stdout_log.exists())
        self.assertFileSizeLessEqual(stdout_log, 64)
        self.assertFalse((self.tmp / "app-zero.out.1").exists())
