# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class StringArgTest(NanoInitTestCase):
    def test_string_arg_is_single_argument(self):
        actual = self.tmp / "args.actual"
        recorder = self.write_args_recorder("record-args.sh", actual)
        config = self.write_config(
            "config.json",
            {
                "recorder": {
                    "path": str(recorder),
                    "args": "single arg with spaces",
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_file(actual)
        self.assertEqual(actual.read_text(), "1\nsingle arg with spaces\n")
