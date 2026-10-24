# Streaming-host integration

`libvirtualhid` creates local virtual devices for a streaming host. The host
keeps its network protocol, client input mapping, controller assignment,
configuration, and feedback transport.

Use [`GamepadStateAdapter` in the example](../examples/gamepad_adapter.cpp)
as the integration pattern:

1. Choose a built-in profile and stable controller metadata.
2. Create one gamepad per active client controller and submit an initial
   neutral state.
3. Apply button, axis, trigger, touch, motion, and battery updates as the
   client sends them.
4. Forward output callbacks, such as rumble and LEDs, to the client when the
   selected profile and backend support them.
5. Destroy the gamepad on disconnect; recreate it after a broker restart or
   other loss of the local device.

Check runtime and effective profile capabilities before advertising optional
features. `DeviceNode` paths can help diagnose host-side enumeration; client
feature support still depends on the physical controller, connection, client,
and game. See the [end-user compatibility matrix](end-user-gamepad-guide.md#compatibility-matrix)
for observed streaming behavior.

On Linux, `device_nodes()` identifies the nodes owned by one handle: uinput
uses its exact sysfs input directory, and UHID uses the HID parent's physical
and unique identities. A seat tag alone is insufficient when several identical
controllers share the same session. Missing metadata returns no match; retry
discovery after the kernel finishes creating children. Closed handles return
no nodes. Treat returned paths as snapshots, not authorization: a privileged
broker must open and verify each current character device and its sysfs
identity before granting access, and revoke that access when the session ends.

Route `RuntimeOptions::log_callback` into the host logger for lifecycle,
failure, and debug-level input coordinate diagnostics. When streaming one
monitor from a multi-monitor desktop, supply the full desktop bounds and the
selected viewport in `CreateMouseOptions`. See the
[mouse viewport setup](usage.md#absolute-mouse-viewports).
Use native screen units for that geometry: screen points on macOS and desktop
pixels on Windows, Linux, and FreeBSD. The captured image dimensions supplied
to `Mouse::move_absolute()` can differ from the viewport dimensions.
