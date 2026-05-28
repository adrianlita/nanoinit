<!--
MIT License
Copyright (c) 2022-2026 AXIPlus / Adrian Lita / Alex Stancu - www.axiplus.com
-->

# nanoinit tests

Run the full test suite from the repository root:

```sh
python3 -m unittest discover -s test -p 'test_*.py'
```

The tests build `nanoinit` first, create temporary helper applications and JSON
configs under `/tmp`, run each feature test, and remove their temporary files
afterwards.

`test-config.json` is an intentionally invalid config fixture. Each executable
test lives in its own `test_*.py` file, with shared helpers in `common.py`.

## Coverage

The suite covers:

- no-config zero-config behavior
- default `/etc/nanoinit/config.json` lookup behavior
- control commands: `list`, `ls`, `status`, `start`, `stop`
- second supervisor invocation prints help when the control socket is active
- invalid config fallback behavior
- root config parsing
- nested config parsing with `--config-json-object`
- environment overrides for config file and config object
- array arguments
- string arguments as one argument
- `autostart` defaulting to `true`
- `autostart: false` skipping application startup
- removed manual mode environment and flag behavior
- application `stdout` redirection
- application `stderr` redirection
- application `stdout` / `stderr` passthrough
- empty `stdout` / `stderr` redirection to `/dev/null`
- `autorestart`
- autorestart restart delay
- spawn failure logging
- `SIGUSR1` config reload
- nanoinit `--log-path`
- application `stdout` / `stderr` log rotation
- application `stdout` / `stderr` `prefix_logs`
- newline-boundary rotation behavior
- rotate count `0`

These tests are black-box feature tests against the compiled binary, implemented
with Python's standard `unittest` module. That is the most useful level for
nanoinit because the primary behavior is process supervision, signal handling,
config parsing, and file-descriptor routing.
