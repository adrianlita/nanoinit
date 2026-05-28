# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
import subprocess

from common import ROOT_DIR, NanoInitTestCase


class DefaultConfigFileTest(NanoInitTestCase):
    def build_nanoinit_with_default_config(self, default_config):
        binary = self.tmp / "nanoinit-default-config"
        source_dir = ROOT_DIR / "source"
        source_files = sorted(source_dir.rglob("*.c"))

        subprocess.run(
            [
                "gcc",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-O2",
                "-I",
                str(source_dir),
                f'-DNANOINIT_DEFAULT_CONFIG_FILE="{default_config}"',
                *map(str, source_files),
                "-o",
                str(binary),
            ],
            check=True,
        )
        return binary

    def start_binary(self, binary, *args):
        stdout = open(self.tmp / "nanoinit.stdout", "wb")
        stderr = open(self.tmp / "nanoinit.stderr", "wb")
        self.proc = subprocess.Popen(
            [str(binary), *map(str, args)],
            stdout=stdout,
            stderr=stderr,
        )
        stdout.close()
        stderr.close()

    def test_existing_default_config_file_is_used_when_config_file_is_omitted(self):
        marker = self.tmp / "default-config.marker"
        app = self.write_marker_script("default-config-app.sh", marker, "default-config")
        default_config = self.write_config(
            "default-config.json",
            {
                "default-config": {
                    "path": str(app),
                }
            },
        )
        binary = self.build_nanoinit_with_default_config(default_config)

        self.start_binary(binary, "-v0")
        self.wait_for_contains(marker, "default-config")

    def test_missing_default_config_file_keeps_zero_config_behavior(self):
        binary = self.build_nanoinit_with_default_config(self.tmp / "missing-default-config.json")

        try:
            subprocess.run(
                [str(binary), "-v0"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=1,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return

        self.fail("nanoinit exited even though no config was available")
