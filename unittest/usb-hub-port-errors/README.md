# USB hub port error regression tests

Run from the Circle root with Python 3 and a C++17 compiler supporting
AddressSanitizer and UndefinedBehaviorSanitizer:

```sh
python3 unittest/usb-hub-port-errors/run.py
python3 unittest/usb-hub-port-errors/run.py --source /path/to/other/circle
```

`CXX` selects the compiler. The harness compiles the actual
`lib/usb/usbstandardhub.cpp` with the warnings of Circle's `Rules.mk`. Stubs
replace the host controller, the endpoint, the USB device and function, the
logger and the timer. The host controller stub models the ports of a two-port
hub: their status and change bits, the hub's status change interrupt and
requests that fail once on demand. No hardware is required.

## Failure and fix

When a hub reports a port status change, `HandlePortStatusChange()` reads the
status of each changed port and clears the change bits it finds. A failed
request in this sequence was a `LogPanic`, so one transaction error on the
hub's control endpoint halted the system. On a Raspberry Pi 4 such an error was
observed on the routine clear of `C_PORT_CONNECTION` at the first status change
after enumeration, and the system stopped during startup.

The fix logs the failure as an error of the hub device, naming the port and the
request, and goes on. A failed status read skips that port, and a failed clear
leaves the change bit set. The hub reports a change bit again until it has been
cleared (USB 2.0, 11.12.1 and 11.12.4), so the change is handled at the next
status change interrupt.

## Coverage and results

Six cases cover a clear without errors, a failed clear of `C_PORT_CONNECTION`,
the same change handled when it is reported again, failed clears of
`C_PORT_ENABLE` and `C_PORT_RESET`, a failed status read on one of two changed
ports followed by the device found on that port, and a disconnect whose clear
fails.

| Source | Passed | Failed |
| --- | ---: | ---: |
| Unmodified develop 6007708 | 1 | 5 |
| With this patch | 6 | 0 |

Each failing case ends with the system halted at the `LogPanic` of the failed
request. ASan and UBSan are enabled.

## Limits

A failed read of the hub status itself, the first request of the handler, still
halts the system. It was not observed. A hub whose requests keep failing is
logged again at each of its status change interrupts. The stubs model no real
transfers, timing or controller recovery. That a later request succeeds after a
transaction error relies on the host controller recovering the endpoint, which
`ControlMessage()` does for the xHCI controller.
