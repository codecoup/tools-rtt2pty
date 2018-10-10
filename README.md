# RTT to PTY bridge

This utility allows you to form a bridge between Segger's
[Real Time Transfer (RTT)](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)
and a pseudo-terminal interface (PTY).

It behaves similarly to common USB to serial adapters, instantiating a PTY
device while the utility is running, allowing you to send and receive data
through the device using any number of common utilities (`minicom`, etc.).

## Requirements

Since RTT is a proprietary standard, it will only work with a Segger J-Link or
compatible device, and you will need to download the appropriate jlink arm
library (`libjlinkarm.so` on Linux, `libjlinkarm.dylib` on OS X).

The application will search for `libjlinkarm.so` in common paths by default,
but you can specify the file location using the `-j <filename>` option.

## Key Parameters

The following command-line parameters are available (`rtt2pty --help`):

```
rtt2pty: Segger RTT to PTY bridge
rtt2pty [-opt <param>]

Options:
       -d <devname>      Segger device name
       -s <serial>       J-Link serial number
       -S <speed>        SWD/JTAG speed
       -b <name>         Buffer name
       -2                Enable bi-directional comms
       -j <filename>     libjlinkarm.so/dylib location
```

### Device Name (`-d <devname>`)

The Segger target device name string. The default value is `nrf52`.

**NOTE:** You can get a full list of device names by opening `JLinkExe` and
entering the following command: `expdevlist <path to output file>`.

### Serial Number (`-s <serial>`)

If you have more than one J-Link connected to your computer, the first device
found will be selected by default. You can indicate the device you intend to
connect to by supplying the J-Link's serial number, which is visible any time
you run `JLinkExe`.

### Bi-directional Communication (`-2`)

By default, the tool only allows communication in one direction (target to PC).
Setting the `-2` flag allows communication in both directions.

## Example

On OS X, you can run the following command to open a PTY device with
bi-directional communication over RTT:

```
$ ./rtt2pty -j libjlinkarm.dylib -2 1
Connected to:
  #################
  S/N: ############
Searching for RTT control block...
Using up-buffer #0 (size=1024)
Using down-buffer #0 (size=16)
PTY name is /dev/ttys003
```

You can then connect to the PTY using a tool like `minicom` as follows:

```
$ minicom -D /dev/ttys003
```
