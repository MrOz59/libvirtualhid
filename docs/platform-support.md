# Platform support

The public C++ API is shared across platforms, but device availability and
feedback depend on the installed backend and selected profile. Query runtime
and effective profile capabilities before enabling optional features.

| Platform | Gamepads                                                                                           | Other devices                                                                                          | Requirements                                                             |
|----------|----------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------|
| Windows  | Generic, Xbox, PlayStation, Switch Pro                                                             | Driver-backed keyboard and relative mouse; Win32 keyboard, text, and mouse fallback                    | AMD64 UMDF package and machine license for driver-backed devices         |
| Linux    | Generic and Xbox 360 through `uinput`; PlayStation, Switch Pro, Xbox One and Series through `uhid` | Keyboard, mouse, touchscreen, trackpad, pen tablet through `uinput`; XTest keyboard and mouse fallback | Writable device nodes and required kernel modules                        |
| FreeBSD  | Built-in profiles through `uinput`                                                                 | Keyboard, mouse, touchscreen, trackpad, pen tablet through `uinput`; XTest fallback                    | Writable `uinput` device node                                            |
| macOS    | Built-in profiles through the virtual HID broker                                                   | CoreGraphics keyboard and mouse                                                                        | Signed broker, Apple entitlement, system permission, and machine license |

## Windows

Install the [Windows driver package](windows-driver.md) to create gamepads
and Raw Input-visible keyboard or relative mouse devices. Without the package
or a valid license, supported keyboard and mouse operations use Win32
fallbacks. Unicode text and absolute mouse movement use Win32 even when the
driver is installed.

Xbox 360 appears to XInput through the package's XUSB companion. The other
profiles use virtual HID. Battery values submitted for Xbox One and Series
can be read through HID, but XInput and applications relying on its battery
API do not receive the remote value. Steam may hide battery indicators for
wired virtual devices. Windows PlayStation devices use USB report framing,
even when the public profile requested Bluetooth.

The published installer targets Windows 11 version 21H2 or later on AMD64.
Windows 10 version 2004 or later has a best-effort path. Windows ARM64 release
packages are not available.

## Linux

Linux needs `uhid` for descriptor-driven profiles and `uinput` for the other
device paths. Xbox One and Series can fall back to `uinput` when `uhid` is
unavailable; that fallback retains ordinary input and rumble but loses native
trigger rumble and battery notifications. FreeBSD uses `uinput` for all
profiles and does not expose descriptor-driven PlayStation, Switch Pro, or
Xbox features.

Consumers without hidraw access can set `CreateGamepadOptions::prefer_input_events`
to create generic and Xbox controllers through uinput even when UHID is
available. This retains ordinary evdev rumble and the requested controller
family, but not native trigger rumble or battery notifications. Linux returns
`unsupported_profile` for this preference with PlayStation or Switch profiles,
instead of silently creating an inaccessible native HID device. The default
remains native HID with the existing uinput fallback for Xbox controllers.

Install persistent rules such as `/etc/udev/rules.d/60-libvirtualhid.rules`
for the account running the host application:

```udev
KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", GROUP="input", MODE="0660", TAG+="uaccess"
KERNEL=="uhid", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", IMPORT{parent}="HID_*"
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ENV{HID_PHYS}=="libvirtualhid/uhid/*", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{phys}=="libvirtualhid/uhid/*", GROUP="input", MODE="0660", TAG+="uaccess"
```

The `hidraw` rule imports `HID_PHYS` from the HID parent before matching it.
A one-time `chmod` on a generated device node will not survive device
recreation. Load `uhid` and `uinput`, and load `hid_playstation` when using
the kernel feedback path for PlayStation controllers:

```sh
sudo modprobe -a uhid uinput hid_playstation
sudo udevadm control --reload-rules
sudo udevadm trigger --property-match=DEVNAME=/dev/uinput
sudo udevadm trigger --property-match=DEVNAME=/dev/uhid
sudo udevadm trigger --subsystem-match=hidraw
sudo udevadm trigger --subsystem-match=input
```

The consuming user may need membership in the `input` group and a new login
session. Desktop logins may instead receive access through `uaccess`.

For independent sessions, set `DeviceProfile::linux_phys` to the session's
physical identity on every profile and keep runtimes and device handles owned
by that session. Explicit identities prevent keyboard/mouse XTest fallback
and require exact physical matching during node discovery. PlayStation drivers
can leave their input children's `phys` empty and rename motion/touchpad
devices; discovery therefore also checks the owning HID parent's `HID_PHYS`
and controller identity. It excludes another session's children even when
their names match. The application must provide udev rules that assign the
matching `ID_SEAT` to uinput devices and propagate it from UHID parents to
their input, joystick and hidraw children. A physical identity alone does not
assign a seat or grant/restrict device access.

Linux Switch Pro initialization replies use a device-specific MAC derived
from the supplied stable identity and session physical identity. Supplying an
explicit colon-separated MAC uses that MAC directly. USB status and device
information replies advertise the same identity, so multiple virtual Switch
controllers no longer share a fixed protocol MAC.

Virtual Xbox One and Series controllers use `uhid` with a kernel-only vendor
marker so physical-controller drivers such as `xpadneo` cannot intercept
Guide-button chords. SDL and the evdev/hidraw interfaces retain the Xbox
vendor and product IDs. Trigger rumble and battery reporting remain available;
no change to xpadneo's physical-controller settings is required.

## FreeBSD

The `uinput` kernel module and a writable `/dev/input/uinput` or
`/dev/uinput` device are required. Generic and Xbox-family profiles provide
ordinary controls and rumble. PlayStation and Switch Pro profiles do not expose
Linux `uhid` features such as motion, battery, or native output reports.

## macOS

Gamepads require the installed, signed, licensed broker and macOS permission
to create virtual HID devices. Keyboard and mouse use CoreGraphics and may
require synthetic-input permission. Touchscreen, trackpad, and pen tablet
creation are unavailable. See [macOS setup](macos-gamepad.md) for installation
and diagnostics.

A game recognizing a profile still depends on that game's input stack. For
streaming, see the [end-user compatibility matrix](end-user-gamepad-guide.md#compatibility-matrix).
