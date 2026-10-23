using Microsoft.Maui.ApplicationModel;

namespace MuseCompanion;

/// <summary>
/// Runtime permissions needed to scan and connect over BLE. Android 12 (API 31)
/// split the old BLUETOOTH/location permissions into BLUETOOTH_SCAN and
/// BLUETOOTH_CONNECT; older versions still need location to scan.
/// </summary>
public class BlePermissions : Permissions.BasePlatformPermission
{
    public override (string androidPermission, bool isRuntime)[] RequiredPermissions
    {
        get
        {
            var permissions = new List<(string, bool)>();

            if (OperatingSystem.IsAndroidVersionAtLeast(31))
            {
                permissions.Add((global::Android.Manifest.Permission.BluetoothScan, true));
                permissions.Add((global::Android.Manifest.Permission.BluetoothConnect, true));
            }
            else
            {
                permissions.Add((global::Android.Manifest.Permission.Bluetooth, true));
                permissions.Add((global::Android.Manifest.Permission.BluetoothAdmin, true));
                permissions.Add((global::Android.Manifest.Permission.AccessFineLocation, true));
            }

            return permissions.ToArray();
        }
    }
}
