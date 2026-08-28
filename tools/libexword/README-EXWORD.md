# Local libexword changes and build

Baseline: <https://github.com/brijohn/libexword>, branch `2.0-dev`, commit
`d186e356d942ea83c89480dc9d2110c1dbe520ef`.

This copy adds the portability behavior used for the XD-B8600 work:

- Undefine a platform `ntohll` macro before defining libexword's function.
- Honor `EXWORD_DATA_DIR` so private data can live under this repository's
  ignored `build/` directory instead of a global installation.
- Compile with `-fsigned-char`. The old OBEX implementation uses plain `char`
  in binary protocol buffers; forcing signedness keeps behavior consistent
  between host compilers.
- Fix the command-line client's subcommand/username allocations and return a
  defined value from its readline event hook.

## Build the transfer client

On macOS, install the native dependencies with Homebrew:

```sh
brew install pkg-config libusb readline
./scripts/build-libexword.sh
```

On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libreadline-dev
./scripts/build-libexword.sh
```

The build script compiles the command-line client directly, avoiding the
upstream project's optional and obsolete Python-2/SWIG build path. Run it as:

```sh
./scripts/run-libexword.sh
```

For payload transfer, put the EX-word in USB text-transfer mode and enter:

```text
connect text
send artifacts/LINUX.PAY
disconnect
exit
```

`scripts/run-libexword.sh` sets `EXWORD_DATA_DIR` to
`build/exword-data`. Loader packages produced by `build-loader.sh` or
`stage-loader-artifact.sh` are staged under that same directory for
`dict install LNX03` in library mode. Authentication material and command
history stay below ignored `build/` and must never be committed.

The source remains GPL-2.0-or-later under the upstream notices.
