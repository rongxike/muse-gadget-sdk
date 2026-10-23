using Microsoft.Extensions.Logging;
using MuseCompanion.Ble;

namespace MuseCompanion;

public static class MauiProgram
{
    public static MauiApp CreateMauiApp()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>();

#if DEBUG
        builder.Logging.AddDebug();
#endif

        builder.Services.AddSingleton<MuseBleService>();
        builder.Services.AddSingleton<MainPage>();
        builder.Services.AddSingleton<App>();

        return builder.Build();
    }
}
