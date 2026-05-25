# nanoinit

`nanoinit` is a small Linux process supervisor designed for Docker containers.
It starts one or more configured applications, keeps selected applications alive
with optional autorestart, forwards termination signals, and can reload its
configuration while the container is running.

The project is intentionally small: configuration is JSON, application startup is
done with `fork()` and `execv()`, and there are no runtime service-manager
dependencies.

## Contents

- [What nanoinit does](#what-nanoinit-does)
- [Quick start](#quick-start)
- [Building](#building)
- [Using nanoinit in Docker](#using-nanoinit-in-docker)
- [Command-line arguments](#command-line-arguments)
- [Environment variables](#environment-variables)
- [Configuration file](#configuration-file)
- [Manual mode](#manual-mode)
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
- automatically restarting selected processes when they exit
- disabling selected processes during debugging with manual mode
- reloading the configuration without replacing the container

`nanoinit` is not a general-purpose service manager. It does not implement
dependency ordering, health checks, privilege switching, environment files, start
limits, backoff policies, or per-application control commands.

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
        "manual": true
    }
}
```

Run `nanoinit` with that file:

```sh
./nanoinit --config-file=/etc/nanoinit/config.json --verbose=2
```

In normal mode, both `web` and `worker` are started. In manual mode, `web` is
started but `worker` is skipped because it is marked with `"manual": true`:

```sh
NANOINIT_MANUAL_MODE=1 ./nanoinit --config-file=/etc/nanoinit/config.json
```

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

For debugging, enable manual mode at container runtime:

```sh
docker run --rm -e NANOINIT_MANUAL_MODE=1 your-image
```

This is usually more practical than baking `--manual-mode` into the Dockerfile,
because you can turn manual mode on only for debug runs.

## Command-line arguments

### `-c`, `--config-file=/path/to/config.json`

Path to the JSON configuration file.

If this argument is omitted, no configuration is loaded. `nanoinit` starts no
applications and remains running until it receives a stop signal. This is useful
for keeping a container alive while debugging its filesystem or environment.

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

### `-m`, `--manual-mode`

Enables manual mode. See [Manual mode](#manual-mode).

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

## Environment variables

Environment variables are useful in Docker because they can be supplied at
runtime without changing the image.

### `NANOINIT_MANUAL_MODE`

Enables manual mode when set to any value.

Examples:

```sh
NANOINIT_MANUAL_MODE=1 ./nanoinit -c config.json
```

```sh
docker run -e NANOINIT_MANUAL_MODE=1 your-image
```

Only presence is checked. The actual value is not parsed as a boolean.

### `NANOINIT_CONFIG_FILE`

Overrides `--config-file`.

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

## Configuration file

The configuration file is JSON parsed by the bundled `edJSON` parser. JSON
comments are supported.

At the selected configuration object, every property name is treated as an
application name. Each application value must be an object.

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
    "app": {
        "path": "/usr/local/bin/app",
        "args": ["--listen", "0.0.0.0:8080"],
        "autorestart": true,
        "manual": false,
        "stdout": "/var/log/app.stdout.log",
        "stderr": "/var/log/app.stderr.log"
    }
}
```

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

There is currently no restart delay, maximum retry count, or backoff policy.

`manual`

Optional boolean. Default: `false`.

When `true`, the application is skipped if nanoinit is running in manual mode.
In normal mode, the application starts like any other configured application.

`stdout`

Optional string. Redirects the application's standard output.

- omitted: inherit nanoinit's `stdout`
- non-empty string: write `stdout` to that path
- empty string `""`: redirect `stdout` to `/dev/null`

`stderr`

Optional string. Redirects the application's standard error.

- omitted: inherit nanoinit's `stderr`
- non-empty string: write `stderr` to that path
- empty string `""`: redirect `stderr` to `/dev/null`

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
        "manual": true
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

## Manual mode

Manual mode is designed for debugging.

Sometimes a container normally starts several applications, but during debugging
you want one of them to stay stopped so you can run it by hand with different
arguments, under a debugger, or from an interactive shell. Mark that application
as manual:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "autorestart": true
    },
    "worker": {
        "path": "/usr/local/bin/worker",
        "manual": true
    }
}
```

Normal run:

```sh
nanoinit -c config.json
```

Result:

- `api` starts
- `worker` starts

Manual-mode run:

```sh
NANOINIT_MANUAL_MODE=1 nanoinit -c config.json
```

Result:

- `api` starts
- `worker` is skipped

Manual mode does not start an interactive shell and does not change the config
file. It only filters out applications marked with `"manual": true`.

## Logging and output redirection

There are two kinds of output to consider:

- nanoinit's own logs
- each supervised application's `stdout` and `stderr`

Use `--verbose` to control how much nanoinit prints to the terminal. Use
`--log-path` to also write nanoinit's own logs to a file.

Use `stdout` and `stderr` in the config file to redirect application streams.
Relative output paths are resolved relative to nanoinit's current working
directory.

Example:

```json
{
    "api": {
        "path": "/usr/local/bin/api",
        "stdout": "/var/log/api.stdout.log",
        "stderr": "/var/log/api.stderr.log"
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
- `worker` output is discarded through `/dev/null`
- nanoinit's own logs still follow `--verbose` and `--log-path`

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

No config file is loaded, no applications are started, and nanoinit waits for a
stop signal.

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
        "manual": true
    }
}
```

```sh
docker run --rm -it -e NANOINIT_MANUAL_MODE=1 your-image
```

Inside the container, run the worker yourself:

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

### A manual app still starts

Manual mode must be enabled globally and the app must be marked manual.

The app entry needs:

```json
"manual": true
```

The nanoinit process needs either:

```sh
nanoinit --manual-mode -c config.json
```

or:

```sh
NANOINIT_MANUAL_MODE=1 nanoinit -c config.json
```

### Output files are not created

Check that the parent directory exists and that the process user can write to it.
`nanoinit` opens configured output files directly; it does not create missing
parent directories.

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
|-- README.md
|-- TODO.md
|-- build-releases.sh
|-- deploy/
|   |-- Dockerfile.alpine
|   `-- Dockerfile.ubuntu
|-- source/
|   |-- arguments.c
|   |-- config.c
|   |-- log.c
|   |-- main.c
|   |-- nanoinit.c
|   |-- supervisor.c
|   `-- edJSON/
`-- test/
    |-- config1.json
    |-- config2.json
    |-- config3.json
    `-- config4.json
```

Important source files:

- `source/arguments.c`: command-line arguments and environment overrides
- `source/config.c`: JSON parsing and config validation
- `source/supervisor.c`: process spawning, supervision, manual mode, signals
- `source/nanoinit.c`: reload helper
- `source/log.c`: nanoinit logging

## License

MIT. See [LICENSE](LICENSE).
