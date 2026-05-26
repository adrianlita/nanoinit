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
