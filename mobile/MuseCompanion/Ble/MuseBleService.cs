using System.Text;
using Plugin.BLE;
using Plugin.BLE.Abstractions.Contracts;
using Plugin.BLE.Abstractions.EventArgs;
using Plugin.BLE.Abstractions.Exceptions;

namespace MuseCompanion.Ble;

/// <summary>
/// Finds a Muse device over BLE, connects, and runs the "hello"/"hi" handshake.
///
/// Flow:
///   1. Scan for a device whose name starts with one of the Muse prefixes.
///   2. Connect and discover the handshake service.
///   3. Subscribe to the HELLO characteristic. The firmware pushes "hello" as
///      soon as we subscribe.
///   4. On receiving "hello", write "hi" back to the REPLY characteristic.
/// </summary>
public class MuseBleService
{
    private readonly IBluetoothLE _ble;
    private readonly IAdapter _adapter;

    private IDevice? _device;
    private ICharacteristic? _hello;
    private ICharacteristic? _reply;
    private bool _disconnecting;

    public MuseBleService()
    {
        _ble = CrossBluetoothLE.Current;
        _adapter = _ble.Adapter;
        _adapter.ScanTimeout = 15000;
        _adapter.DeviceDisconnected += OnDeviceDisconnected;
        _adapter.DeviceConnectionLost += OnDeviceDisconnected;
    }

    /// <summary>Human-readable log lines for the UI.</summary>
    public event Action<string>? Log;

    /// <summary>Connection status text (null while idle/disconnected).</summary>
    public event Action<string?>? StatusChanged;

    /// <summary>Raised true once the handshake completes.</summary>
    public event Action<bool>? HandshakeChanged;

    public bool IsConnected => _device?.State == Plugin.BLE.Abstractions.DeviceState.Connected;

    private void Emit(string message) => Log?.Invoke(message);

    public async Task ScanAndConnectAsync()
    {
        if (!_ble.IsOn)
        {
            Emit("Bluetooth is off. Please turn it on and try again.");
            StatusChanged?.Invoke("Bluetooth off");
            return;
        }

        await DisconnectAsync();

        StatusChanged?.Invoke("Scanning\u2026");
        Emit("Scanning for a Muse device\u2026");

        IDevice? found = null;
        void OnDiscovered(object? sender, DeviceEventArgs e)
        {
            var name = e.Device.Name;
            if (string.IsNullOrEmpty(name) || found is not null)
            {
                return;
            }
            if (MuseBleConstants.NamePrefixes.Any(p => name.StartsWith(p, StringComparison.OrdinalIgnoreCase)))
            {
                found = e.Device;
                Emit($"Found {name} ({e.Device.Id}).");
                _ = _adapter.StopScanningForDevicesAsync();
            }
        }

        _adapter.DeviceDiscovered += OnDiscovered;
        try
        {
            await _adapter.StartScanningForDevicesAsync();
        }
        finally
        {
            _adapter.DeviceDiscovered -= OnDiscovered;
        }

        if (found is null)
        {
            Emit("No Muse device found. Make sure it is powered on and advertising.");
            StatusChanged?.Invoke("Not found");
            return;
        }

        await ConnectAsync(found);
    }

    private async Task ConnectAsync(IDevice device)
    {
        try
        {
            StatusChanged?.Invoke($"Connecting to {device.Name}\u2026");
            await _adapter.ConnectToDeviceAsync(device);
            _device = device;
            Emit($"Connected to {device.Name}.");
            StatusChanged?.Invoke($"Connected to {device.Name}");

            var service = await device.GetServiceAsync(MuseBleConstants.HandshakeService);
            if (service is null)
            {
                Emit("Handshake service not found on this device.");
                StatusChanged?.Invoke("Unsupported device");
                return;
            }

            _hello = await service.GetCharacteristicAsync(MuseBleConstants.HelloCharacteristic);
            _reply = await service.GetCharacteristicAsync(MuseBleConstants.ReplyCharacteristic);
            if (_hello is null || _reply is null)
            {
                Emit("Handshake characteristics not found.");
                StatusChanged?.Invoke("Unsupported device");
                return;
            }

            _hello.ValueUpdated += OnHelloUpdated;
            await _hello.StartUpdatesAsync();
            Emit("Subscribed to HELLO, waiting for the device to say hello\u2026");
        }
        catch (DeviceConnectionException ex)
        {
            Emit($"Connection failed: {ex.Message}");
            StatusChanged?.Invoke("Connection failed");
        }
        catch (Exception ex)
        {
            Emit($"Error: {ex.Message}");
            StatusChanged?.Invoke("Error");
        }
    }

    private async void OnHelloUpdated(object? sender, CharacteristicUpdatedEventArgs e)
    {
        var text = Decode(e.Characteristic.Value);
        Emit($"Device: \"{text}\"");

        if (string.Equals(text, MuseBleConstants.Hello, StringComparison.OrdinalIgnoreCase))
        {
            await SendReplyAsync();
        }
    }

    private async Task SendReplyAsync()
    {
        if (_reply is null)
        {
            return;
        }
        try
        {
            await _reply.WriteAsync(Encoding.UTF8.GetBytes(MuseBleConstants.Reply));
            Emit($"App: \"{MuseBleConstants.Reply}\"");
            Emit("Handshake complete \u2713");
            HandshakeChanged?.Invoke(true);
        }
        catch (Exception ex)
        {
            Emit($"Failed to send reply: {ex.Message}");
        }
    }

    public async Task DisconnectAsync()
    {
        _disconnecting = true;
        HandshakeChanged?.Invoke(false);
        if (_hello is not null)
        {
            _hello.ValueUpdated -= OnHelloUpdated;
            try
            {
                await _hello.StopUpdatesAsync();
            }
            catch
            {
                // Ignore: the device may already be gone.
            }
            _hello = null;
        }
        _reply = null;

        if (_device is not null)
        {
            try
            {
                await _adapter.DisconnectDeviceAsync(_device);
            }
            catch
            {
                // Ignore: already disconnected.
            }
            _device = null;
        }
        StatusChanged?.Invoke(null);
        _disconnecting = false;
    }

    private void OnDeviceDisconnected(object? sender, DeviceEventArgs e)
    {
        _hello = null;
        _reply = null;
        _device = null;
        // During an intentional DisconnectAsync the events are raised there; avoid firing twice.
        if (_disconnecting)
        {
            return;
        }
        Emit($"Disconnected from {e.Device.Name}.");
        StatusChanged?.Invoke(null);
        HandshakeChanged?.Invoke(false);
    }

    private static string Decode(byte[]? value)
    {
        if (value is null || value.Length == 0)
        {
            return string.Empty;
        }
        return Encoding.UTF8.GetString(value).TrimEnd('\0', '\n', '\r');
    }
}
