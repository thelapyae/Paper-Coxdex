# Paper Coxdex

Paper Coxdex turns an M5Stack PaperS3 into a Bluetooth Codex Micro-compatible touch
controller. It presents six agent slots, six command keys, live agent state on
the e-ink display, and buzzer alerts for completion, input requests, and errors.

This is an independent compatibility project. It is not official OpenAI,
Codex Micro, M5Stack, or Work Louder firmware.

## What works in this MVP

- Bluetooth vendor HID connection to the Codex desktop app
- Six touch agent keys (`AG00` to `AG05`)
- Default command keys: Fast, Approve, Reject, Split, Voice, and Codex
- E-ink agent states: Idle, Thinking, Complete, Needs Input, and Error
- Different buzzer patterns for Complete, Needs Input, and Error
- Battery and connection status in the top bar
- High-contrast E-Ink UI inspired by Codex Micro's raised keycap layout
- Large agent numbers, state symbols, thick borders, and a wide Voice key
- Full-screen Accept/Reject decision popup when Codex needs input
- Model cycle button using Codex's native model picker and keyboard controls
- Six-notch Thinking slider backed by Codex Micro encoder events
- Automatic portrait and landscape layouts using the PaperS3 IMU

The PaperS3 has no physical keys, rotary encoder, RGB LEDs, or joystick. This
version uses the touch display for keys and maps Codex's RGB state colors into
text and grayscale cards.

## Build

PlatformIO is required. From this folder:

```sh
pio run -e papers3
```

## Flash

1. Connect the PaperS3 to the Mac with a data-capable USB-C cable.
2. Power it on with one click of the side button.
3. If no serial port appears, hold the side button until the rear status LED
   flashes red. That is download mode.
4. Run:

```sh
./scripts/detect-device.sh
./scripts/flash.sh
```

## Pair with the Mac and Codex

1. Open macOS **System Settings > Bluetooth**.
2. Pair **Paper Micro**.
3. Open or restart Codex.
4. If macOS asks, enable Codex under **System Settings > Privacy & Security >
   Input Monitoring**. Codex needs this permission to read vendor HID keys.
5. The PaperS3 top bar should change to **LIVE**, and Codex should
   show its Codex Micro onboarding or settings.

If an earlier pairing exists after a firmware protocol change, forget
**Paper Micro** in Bluetooth settings and pair it again.

## Controls

- **MODEL** opens Codex's model picker, moves to the next model, and confirms it.
- **THINKING** maps Low, Medium, High, XHigh, Max, and Ultra to the
  Codex Micro reasoning encoder. The first touch synchronizes at Low before
  moving to the chosen level.
- **ACCEPT / REJECT** remain available as normal keys and become a large
  decision popup whenever an agent enters the Needs Input state.
- **NEW, VOICE, CODEX** start a new chat, control push-to-talk, and submit.
- Rotate the device and hold it steady briefly to switch through all four
  orientations: portrait, landscape, reverse portrait, and reverse landscape.
  Orientation changes use a full E-Ink refresh to prevent stale rotated pixels.
  If the device is flat on a desk, it keeps the last orientation.

## Safety note

PaperS3 units earlier than v1.1/v1.2 have a documented USB charging warning.
Use a normal 5 V computer USB port and avoid QC 2.0/3.0 high-voltage chargers.

## Compatibility note

The Codex integration is not a public stable protocol. This MVP implements the
locally installed Codex desktop app's HID discovery and JSON-RPC framing as of
August 11, 2026. A future Codex update may require a firmware adjustment.

## Contributing

Issues and pull requests are welcome. Please keep the UI readable in black and
white, preserve touch targets, and test both Bluetooth reconnect and Codex
status updates before submitting changes.

## License

MIT. See `LICENSE`.
