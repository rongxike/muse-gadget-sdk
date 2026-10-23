# Muse Companion (mobile)

A minimal, cross-platform [.NET MAUI](https://learn.microsoft.com/dotnet/maui/)
app that connects to a Muse gadget over Bluetooth Low Energy and runs a simple
`hello` / `hi` handshake.

The first build targets **Android only**. iOS support is intentionally left for
later (see [Adding iOS later](#adding-ios-later)).

## What it does

1. Scans for a BLE device advertising a Muse name (`MuseGadget-…`,
   `HomeLink-…`, or `Muse-…`).
2. Connects and shows **Connected to &lt;device&gt;** once the link is up.
3. Subscribes to the handshake service. The device sends **`hello`** as soon as
   the app subscribes.
4. The app automatically replies **`hi`**, completing the handshake.

The UI is deliberately plain: a status panel, **Scan & Connect** / **Disconnect**
buttons, and a running log.

## How the two sides find each other and shake hands

The device (firmware) and the app agree on a dedicated, unauthenticated
"handshake" GATT service so a basic app works without pairing/passkeys. The
UUIDs are defined in both places and must stay in sync:

| Role                 | UUID                                     | Properties     |
| -------------------- | ---------------------------------------- | -------------- |
| Handshake service    | `4d757365-0010-4000-8000-6a6f6c6c7900`   | —              |
| `HELLO` (device→app) | `4d757365-0011-4000-8000-6a6f6c6c7900`   | read, notify   |
| `REPLY` (app→device) | `4d757365-0012-4000-8000-6a6f6c6c7900`   | write          |

- Firmware: `esp32/components/muse/muse_ble.c` (the `HS_*` UUIDs and the
  `on_handshake` callback). The device pushes `hello` from
  `BLE_GAP_EVENT_SUBSCRIBE` when the app enables notifications on `HELLO`.
- App: `MuseCompanion/Ble/MuseBleConstants.cs` and `MuseBleService.cs`.

Because the handshake service carries no credentials, it uses no encryption or
authentication flags — the existing secured Muse phone-setup service is
untouched and still requires passkey pairing.

## Build & run (Android)

Prerequisites:

- .NET 8 SDK with the MAUI workload: `dotnet workload install maui-android`
- An Android device or emulator (physical device recommended for BLE)

```bash
cd mobile
dotnet build MuseCompanion/MuseCompanion.csproj -f net8.0-android

# Deploy to a connected device/emulator
dotnet build MuseCompanion/MuseCompanion.csproj -f net8.0-android -t:Run
```

Grant the Bluetooth permission prompt on first launch, power on the Muse
device, then tap **Scan & Connect**.

## Adding iOS later

1. Add `net8.0-ios` to `<TargetFrameworks>` in `MuseCompanion.csproj`.
2. Add a `Platforms/iOS` folder (`AppDelegate.cs`, `Program.cs`, `Info.plist`
   with `NSBluetoothAlwaysUsageDescription`).
3. The shared `Ble/` code and UI need no changes — Plugin.BLE is
   cross-platform.
