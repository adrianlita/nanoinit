# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class RootConfigArrayArgsTest(NanoInitTestCase):
    def test_root_config_and_array_args(self):
        actual = self.tmp / "args.actual"
        recorder = self.write_args_recorder("record-args.sh", actual)
        config = self.write_config(
            "config.json",
            {
                "recorder": {
                    "path": str(recorder),
                    "args": ["alpha", "two words"],
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_file(actual)
        self.assertEqual(actual.read_text(), "2\nalpha\ntwo words\n")
