# Installing and booting on an XD-B8600

These instructions cover a new clone, including installation of the LNX03
Casio add-in and transfer of `LINUX.PAY`. They apply only to the Japanese
XD-B8600/DATAPLUS 6 that this port was developed on.

> [!CAUTION]
> Remove the SD card before booting Linux. SDHI is intentionally disabled
> because the inherited clock tree is not understood yet. Do not bind the
> SDHI driver manually. Do not use libexword's `format` command. The v9
> secondary-display/touch helpers contain raw addresses measured only on the
> XD-B8600; never boot this payload or run those helpers on another model.

## 1. Clone and verify

```sh
git clone https://github.com/AiSlopUtils/XD-B8600-linux.git
cd XD-B8600-linux
make verify
```

This verifies every supplied artifact and recreates `LINUX.PAY` from the
checked-in zImage and X11 image byte-for-byte.

## 2. Build the USB client

The transfer client is a native host program. On macOS with Homebrew:

```sh
brew install pkg-config libusb readline
make usb-tool
```

On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libusb-1.0-0-dev libreadline-dev
make usb-tool
```

If a Linux host reports permission denied while opening USB device
`07cf:6101`, install the included udev rule and reconnect the EX-word:

```sh
sudo install -m 0644 tools/udev/60-exword.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

The resulting program is `build/libexword/exword`. Always start it through
`./scripts/run-libexword.sh`; the wrapper keeps its models, history, and
private authentication database under the ignored `build/exword-data`
directory.

## 3. Install the LNX03 add-in once

Stage the supplied, known-good loader package without compiling it:

```sh
make loader-package
```

This creates `build/exword-data/ja/LNX03`. If LNX03 is already installed on
the dictionary, skip the rest of this section.

Put the EX-word in its add-on/library USB connection mode, then run:

```sh
./scripts/run-libexword.sh
```

At the `>>` prompt, authenticate with the library identity already used
by the device, install the package, and disconnect:

```text
connect library ja
dict auth YOUR_EXISTING_USER
dict install LNX03
disconnect
exit
```

`dict auth` without a key reads the private
`build/exword-data/users.dat` database. If you already use libexword, copy
your existing `users.dat` there before starting the wrapper. Never add that
file to Git. If you have the device's existing 20-byte authentication key but
not that database, authenticate without changing anything on the dictionary:

```text
dict auth YOUR_EXISTING_USER 0xYOUR_40_HEXADECIMAL_DIGITS
```

> [!WARNING]
> Do not use `dict reset` merely to make authentication work. Upstream
> libexword documents that reset deletes installed add-ons. Use it only if
> you understand that consequence and intend to reset the device's add-on
> library identity.

To prove the loader can also be rebuilt exactly from source, install Docker
and run `make loader`. That path downloads a checksum-pinned devkitSH4 SDK,
builds in a Linux/amd64 container, compares the result with the supplied
artifact, and stages the same installable directory.

## 4. Choose the Linux payload

There are two supported paths:

- `artifacts/LINUX.PAY` is the checked-in, hash-verified recovery/reference
  payload. `make verify` checks it without a target build.
- `build/LINUX-from-source.PAY` is the complete source-built payload with the
  working v9 AwesomeWM desktop. Build it with Docker using:

    ```sh
    JOBS=4 make all
    ```

The v9 source path includes one AwesomeWM desktop, the Start menu and
`exdesk` utilities, the one-instance xterm wrapper, text-only screenfetch,
and the userspace secondary-LCD/touch mouse. Earlier hash-verified milestones
remain under `artifacts/milestones/` for regression testing. Keep the SD card
removed for every payload documented here.

## 5. Transfer and read back the payload

### Checked-in recovery payload

Create a separate readback directory so the verification download cannot
overwrite the repository artifact:

```sh
mkdir -p build/readback
cd build/readback
../../scripts/run-libexword.sh
```

Put the EX-word in text-transfer USB mode and enter:

```text
connect text ja
send ../../artifacts/LINUX.PAY
get LINUX.PAY
disconnect
exit
```

Return to the repository root and compare the upload with its readback:

```sh
cd ../..
cmp artifacts/LINUX.PAY build/readback/LINUX.PAY
```

No output from `cmp` means they are identical. You can also rerun
`make verify` to check the source artifact's recorded SHA-256.

### Source-built v9 payload

Keep the outgoing file separate from the readback file:

```sh
mkdir -p build/upload-v9 build/readback-v9
cp build/LINUX-from-source.PAY build/upload-v9/LINUX.PAY
cd build/readback-v9
../../scripts/run-libexword.sh
```

In text-transfer USB mode, enter:

```text
connect text ja
send ../upload-v9/LINUX.PAY
get LINUX.PAY
disconnect
exit
```

Return to the repository root and require an exact match:

```sh
cd ../..
cmp build/upload-v9/LINUX.PAY build/readback-v9/LINUX.PAY
```

Do not boot if `cmp` prints a difference or returns a failure status.

## 6. Boot

1. Safely leave USB mode and disconnect the cable.
2. Confirm that the SD slot is empty.
3. Launch the LNX03 add-in from the EX-word menu.
4. Release all keys, then press Enter at the startup-check screen to inspect
   `LINUX.PAY`.
5. When the loader says the payload is verified and ready, press Enter to arm
   it.
6. Check that the screen now says `ARMED`, then press Enter once more to
   transfer control. X-capable builds take noticeably longer than a normal
   add-in to start.

At the BusyBox prompt, run `startx`. The v9 image starts Xfbdev, draws the
six-button secondary pad, starts its supervised touch-to-pointer helper, and
then launches AwesomeWM. The first X startup is slow on this 16 MiB system;
wait for the desktop and Start bar instead of repeatedly pressing keys.

The loader and Linux payload run from RAM and do not replace Casio firmware
or write NOR flash.

## 7. Using the v9 desktop

The desktop deliberately has one workspace. Click the Start button in the
bottom-left corner to open:

- File Manager;
- Command Prompt;
- Task Manager;
- System Information;
- Disk Information;
- Clock; and
- Eyes.

The secondary screen is divided into left/up/right across the top and
left-click/down/right-click across the bottom. The keyboard's Power key still
toggles the main-keypad pointer mode. With that mode enabled, arrows move and
Enter/Zoom/Back click left/middle/right. Turn keyboard pointer mode off and
press Back+Q to exit AwesomeWM.

Only one xterm can run at once. If a terminal-backed Start item appears to do
nothing, close the current xterm first. This limit prevents repeated launches
from exhausting RAM. `/tmp/xterm.log` records the most recent terminal exit;
`/tmp/subpad-mouse.log` records secondary-pad startup diagnostics.

If the secondary LCD is not drawn or touch does not move the pointer, leave X
with Back+Q and inspect the logs from the text console. Do not run the raw
secondary-display or ADC diagnostics from xterm, do not enable an experimental
kernel subpad driver, and do not retry MMIO helpers on a different EX-word
model.
