# MIT License
# Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
#
import os
import json
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[1]
BIN = ROOT_DIR / "nanoinit"

_BUILT = False


def build_nanoinit():
    global _BUILT
    if _BUILT:
        return

    subprocess.run(["make", "-C", str(ROOT_DIR / "source")], check=True)
    _BUILT = True


class NanoInitTestCase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build_nanoinit()

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="nanoinit-test."))
        self.proc = None

    def tearDown(self):
        self.stop_nanoinit()
        shutil.rmtree(self.tmp, ignore_errors=True)

    def start_nanoinit(self, *args, env=None):
        stdout = open(self.tmp / "nanoinit.stdout", "wb")
        stderr = open(self.tmp / "nanoinit.stderr", "wb")
        full_env = os.environ.copy()
        if env:
            full_env.update(env)

        self.proc = subprocess.Popen(
            [str(BIN), *map(str, args)],
            stdout=stdout,
            stderr=stderr,
            env=full_env,
        )
        stdout.close()
        stderr.close()
        return self.proc

    def stop_nanoinit(self):
        if self.proc is None:
            return

        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)

        self.proc = None

    def run_nanoinit_timeout(self, *args, timeout=1, env=None):
        full_env = os.environ.copy()
        if env:
            full_env.update(env)

        try:
            result = subprocess.run(
                [str(BIN), *map(str, args)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                env=full_env,
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired:
            return 124

        return result.returncode

    def write_file(self, path, content, executable=False):
        path = Path(path)
        path.write_text(content)
        if executable:
            path.chmod(0o755)
        return path

    def write_config(self, name, data):
        config = self.tmp / name
        config.write_text(json.dumps(data, indent=4))
        return config

    def write_marker_script(self, name, marker, value):
        script = self.tmp / name
        self.write_file(
            script,
            f"#!/bin/sh\nprintf '%s\\n' '{value}' > '{marker}'\n",
            executable=True,
        )
        return script

    def write_args_recorder(self, name, output):
        script = self.tmp / name
        self.write_file(
            script,
            "#!/bin/sh\n"
            "{\n"
            "    printf '%s\\n' \"$#\"\n"
            "    for arg in \"$@\"; do\n"
            "        printf '%s\\n' \"$arg\"\n"
            "    done\n"
            f"}} > '{output}'\n",
            executable=True,
        )
        return script

    def wait_for(self, predicate, description, timeout=5):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if predicate():
                return
            time.sleep(0.1)
        self.fail(f"timed out waiting for {description}")

    def wait_for_file(self, path, timeout=5):
        path = Path(path)
        self.wait_for(path.is_file, f"file {path}", timeout=timeout)

    def wait_for_contains(self, path, text, timeout=5):
        path = Path(path)

        def contains():
            return path.is_file() and text in path.read_text(errors="replace")

        self.wait_for(contains, f"{text!r} in {path}", timeout=timeout)

    def wait_for_integer_ge(self, path, minimum, timeout=5):
        path = Path(path)

        def value_is_enough():
            if not path.is_file():
                return False
            value = path.read_text().strip()
            return value.isdigit() and int(value) >= minimum

        self.wait_for(value_is_enough, f"{path} >= {minimum}", timeout=timeout)

    def assertFileContains(self, path, text):
        content = Path(path).read_text(errors="replace")
        self.assertIn(text, content)

    def assertFileNotContains(self, path, text):
        path = Path(path)
        if not path.exists():
            return
        content = path.read_text(errors="replace")
        self.assertNotIn(text, content)

    def assertFileSizeLessEqual(self, path, limit):
        size = Path(path).stat().st_size
        self.assertLessEqual(size, limit, f"{path} has size {size}, expected <= {limit}")

    def reload_nanoinit(self):
        self.assertIsNotNone(self.proc)
        os.kill(self.proc.pid, signal.SIGUSR1)
