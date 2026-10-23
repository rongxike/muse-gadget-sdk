using System.Text;
using MuseCompanion.Ble;

namespace MuseCompanion;

public partial class MainPage : ContentPage
{
    private readonly MuseBleService _ble;
    private readonly StringBuilder _log = new();

    public MainPage(MuseBleService ble)
    {
        InitializeComponent();
        _ble = ble;
        _ble.Log += OnLog;
        _ble.StatusChanged += OnStatusChanged;
        _ble.HandshakeChanged += OnHandshakeChanged;
    }

    private async void OnScanClicked(object? sender, EventArgs e)
    {
        if (!await EnsurePermissionsAsync())
        {
            OnLog("Bluetooth permissions were denied.");
            return;
        }

        ScanButton.IsEnabled = false;
        try
        {
            await _ble.ScanAndConnectAsync();
        }
        finally
        {
            ScanButton.IsEnabled = true;
            DisconnectButton.IsEnabled = _ble.IsConnected;
        }
    }

    private async void OnDisconnectClicked(object? sender, EventArgs e)
    {
        await _ble.DisconnectAsync();
        DisconnectButton.IsEnabled = false;
    }

    private void OnLog(string message) =>
        MainThread.BeginInvokeOnMainThread(() =>
        {
            _log.Insert(0, message + "\n");
            LogLabel.Text = _log.ToString();
        });

    private void OnStatusChanged(string? status) =>
        MainThread.BeginInvokeOnMainThread(() =>
        {
            StatusLabel.Text = status ?? "Disconnected";
            DisconnectButton.IsEnabled = _ble.IsConnected;
        });

    private void OnHandshakeChanged(bool done) =>
        MainThread.BeginInvokeOnMainThread(() =>
            HandshakeLabel.Text = done ? "Handshake: complete \u2713" : "Handshake: not started");

    private static async Task<bool> EnsurePermissionsAsync()
    {
#if ANDROID
        var status = await Permissions.CheckStatusAsync<BlePermissions>();
        if (status != PermissionStatus.Granted)
        {
            status = await Permissions.RequestAsync<BlePermissions>();
        }
        return status == PermissionStatus.Granted;
#else
        return await Task.FromResult(true);
#endif
    }
}
