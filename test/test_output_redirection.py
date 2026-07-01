# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
from common import NanoInitTestCase


class OutputRedirectionTest(NanoInitTestCase):
    def test_stdout_and_stderr_redirection(self):
        marker = self.tmp / "redirect.marker"
        stdout_log = self.tmp / "app.stdout"
        stderr_log = self.tmp / "app.stderr"
        app = self.write_file(
            self.tmp / "redirect-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'app stdout\\n'\n"
            "printf 'app stderr\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "redirect": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stderr": str(stderr_log),
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "done")
        self.wait_for_contains(stdout_log, "app stdout")
        self.wait_for_contains(stderr_log, "app stderr")
        self.assertFileNotContains(self.tmp / "nanoinit.stdout", "app stdout")
        self.assertFileNotContains(self.tmp / "nanoinit.stderr", "app stderr")

    def test_output_path_placeholders_are_rendered_for_direct_redirection(self):
        marker = self.tmp / "placeholder-direct.marker"
        app = self.write_file(
            self.tmp / "placeholder-direct-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'placeholder stdout\\n'\n"
            "printf 'placeholder stderr\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "placeholder-direct": {
                    "path": str(app),
                    "stdout": str(self.tmp / "direct-{app-name}-{timestamp}.out"),
                    "stderr": str(self.tmp / "direct-{app-name}-{timestamp}.err"),
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "done")
        self.wait_for(lambda: len(list(self.tmp.glob("direct-placeholder-direct-*.out"))) == 1, "rendered stdout log")
        self.wait_for(lambda: len(list(self.tmp.glob("direct-placeholder-direct-*.err"))) == 1, "rendered stderr log")

        stdout_logs = list(self.tmp.glob("direct-placeholder-direct-*.out"))
        stderr_logs = list(self.tmp.glob("direct-placeholder-direct-*.err"))
        self.assertFileContains(stdout_logs[0], "placeholder stdout")
        self.assertFileContains(stderr_logs[0], "placeholder stderr")
        self.assertFalse((self.tmp / "direct-{app-name}-{timestamp}.out").exists())
        self.assertFalse((self.tmp / "direct-{app-name}-{timestamp}.err").exists())

        prefix = "direct-placeholder-direct-"
        stdout_timestamp = stdout_logs[0].name[len(prefix):-len(".out")]
        stderr_timestamp = stderr_logs[0].name[len(prefix):-len(".err")]
        self.assertEqual(stdout_timestamp, stderr_timestamp)
