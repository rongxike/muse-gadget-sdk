namespace MuseCompanion.Ble;

/// <summary>
/// GATT identifiers for the Muse "handshake" service. These must match the
/// firmware in esp32/components/muse/muse_ble.c (the HS_* UUIDs).
///
/// The firmware builds each 128-bit UUID as 4d757365-00XX-4000-8000-6a6f6c6c7900
/// where XX is the characteristic/service index.
/// </summary>
public static class MuseBleConstants
{
    /// <summary>Name prefixes the Muse device advertises with.</summary>
    public static readonly string[] NamePrefixes = { "MuseGadget", "HomeLink", "Muse-" };

    /// <summary>Handshake service (index 0x10).</summary>
    public static readonly Guid HandshakeService = new("4d757365-0010-4000-8000-6a6f6c6c7900");

    /// <summary>Device -> app greeting (read + notify, index 0x11).</summary>
    public static readonly Guid HelloCharacteristic = new("4d757365-0011-4000-8000-6a6f6c6c7900");

    /// <summary>App -> device reply (write, index 0x12).</summary>
    public static readonly Guid ReplyCharacteristic = new("4d757365-0012-4000-8000-6a6f6c6c7900");

    /// <summary>Greeting the device sends once the app subscribes.</summary>
    public const string Hello = "hello";

    /// <summary>Reply the app writes back to complete the handshake.</summary>
    public const string Reply = "hi";
}
