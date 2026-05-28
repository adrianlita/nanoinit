# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
import time

from common import NanoInitTestCase


class AutorestartTest(NanoInitTestCase):
    def test_autorestart_restarts_exited_app(self):
        counter = self.tmp / "restart.count"
        app = self.write_file(
            self.tmp / "restart-app.sh",
            "#!/bin/sh\n"
            "count=0\n"
            f"if [ -f '{counter}' ]; then\n"
            f"    count=$(cat '{counter}')\n"
            "fi\n"
            "count=$((count + 1))\n"
            f"printf '%s\\n' \"$count\" > '{counter}'\n"
            "exit 0\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {"restart": {"path": str(app), "autorestart": True}},
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_integer_ge(counter, 2, timeout=8)

    def test_autorestart_waits_before_restarting_fast_exit(self):
        counter = self.tmp / "restart-delay.count"
        app = self.write_file(
            self.tmp / "restart-delay-app.sh",
            "#!/bin/sh\n"
            "count=0\n"
            f"if [ -f '{counter}' ]; then\n"
            f"    count=$(cat '{counter}')\n"
            "fi\n"
            "count=$((count + 1))\n"
            f"printf '%s\\n' \"$count\" > '{counter}'\n"
            "exit 0\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {"restart-delay": {"path": str(app), "autorestart": True}},
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_integer_ge(counter, 1, timeout=5)
        time.sleep(0.2)

        value = int(counter.read_text().strip())
        self.assertLessEqual(value, 2)
        self.wait_for_integer_ge(counter, 2, timeout=8)
