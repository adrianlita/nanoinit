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
- invalid config fallback behavior
- root config parsing
- nested config parsing with `--config-json-object`
- environment overrides for config file and config object
- array arguments
- string arguments as one argument
- manual mode through `--manual-mode`
- manual mode through `NANOINIT_MANUAL_MODE`
- application `stdout` redirection
- application `stderr` redirection
- empty `stdout` / `stderr` redirection to `/dev/null`
- `autorestart`
- `SIGUSR1` config reload
- nanoinit `--log-path`
- application `stdout` / `stderr` log rotation
- rotate count `0`

These tests are black-box feature tests against the compiled binary, implemented
with Python's standard `unittest` module. That is the most useful level for
nanoinit because the primary behavior is process supervision, signal handling,
config parsing, and file-descriptor routing.
