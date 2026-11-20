// Метаданные плагина (показываются в About плагинов Paint.NET).
using System;
using PaintDotNet;

namespace PdnRide;

public sealed class RidePluginSupportInfo : IPluginSupportInfo
{
    public string DisplayName => "RIDE FileType";

    public string Author => "zara6502";

    public string Copyright => "zara6502";

    public Version Version =>
        typeof(RidePluginSupportInfo).Assembly.GetName().Version;

    public Uri WebsiteUri =>
        new("https://github.com/zara6502/scsm/tree/pdn-ride/pdn-ride");
}
