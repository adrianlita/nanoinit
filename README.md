# nanoinit

`nanoinit` is a small Linux process supervisor designed for Docker containers.
It starts one or more configured applications, keeps selected applications alive
with optional autorestart, exposes simple runtime controls, forwards termination
signals, and can reload its configuration while the container is running.

The project is intentionally small: configuration is JSON, application startup is
done with `fork()` and `execv()`, and there are no runtime service-manager
dependencies.

## Contents

- [What nanoinit does](#what-nanoinit-does)
- [Quick start](#quick-start)
- [Building](#building)
- [Testing](#testing)
- [Continuous integration and releases](#continuous-integration-and-releases)
- [Using nanoinit in Docker](#using-nanoinit-in-docker)
- [Command-line arguments](#command-line-arguments)
- [Environment variables](#environment-variables)
- [Configuration file](#configuration-file)
- [Autostart](#autostart)
- [Runtime controls](#runtime-controls)
- [Logging and output redirection](#logging-and-output-redirection)
- [Signals and reloads](#signals-and-reloads)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [Project layout](#project-layout)

## What nanoinit does

`nanoinit` is meant to be the main process of a container. It reads a JSON
configuration file, starts each configured application, and then waits for child
processes and signals.

It is useful when a container needs a tiny supervisor instead of a shell script or
a full init system. Typical use cases include:

- starting multiple long-running processes in one container
- redirecting each process' `stdout` and `stderr`
- rotating redirected application output files by size
- automatically restarting selected processes when they exit
- keeping selected configured processes from starting automatically
- checking status and starting or stopping configured processes at runtime
- reloading the configuration without replacing the container

`nanoinit` is not a general-purpose service manager. It does not implement
dependency ordering, health checks, privilege switching, environment files, start
limits, or health-aware backoff policies.

## Quick start

Create a config file:

```json
{
    "web": {
        "path": "/usr/local/bin/web-server",
        "args": ["--host", "0.0.0.0", "--port", "8080"],
        "autorestart": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "args": "--foreground",
        "autostart": false
    }
}
```

Run `nanoinit` with that file:

```sh
./nanoinit --config-file=/etc/nanoinit/config.json --verbose=2
```

`web` starts automatically. `worker` is configured but skipped because it has
`"autostart": false`.

## Building

Build from the `source` directory:

```sh
make -C source
```

The resulting binary is written to the repository root:

```sh
./nanoinit
```

Build a debug binary:

```sh
debugEnable=true make -C source
```

Clean generated build output:

```sh
make -C source clean
```

### Build requirements

Ubuntu:

- `gcc`
- `make`
- standard libc development files

Alpine:

- `gcc`
- `g++`
- `make`
- `argp-standalone`

The Alpine build must link with `-largp`. The provided Alpine builder Dockerfile
patches the Makefile for that case.

### Builder images

The repository includes builder Dockerfiles:

```sh
docker build -f deploy/Dockerfile.ubuntu -t nanoinit-builder-ubuntu .
docker run --rm -v "$PWD/deploy/release:/opt/release" nanoinit-builder-ubuntu
```

```sh
docker build -f deploy/Dockerfile.alpine -t nanoinit-builder-alpine .
docker run --rm -v "$PWD/deploy/release:/opt/release" nanoinit-builder-alpine
```

Each builder copies the compiled `nanoinit` binary to `/opt/release/nanoinit` in
the mounted release directory.

## Testing

Run the feature test suite from the repository root:

```sh
python3 -m unittest discover -s test -p 'test_*.py'
```

The Python tests build `nanoinit`, create temporary helper applications and
configs, then verify the supervisor behavior through the compiled binary. Each
feature test lives in its own file under `test/`. The suite covers config
parsing, arguments, environment overrides, autostart, output redirection,
autorestart, reloads, `--log-path`, and application log rotation.

## Continuous integration and releases

GitHub Actions workflows live under `.github/workflows/`.

`CI` runs on every push, on pull requests, and when started manually from the
Actions tab. It runs the Python unittest suite:

```sh
python3 -m unittest discover -s test -p 'test_*.py'
```

`Release` runs when a tag matching `v*` is pushed and can also be started
manually. It runs the test suite, builds release binaries with the Ubuntu and
Alpine Docker builder images, produces `nanoinit-ubuntu`, `nanoinit-alpine`,
and `SHA256SUMS`, then uploads them as workflow artifacts.

When the release workflow is triggered by a `v*` tag push, it also creates a
GitHub Release for that tag and attaches the same assets. Manual runs from a
branch build the assets without publishing a GitHub Release.

## Using nanoinit in Docker

Copy the binary and a config file into your image:

```dockerfile
COPY nanoinit /usr/local/bin/nanoinit
COPY config.json /etc/nanoinit/config.json

CMD ["/usr/local/bin/nanoinit", "--config-file=/etc/nanoinit/config.json"]
```

For a config embedded under a parent object:

```dockerfile
CMD [
    "/usr/local/bin/nanoinit",
    "--config-file=/etc/app/config.json",
    "--config-json-object=/nanoinit"
]
```

To keep an application in the config without launching it automatically, set
`"autostart": false` for that application.

## Command-line arguments

### `-c`, `--config-file=/path/to/config.json`

Path to the JSON configuration file.

If this argument is omitted, nanoinit checks `/etc/nanoinit/config.json`. When
that file exists, nanoinit uses it as the config file. When it does not exist,
no configuration is loaded: nanoinit starts no applications and remains running
until it receives a stop signal. This is useful for keeping a container alive
while debugging its filesystem or environment.

### `-j`, `--config-json-object=/path/in/json`

Path to the parent JSON object that contains the nanoinit application
configuration.

If omitted, the root object of the JSON file is treated as the nanoinit
configuration. Use this option when the JSON file is shared with another
application and nanoinit should read only one nested object.

Examples:

- `/nanoinit`
- `/config/nanoinit`
- `/services/supervisor`

Use slash-separated object names. A leading slash is recommended and is the
format used throughout the examples.

### `-l`, `--log-path=/path/to/log.txt`

Writes nanoinit logs to the specified file in addition to normal terminal output.

The log file receives nanoinit's log entries. Terminal output is filtered by the
selected verbosity level. `--log-path` is not the same as per-application
`stdout` or `stderr` redirection, which is configured per app in the JSON file.

### `-r`, `--reload`

Finds a running `nanoinit` process and sends it `SIGUSR1`. The running supervisor
then terminates its supervised applications, reloads the config file, and starts
applications from the new config.

This command is intended to be executed from the same container or host namespace
where the target `nanoinit` process is visible in `/proc`.

### `-v`, `--verbose=0-2`

Controls terminal logging verbosity.

- `0`: nanoinit errors only
- `1`: nanoinit errors and application errors
- `2`: verbose logs

Default: `0`.

### Runtime control commands

When a `nanoinit` supervisor is already running, a second `nanoinit` invocation
can connect to it and control configured applications:

```sh
nanoinit list
nanoinit ls
nanoinit status <app-name>
nanoinit start <app-name>
nanoinit stop <app-name>
```

These commands do not start another supervisor. They connect to the running
supervisor through the control socket, print the response, and exit. `<app-name>`
is the JSON object name from the loaded config.

If a supervisor is already active on the configured control socket and `nanoinit`
is run again without a control command, the second invocation prints the help
menu and exits instead of starting another supervisor.

Use `list` or `ls` to show all configured applications and their status. Use
`status` for one application. Use `start` to launch a stopped configured
application, including one with `"autostart": false`. Use `stop` to send
`SIGTERM` to a running application.

If an application has `"autorestart": true`, `nanoinit stop <app-name>` also
marks that application as intentionally stopped, so it will not be restarted by
autorestart. A later `nanoinit start <app-name>` enables it again.

## Environment variables

Environment variables are useful in Docker because they can be supplied at
runtime without changing the image.

### `NANOINIT_CONFIG_FILE`

Overrides `--config-file` and the default `/etc/nanoinit/config.json` lookup.

```sh
NANOINIT_CONFIG_FILE=/tmp/debug-config.json ./nanoinit -c /etc/nanoinit/config.json
```

In this example, `/tmp/debug-config.json` is used.

### `NANOINIT_CONFIG_JSON_OBJECT`

Overrides `--config-json-object`.

```sh
NANOINIT_CONFIG_JSON_OBJECT=/debug/nanoinit ./nanoinit -j /production/nanoinit
```

In this example, `/debug/nanoinit` is used.

### `NANOINIT_CONTROL_SOCKET`

Sets the Unix-domain socket used by runtime control commands.

Default: `/tmp/nanoinit.sock`.

The running supervisor and the control command must use the same value:

```sh
NANOINIT_CONTROL_SOCKET=/run/nanoinit/control.sock nanoinit -c /etc/nanoinit/config.json
NANOINIT_CONTROL_SOCKET=/run/nanoinit/control.sock nanoinit status web
```

Use this when the default socket path is not writable, when you want the socket
under `/run`, or when more than one `nanoinit` supervisor runs in the same
namespace.

### `NI_LOG_FORMAT`

Sets the format for nanoinit's own log lines. This overrides `ni_log_format`
from the configuration file.

Default: `{message}`.

Supported placeholders:

- `{message}`: the formatted log message
- `{timestamp}`: current Unix timestamp with millisecond precision
- `{timestampISO}`: current UTC timestamp in ISO-8601 format, for example
  `2026-02-03T11:23:56.123Z`
- `{app-name}`: `nanoinit` for nanoinit's own logs
- `{device-name}`: value from `DEVICE_NAME`, or the host/container hostname
- `{env:NAME}`: value of the environment variable `NAME`, or empty when unset

Example:

```sh
NI_LOG_FORMAT="[{timestamp}] [{device-name}] [{app-name}] {message}" nanoinit -c /etc/nanoinit/config.json
```

Unknown placeholders are left unchanged.

### `DEVICE_NAME`

Sets the `{device-name}` value used by `NI_LOG_FORMAT`, `ni_log_format`, and
application `prefix_logs`.

When this variable is omitted or empty, nanoinit uses the hostname reported by
the container or pod.

## Configuration file

The configuration file is JSON parsed by the bundled `edJSON` parser. JSON
comments are supported.

At the selected configuration object, every property name is treated as an
application name, except for the reserved global option `ni_log_format`. Each
application value must be an object.

Minimal config:

```json
{
    "app": {
        "path": "/usr/local/bin/app"
    }
}
```

Full application entry:

```json
{
    "ni_log_format": "[{timestamp}] [{device-name}] [{app-name}] {message}",
    "app": {
        "path": "/usr/local/bin/app",
        "args": ["--listen", "0.0.0.0:8080"],
        "autorestart": true,
        "autostart": true,
        "prefix_logs": "[{timestamp}] [{device-name}] [{app-name}] ",
        "stdout": "/var/log/app.stdout.log",
        "stdout_passthrough": false,
        "stdout_rotate_size": 10485760,
        "stdout_rotate_count": 5,
        "stderr": "/var/log/app.stderr.log",
        "stderr_passthrough": false,
        "stderr_rotate_size": 10485760,
        "stderr_rotate_count": 5
    }
}
```

### Global fields

`ni_log_format`

Optional string. Default: `{message}`.

Sets the format for nanoinit's own log lines from the loaded configuration file.
It supports the same placeholders as `NI_LOG_FORMAT`:

- `{message}`: the formatted log message
- `{timestamp}`: current Unix timestamp with millisecond precision
- `{timestampISO}`: current UTC timestamp in ISO-8601 format, for example
  `2026-02-03T11:23:56.123Z`
- `{app-name}`: `nanoinit` for nanoinit's own logs
- `{device-name}`: value from `DEVICE_NAME`, or the host/container hostname
- `{env:NAME}`: value of the environment variable `NAME`, or empty when unset

`NI_LOG_FORMAT` takes precedence when both are configured. The config value is
applied after the config file is parsed, so parse errors emitted while loading
the file still use the default or environment-provided format.

### Application fields

`path`

Required. Path to the executable passed to `execv()`.

The path may be absolute or relative. Relative paths are resolved relative to the
current working directory of the `nanoinit` process.

`args`

Optional. Arguments passed to the application.

Use a string for exactly one argument:

```json
{
    "worker": {
        "path": "/usr/local/bin/worker",
        "args": "--foreground"
    }
}
```

Use an array for multiple arguments:

```json
{
    "web": {
        "path": "/usr/local/bin/web",
        "args": ["--host", "0.0.0.0", "--port", "8080"]
    }
}
```

Do not combine multiple command-line arguments into one string. `nanoinit` does
not run a shell and does not split strings on spaces.

`autorestart`

Optional boolean. Default: `false`.

When `true`, `nanoinit` starts the application again after it exits. This applies
to both clean and failing exits.

Restarts are delayed by one second. This keeps a missing executable or instantly
exiting process from creating a tight restart loop. There is currently no maximum
retry count or health-aware backoff policy.

`autostart`

Optional boolean. Default: `true`.

When `true`, nanoinit starts the application during normal startup and after
reloads. When `false`, the application remains configured but is not launched by
nanoinit.

`prefix_logs`

Optional string. Default: empty.

When non-empty, nanoinit prefixes every line written by the application to
`stdout` and `stderr`. The prefix is applied to configured output files and to
passthrough output.

Supported placeholders:

- `{timestamp}`: current Unix timestamp with millisecond precision
- `{timestampISO}`: current UTC timestamp in ISO-8601 format, for example
  `2026-02-03T11:23:56.123Z`
- `{app-name}`: the application name from the config object
- `{device-name}`: value from `DEVICE_NAME`, or the host/container hostname
- `{env:NAME}`: value of the environment variable `NAME`, or empty when unset
- `{message}`: empty for prefixes

Example:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "prefix_logs": "[{timestamp}] [{device-name}] [{app-name}] "
    }
}
```

If `stdout` or `stderr` is omitted and `prefix_logs` is set, nanoinit captures
that stream so it can add the prefix, then forwards it to nanoinit's actual
stdout or stderr. If `stdout` or `stderr` is explicitly set to `""`, output is
still discarded through `/dev/null`.

`stdout`

Optional string. Redirects the application's standard output.

- omitted: inherit nanoinit's `stdout`
- non-empty string: write `stdout` to that path
- empty string `""`: redirect `stdout` to `/dev/null`

The path supports the same placeholders as `prefix_logs`. The path is rendered
once when the application is spawned; `{message}` is empty for file paths and
unknown placeholders are left unchanged.

`stdout_passthrough`

Optional boolean. Default: `false`.

When `true`, nanoinit writes the application's `stdout` to the configured
`stdout` file and also passes the same bytes through to nanoinit's actual
standard output. This is useful when you want both a file on disk and container
stdout logging.

Passthrough only applies when `stdout` is a non-empty file path. If `stdout` is
omitted, the application already inherits nanoinit's stdout. If `stdout` is
`""`, output is discarded through `/dev/null` and passthrough is ignored.

`stdout_rotate_size`

Optional integer. Default: `0`.

When greater than `0`, enables size-based rotation for the file configured by
`stdout`. The value is the byte threshold that arms rotation. After the current
`stdout` log reaches that threshold, nanoinit waits for the next newline
character, writes that complete line, then rotates the file and starts a new
current log file.

Rotation does not split a log line. A rotated file can therefore be larger than
`stdout_rotate_size` when the line that crosses the threshold is longer than the
remaining space.

Rotation only applies when `stdout` is a non-empty file path. It is ignored when
`stdout` is omitted or set to `""`.

`stdout_rotate_count`

Optional integer. Default: `1`.

Number of rotated `stdout` files to keep. For example, with:

```json
"stdout": "/var/log/app.stdout.log",
"stdout_rotate_size": 10485760,
"stdout_rotate_count": 3
```

nanoinit keeps:

- `/var/log/app.stdout.log`
- `/var/log/app.stdout.log.1`
- `/var/log/app.stdout.log.2`
- `/var/log/app.stdout.log.3`

Set `stdout_rotate_count` to `0` to keep only the current log file.

`stderr`

Optional string. Redirects the application's standard error.

- omitted: inherit nanoinit's `stderr`
- non-empty string: write `stderr` to that path
- empty string `""`: redirect `stderr` to `/dev/null`

The path supports the same placeholders as `stdout`.

`stderr_passthrough`

Optional boolean. Default: `false`.

When `true`, nanoinit writes the application's `stderr` to the configured
`stderr` file and also passes the same bytes through to nanoinit's actual
standard error.

Passthrough only applies when `stderr` is a non-empty file path. If `stderr` is
omitted, the application already inherits nanoinit's stderr. If `stderr` is
`""`, output is discarded through `/dev/null` and passthrough is ignored.

`stderr_rotate_size`

Optional integer. Default: `0`.

When greater than `0`, enables size-based rotation for the file configured by
`stderr`. The value is the byte threshold that arms rotation. After the current
`stderr` log reaches that threshold, nanoinit waits for the next newline before
rotating. Rotation only applies when `stderr` is a non-empty file path.

`stderr_rotate_count`

Optional integer. Default: `1`.

Number of rotated `stderr` files to keep. Set it to `0` to keep only the current
log file.

### Dedicated config file

If the whole file belongs to nanoinit, omit `--config-json-object`:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "args": ["--config", "/etc/api/config.json"],
        "autorestart": true,
        "stdout": "/var/log/api.stdout.log",
        "stderr": "/var/log/api.stderr.log"
    },
    "debug-shell-helper": {
        "path": "/usr/local/bin/debug-helper",
        "autostart": false
    }
}
```

Run it:

```sh
nanoinit --config-file=/etc/nanoinit/config.json
```

### Shared config file

If nanoinit config is nested inside a larger JSON file, use
`--config-json-object`:

```json
{
    "app": {
        "name": "example"
    },
    "nanoinit": {
        "api": {
            "path": "/usr/local/bin/api",
            "autorestart": true
        },
        "worker": {
            "path": "/usr/local/bin/worker",
            "args": ["--queue", "default"]
        }
    }
}
```

Run it:

```sh
nanoinit --config-file=/etc/app/config.json --config-json-object=/nanoinit
```

Only entries under `/nanoinit` are interpreted as supervised applications.

## Autostart

`autostart` controls whether nanoinit launches an application automatically.

Sometimes a container config includes an application that should stay stopped
until you run it yourself with different arguments, under a debugger, or from an
interactive shell. Set `autostart` to `false` for that application:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "autorestart": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "autostart": false
    }
}
```

Run nanoinit:

```sh
nanoinit -c config.json
```

Result:

- `api` starts
- `worker` is skipped

`autostart` only controls whether nanoinit launches the application
automatically during supervisor startup or config reload. A configured
application with `"autostart": false` can still be launched later with
`nanoinit start <app-name>`.

## Runtime controls

Runtime controls let you inspect and change the state of applications from
inside the same container, or from any environment that can access the control
socket, while the main `nanoinit` supervisor keeps running.

The control interface is intentionally small:

```sh
nanoinit list
nanoinit status api
nanoinit start worker
nanoinit stop worker
```

`list` and `ls` print a table with the application name, current status, PID,
uptime, `autostart`, and `autorestart` values.

`status <app-name>` prints details for one application. Status is one of:

- `running`: the process is currently running and desired to be running
- `stopping`: the process is still running after an explicit stop request
- `stopped`: no process is currently running

`start <app-name>` starts the configured application if it is stopped. If it is
already running, the command reports that and leaves it alone.

`stop <app-name>` sends `SIGTERM` to the configured application if it is running.
If the application has `autorestart` enabled, this command suppresses
autorestart until the next `start <app-name>` command or config reload.

Control commands return a non-zero exit code when the control socket cannot be
reached, the application name does not exist, or the command cannot be completed.

## Logging and output redirection

There are two kinds of output to consider:

- nanoinit's own logs
- each supervised application's `stdout` and `stderr`

Use `--verbose` to control how much nanoinit prints to the terminal. Use
`--log-path` to also write nanoinit's own logs to a file.

By default, nanoinit's own logs contain only the log message. Use
`ni_log_format` in the config file or `NI_LOG_FORMAT` in the environment to
include fields such as timestamp, app name, or device name. `NI_LOG_FORMAT`
takes precedence over the config value.

Use `stdout` and `stderr` in the config file to redirect application streams.
Relative output paths are resolved relative to nanoinit's current working
directory.

Use `stdout_passthrough` and `stderr_passthrough` when an application stream
should be written to a configured file and also forwarded to nanoinit's actual
stdout or stderr.

Use `prefix_logs` when application output should be tagged per line before it is
written to configured files or forwarded to nanoinit's stdout/stderr.

Use `stdout_rotate_size` / `stdout_rotate_count` and
`stderr_rotate_size` / `stderr_rotate_count` to rotate application output files.
Rotation is handled by nanoinit while the supervised process is running, so the
application does not need to reopen its logs. Rotation happens on newline
boundaries after the configured byte threshold is reached, which avoids splitting
one application log message across two files.

Example:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "stdout": "/var/log/api.stdout.log",
        "stdout_passthrough": true,
        "stdout_rotate_size": 10485760,
        "stdout_rotate_count": 5,
        "stderr": "/var/log/api.stderr.log",
        "stderr_passthrough": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "stdout": "",
        "stderr": ""
    }
}
```

In this example:

- `api` writes its output to files
- `api` also forwards that output to nanoinit's stdout and stderr
- `api` keeps up to five rotated stdout files, each around 10 MiB
- `worker` output is discarded through `/dev/null`
- nanoinit's own logs still follow `--verbose` and `--log-path`

The rotated file names use numeric suffixes. For `stdout` set to
`/var/log/api.stdout.log`, the most recent rotated file is
`/var/log/api.stdout.log.1`, then `.2`, and so on.

## Signals and reloads

`nanoinit` registers handlers for:

- `SIGTERM`
- `SIGINT`
- `SIGQUIT`
- `SIGUSR1`

When it receives `SIGTERM`, `SIGINT`, or `SIGQUIT`, it forwards that signal to
all currently running supervised applications and waits for them to exit.

When it receives `SIGUSR1`, it performs a reload:

1. Forward `SIGTERM` to running supervised applications.
2. Wait for those applications to exit.
3. Free the current configuration.
4. Read the configuration file again.
5. Start applications from the new configuration.

You can trigger reload by sending `SIGUSR1` directly:

```sh
kill -USR1 <nanoinit-pid>
```

Or by using the helper mode:

```sh
nanoinit --reload
```

The helper mode searches `/proc` for a different process whose command line
contains `nanoinit`, then sends `SIGUSR1` to it.

## Examples

### Keep a container alive with no apps

```sh
nanoinit
```

If `/etc/nanoinit/config.json` does not exist, no config file is loaded, no
applications are started, and nanoinit waits for a stop signal.

### Run two apps

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "args": ["--listen", "0.0.0.0:8080"],
        "autorestart": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "args": ["--queue", "default"],
        "autorestart": true
    }
}
```

```sh
nanoinit -c config.json -v2
```

### Debug one app manually

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "autorestart": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "autostart": false
    }
}
```

```sh
docker run --rm -it your-image
```

The worker is configured but not started by nanoinit. Run it yourself when you
need it:

```sh
/usr/local/bin/worker --queue default --debug
```

### Override config at runtime

```sh
docker run --rm \
    -e NANOINIT_CONFIG_FILE=/tmp/debug-config.json \
    -v "$PWD/debug-config.json:/tmp/debug-config.json:ro" \
    your-image
```

## Troubleshooting

### Nothing starts

Check that:

- `--config-file` points to an existing file
- `--config-json-object` points to the object that contains app entries
- every app has a `path`
- `path` is correct from inside the container
- executable permissions are set on each binary
- verbosity is high enough to show useful logs: `--verbose=2`

### Arguments are not split

`nanoinit` does not invoke a shell. This means:

```json
"args": "--host 0.0.0.0 --port 8080"
```

is one argument, not four.

Use an array:

```json
"args": ["--host", "0.0.0.0", "--port", "8080"]
```

### An autostart-disabled app still starts

Check that the application entry uses the current field name and a boolean
value:

```json
"autostart": false
```

If this field is misspelled or set as a string, config parsing fails and
nanoinit falls back to zero-config behavior.

### Output files are not created

Check that the parent directory exists and that the process user can write to it.
`nanoinit` opens configured output files directly; it does not create missing
parent directories.

### Passthrough output is not visible

Check that:

- `stdout_passthrough` or `stderr_passthrough` is set to `true`
- the matching `stdout` or `stderr` field is a non-empty file path
- the passthrough field is a boolean, not a string

Passthrough is not needed when `stdout` or `stderr` is omitted because the
application already inherits nanoinit's stream. Passthrough is ignored when the
stream is redirected to `/dev/null` with `""`.

### Log rotation does not happen

Check that:

- `stdout` or `stderr` is set to a non-empty file path
- the matching `*_rotate_size` field is greater than `0`
- the rotate fields are integers, not strings
- the process writes enough data to reach the configured byte limit
- the process writes a newline after reaching the limit

Rotation is not applied to inherited terminal output or to streams redirected to
`/dev/null`.

### Reload does not work

Make sure the target nanoinit process is visible in `/proc` from where
`nanoinit --reload` is executed. In containers, PID namespaces can hide the
target process.

You can also send the signal directly if you know the PID:

```sh
kill -USR1 <nanoinit-pid>
```

## Project layout

```text
.
|-- .github/
|   `-- workflows/
|-- README.md
|-- TODO.md
|-- build-releases.sh
|-- deploy/
|   |-- Dockerfile.alpine
|   `-- Dockerfile.ubuntu
|-- source/
|   |-- arguments.c
|   |-- config.c
|   |-- control.c
|   |-- log.c
|   |-- log_formatter.c
|   |-- main.c
|   |-- nanoinit.c
|   |-- supervisor.c
|   |-- supervisor/
|   |   |-- app_logging.c
|   |   |-- control_server.c
|   |   |-- internal.h
|   |   |-- io.c
|   |   `-- lifecycle.c
|   `-- edJSON/
`-- test/
    |-- common.py
    |-- test-config.json
    `-- test_*.py
```

Important source files:

- `source/arguments.c`: command-line arguments and environment overrides
- `source/config.c`: JSON parsing and config validation
- `source/control.c`: runtime control command client
- `source/supervisor.c`: top-level supervision loop, signals, reloads
- `source/supervisor/lifecycle.c`: application spawn, reap, and autorestart
- `source/supervisor/app_logging.c`: application output capture, passthrough, and log rotation
- `source/supervisor/io.c`: select loop for control sockets and output pipes
- `source/supervisor/control_server.c`: runtime control socket server
- `source/supervisor/internal.h`: internal supervisor shared types and functions
- `source/nanoinit.c`: reload helper
- `source/log.c`: nanoinit logging
- `source/log_formatter.c`: reusable log line formatter

## License

MIT. See [LICENSE](LICENSE).
