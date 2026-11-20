// FileType-плагин RIDE для Paint.NET 5.2: Load/Save через RideCodec.dll
// (нативный кодек), окно настроек сохранения через IndirectUI.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using PaintDotNet;
using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;
using PaintDotNet.Rendering;

namespace PdnRide;

public enum RideProfile
{
    Auto,     // INDEXED для <=256 цветов, иначе меньший из RGB/INDEXED
    Rgb,      // 24 бита
    Indexed,  // палитра <=256 цветов
    Qoi,      // канонические QOI-операции в контейнере RIDE
}

public enum RideEntropy
{
    Off,  // rANS-слой выключен
    Auto, // включать при экономии >= 2%
    On,   // всегда
}

public enum RideBlockSize
{
    Default, // 262144 пикселей на блок (reference-значение)
    Small,   // 65536
    Large,   // 1048576
    Single,  // один блок на всё изображение
}

[PluginSupportInfo(typeof(RidePluginSupportInfo))]
public sealed class RideFileType : PropertyBasedFileType
{
    private const string ProfileProperty = "RIDE.Profile";
    private const string EntropyProperty = "RIDE.Entropy";
    private const string BlockSizeProperty = "RIDE.BlockSize";
    private const string LinearProperty = "RIDE.Linear";

    public RideFileType()
        : base(
            "RIDE",
            new FileTypeOptions
            {
                LoadExtensions = new[] { ".ride" },
                SaveExtensions = new[] { ".ride" },
                SupportsCancellation = true,
                SupportsLayers = false,
            })
    {
    }

    // ---------- окно настроек сохранения ----------

    public override PropertyCollection OnCreateSavePropertyCollection()
    {
        var props = new List<Property>
        {
            StaticListChoiceProperty.CreateForEnum(ProfileProperty,
                RideProfile.Auto),
            StaticListChoiceProperty.CreateForEnum(EntropyProperty,
                RideEntropy.Off),
            StaticListChoiceProperty.CreateForEnum(BlockSizeProperty,
                RideBlockSize.Default),
            new BooleanProperty(LinearProperty, false),
        };
        return new PropertyCollection(props);
    }

    public override ControlInfo OnCreateSaveConfigUI(
        PropertyCollection props)
    {
        ControlInfo info = CreateDefaultSaveConfigUI(props);

        PropertyControlInfo profile =
            info.FindControlForPropertyName(ProfileProperty);
        profile.ControlProperties[ControlInfoPropertyNames.DisplayName]
            .Value = "Profile";
        profile.SetValueDisplayName(RideProfile.Auto, "Auto (best)");
        profile.SetValueDisplayName(RideProfile.Rgb, "RGB (24-bit)");
        profile.SetValueDisplayName(RideProfile.Indexed, "Indexed (palette)");
        profile.SetValueDisplayName(RideProfile.Qoi, "QOI ops in RIDE");
        profile.ControlProperties[
                ControlInfoPropertyNames.Description].Value =
            "Auto tries INDEXED for small palettes and keeps the smaller file";

        PropertyControlInfo entropy =
            info.FindControlForPropertyName(EntropyProperty);
        entropy.ControlProperties[ControlInfoPropertyNames.DisplayName]
            .Value = "rANS entropy layer";
        entropy.SetValueDisplayName(RideEntropy.Off, "Off");
        entropy.SetValueDisplayName(RideEntropy.Auto, "Auto (>=2% gain)");
        entropy.SetValueDisplayName(RideEntropy.On, "On");

        PropertyControlInfo blockSize =
            info.FindControlForPropertyName(BlockSizeProperty);
        blockSize.ControlProperties[ControlInfoPropertyNames.DisplayName]
            .Value = "Block size";
        blockSize.SetValueDisplayName(RideBlockSize.Default,
            "Default (262144 px)");
        blockSize.SetValueDisplayName(RideBlockSize.Small, "Small (65536 px)");
        blockSize.SetValueDisplayName(RideBlockSize.Large,
            "Large (1048576 px)");
        blockSize.SetValueDisplayName(RideBlockSize.Single,
            "Single block");

        PropertyControlInfo linear =
            info.FindControlForPropertyName(LinearProperty);
        linear.ControlProperties[ControlInfoPropertyNames.DisplayName]
            .Value = "Linear colors";
        linear.ControlProperties[ControlInfoPropertyNames.Description]
            .Value = "Store the linear-color flag (no conversion)";

        return info;
    }

    // ---------- сохранение ----------

