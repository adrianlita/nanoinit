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

    def test_output_parent_directories_are_created_for_direct_redirection(self):
        marker = self.tmp / "mkdir-direct.marker"
        stdout_log = self.tmp / "missing" / "direct" / "app.stdout"
        stderr_log = self.tmp / "missing" / "direct" / "app.stderr"
        app = self.write_file(
            self.tmp / "mkdir-direct-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'created stdout\\n'\n"
            "printf 'created stderr\\n' >&2\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "mkdir-direct": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                    "stderr": str(stderr_log),
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(marker, "done")
        self.wait_for_contains(stdout_log, "created stdout")
        self.wait_for_contains(stderr_log, "created stderr")
        self.assertTrue(stdout_log.parent.is_dir())

    def test_output_parent_directory_creation_can_be_disabled(self):
        marker = self.tmp / "mkdir-disabled.marker"
        stdout_log = self.tmp / "missing-disabled" / "direct" / "app.stdout"
        app = self.write_file(
            self.tmp / "mkdir-disabled-app.sh",
            "#!/bin/sh\n"
            f"printf 'done\\n' > '{marker}'\n"
            "printf 'should not be written\\n'\n",
            executable=True,
        )
        config = self.write_config(
            "config.json",
            {
                "ni_create_log_dirs": False,
                "mkdir-disabled": {
                    "path": str(app),
                    "stdout": str(stdout_log),
                },
            },
        )

        self.start_nanoinit("-c", config, "-v0")
        self.wait_for_contains(self.tmp / "nanoinit.stderr", "failed to redirect stdout for app mkdir-disabled")
        self.assertFalse(marker.exists())
        self.assertFalse(stdout_log.exists())
        self.assertFalse(stdout_log.parent.exists())

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
                    "stdout": str(self.tmp / "direct-{env:NANOINIT_TEST_PATH_FIELD}-{app-name}-{timestamp}.out"),
                    "stderr": str(self.tmp / "direct-{env:NANOINIT_TEST_PATH_FIELD}-{app-name}-{timestamp}.err"),
                }
            },
        )

        self.start_nanoinit("-c", config, "-v0", env={"NANOINIT_TEST_PATH_FIELD": "env-path"})
        self.wait_for_contains(marker, "done")
        self.wait_for(lambda: len(list(self.tmp.glob("direct-env-path-placeholder-direct-*.out"))) == 1, "rendered stdout log")
        self.wait_for(lambda: len(list(self.tmp.glob("direct-env-path-placeholder-direct-*.err"))) == 1, "rendered stderr log")

        stdout_logs = list(self.tmp.glob("direct-env-path-placeholder-direct-*.out"))
        stderr_logs = list(self.tmp.glob("direct-env-path-placeholder-direct-*.err"))
        self.assertFileContains(stdout_logs[0], "placeholder stdout")
        self.assertFileContains(stderr_logs[0], "placeholder stderr")
        self.assertFalse((self.tmp / "direct-{env:NANOINIT_TEST_PATH_FIELD}-{app-name}-{timestamp}.out").exists())
        self.assertFalse((self.tmp / "direct-{env:NANOINIT_TEST_PATH_FIELD}-{app-name}-{timestamp}.err").exists())

        prefix = "direct-env-path-placeholder-direct-"
        stdout_timestamp = stdout_logs[0].name[len(prefix):-len(".out")]
        stderr_timestamp = stderr_logs[0].name[len(prefix):-len(".err")]
        self.assertEqual(stdout_timestamp, stderr_timestamp)
