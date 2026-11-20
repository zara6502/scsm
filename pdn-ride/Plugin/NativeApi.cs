// P/Invoke-обвязка RideCodec.dll. Контракт — ridecodec/include/ride_codec/
// ride_codec.h (см. также ../ridecodec/CONTRACT.md).
using System;
using System.IO;
using System.Runtime.InteropServices;

namespace PdnRide;

internal static class RideCodecVersion
{
    public const uint Major = 1;
    public const uint Minor = 0;
}

internal enum RideProfileChoice
{
    Auto = 0,
    Rgb = 1,
    Indexed = 2,
    Qoi = 5,
}

internal enum RideEntropyMode
{
    Off = 0,
    Auto = 1,
    On = 2,
}

[StructLayout(LayoutKind.Sequential)]
internal struct RideEncodeOptions
{
    public uint StructSize;
    public int ProfileChoice;
    public int Entropy;
    public int Linear;
    public uint BlockPixels;
    public uint BlockRows;
    public int TryBothParses;
    public uint Threads;
    // v2: квантование для профиля INDEXED (v1-хосты передают короткий
    // struct_size — контракт обратно совместим)
    public uint QuantizeColors; // 0 = не квантовать; 1..256
    public int Dither;          // 0 = нет; 1 = Floyd–Steinberg

    public static RideEncodeOptions Default => new()
    {
        StructSize = (uint)Marshal.SizeOf<RideEncodeOptions>(),
        ProfileChoice = (int)RideProfileChoice.Auto,
        Entropy = (int)RideEntropyMode.Off,
        Linear = 0,
        BlockPixels = 0,        // 262144 в reference-реализации
        BlockRows = 0,
        TryBothParses = 1,
        Threads = 0,
    };
}

[StructLayout(LayoutKind.Sequential)]
internal struct RideDecodeOptions
{
    public uint StructSize;
    public uint Threads;
    public ulong MaxPixels;

    public static RideDecodeOptions Default => new()
    {
        StructSize = (uint)Marshal.SizeOf<RideDecodeOptions>(),
        Threads = 0,
        MaxPixels = 0, // 1 << 30 в reference-реализации
    };
}

[StructLayout(LayoutKind.Sequential)]
internal struct RideError
{
    public uint Code;
    public uint Reserved;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 160)]
    public string Message;

    public const uint Ok = 0;
    public const uint InvalidArg = 1;
    public const uint Malformed = 2;
    public const uint TooLarge = 3;
    public const uint OutOfMemory = 4;
    public const uint Internal = 5;
}

internal static class RideNative
{
    private const string Dll = "RideCodec.dll";

    // DllImport ищет нативные DLL рядом с PaintDotNet.exe, но не в папке
    // плагинов. Явно грузим RideCodec.dll из каталога этой сборки (или из
    // корня установки — работает и так, и так).
    static RideNative()
    {
        try
        {
            string pluginDir = Path.GetDirectoryName(
                typeof(RideNative).Assembly.Location) ?? ".";
            if (NativeLibrary.TryLoad(
                    Path.Combine(pluginDir, "RideCodec.dll"), out _))
                return;
            NativeLibrary.TryLoad(
                Path.Combine(AppContext.BaseDirectory, "RideCodec.dll"), out _);
        }
        catch
        {
            // упадём в стандартный DllImport-поиск
        }
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint RideCodecVersion();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint RideCodecEncode(
        byte[] bgra, uint w, uint h,
        ref RideEncodeOptions opts,
        out IntPtr outPtr, out IntPtr outSize,
        ref RideError err);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern uint RideCodecDecode(
        byte[] data, UIntPtr size,
        out uint w, out uint h,
        ref RideDecodeOptions opts,
        out IntPtr outPtr,
        ref RideError err);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void RideCodecFree(IntPtr ptr);
}