    protected override void OnSaveT(
        Document input,
        Stream output,
        PropertyBasedSaveConfigToken token,
        Surface scratchSurface,
        ProgressEventHandler progressCallback)
    {
        scratchSurface.Clear();
        input.CreateRenderer().Render(scratchSurface);

        int w = scratchSurface.Width;
        int h = scratchSurface.Height;

        var profile = (RideProfile)token
            .GetProperty(ProfileProperty).Value;
        var entropy = (RideEntropy)token
            .GetProperty(EntropyProperty).Value;
        var blockSize = (RideBlockSize)token
            .GetProperty(BlockSizeProperty).Value;
        bool linear = (bool)token.GetProperty(LinearProperty).Value;

        var opts = RideEncodeOptions.Default;
        opts.ProfileChoice = profile switch
        {
            RideProfile.Rgb => (int)RideProfileChoice.Rgb,
            RideProfile.Indexed => (int)RideProfileChoice.Indexed,
            RideProfile.Qoi => (int)RideProfileChoice.Qoi,
            _ => (int)RideProfileChoice.Auto,
        };
        opts.Entropy = (int)entropy;
        opts.Linear = linear ? 1 : 0;
        opts.BlockPixels = blockSize switch
        {
            RideBlockSize.Small => 65536,
            RideBlockSize.Large => 1048576,
            RideBlockSize.Single => 0, // rideent: 0 = один блок
            _ => 262144,
        };

        // Surface -> BGRA (формат RIDE v1 без альфы: альфа отбрасывается)
        var bgra = new byte[(long)w * h * 4];
        unsafe
        {
            for (int y = 0; y < h; ++y)
            {
                ColorBgra* src = scratchSurface.GetRowPointerUnchecked(y);
                int rowOff = y * w * 4;
                for (int x = 0; x < w; ++x)
                {
                    bgra[rowOff + x * 4] = src->B;
                    bgra[rowOff + x * 4 + 1] = src->G;
                    bgra[rowOff + x * 4 + 2] = src->R;
                    bgra[rowOff + x * 4 + 3] = src->A;
                    ++src;
                }
            }
        }

        var encErr = new RideError();
        var encOpts = opts;
        uint r = RideNative.RideCodecEncode(bgra, (uint)w, (uint)h,
            ref encOpts, out IntPtr dataPtr, out IntPtr dataSize,
            ref encErr);
        if (r != 0)
            throw new IOException(
                $"RIDE encode failed: {encErr.Message ?? ""}");

        try
        {
            byte[] data = new byte[dataSize.ToInt64()];
            Marshal.Copy(dataPtr, data, 0, data.Length);
            output.Write(data, 0, data.Length);
        }
        finally
        {
            RideNative.RideCodecFree(dataPtr);
        }
    }

    // ---------- загрузка ----------

    protected override Document OnLoad(Stream input)
    {
        using var ms = new MemoryStream();
        input.CopyTo(ms);
        byte[] file = ms.ToArray();
        if (file.Length < 14)
            throw new InvalidDataException("RIDE: file too small");

        var decOpts = RideDecodeOptions.Default;
        var err = new RideError();
        uint r = RideNative.RideCodecDecode(file, (UIntPtr)file.Length,
            out uint w, out uint h, ref decOpts, out IntPtr bgraPtr,
            ref err);
        if (r != 0)
            throw new InvalidDataException(
                $"RIDE decode failed: {err.Message ?? ""}");

        var bgra = new byte[(long)w * h * 4];
        try
        {
            Marshal.Copy(bgraPtr, bgra, 0, bgra.Length);
        }
        finally
        {
            RideNative.RideCodecFree(bgraPtr);
        }

        var document = new Document((int)w, (int)h);
        var layer = Layer.CreateBackgroundLayer((int)w, (int)h);
        Surface surface = layer.Surface;
        unsafe
        {
            for (int y = 0; y < (int)h; ++y)
            {
                ColorBgra* dst = surface.GetRowPointerUnchecked(y);
                int rowOff = y * (int)w * 4;
                for (int x = 0; x < (int)w; ++x)
                {
                    dst->B = bgra[rowOff + x * 4];
                    dst->G = bgra[rowOff + x * 4 + 1];
                    dst->R = bgra[rowOff + x * 4 + 2];
                    dst->A = bgra[rowOff + x * 4 + 3];
                    ++dst;
                }
            }
        }

        document.Layers.Add(layer);
        return document;
    }
}
