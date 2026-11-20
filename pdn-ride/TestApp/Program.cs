// Тест P/Invoke-обвязки RideCodec.dll: encode -> decode -> сравнение BGRA.
// RideCodec.dll должен быть рядом (копируется при сборке) — см. csproj target.
using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using PaintDotNet;
using PdnRide;

internal static class Program
{
    private static int failures = 0;

    private static void Check(bool ok, string what)
    {
        Console.WriteLine((ok ? "[ OK ] " : "[FAIL] ") + what);
        if (!ok) ++failures;
    }

    private static uint rng = 12345;
    private static byte Rnd()
    {
        rng = rng * 1103515245 + 12345;
        return (byte)(rng >> 16);
    }

    //本地 копия P/Invoke-структур (как в плагине)
    [StructLayout(LayoutKind.Sequential)]
    private struct RideEncodeOptions
    {
        public uint StructSize;
        public int ProfileChoice;
        public int Entropy;
        public int Linear;
        public uint BlockPixels;
        public uint BlockRows;
        public int TryBothParses;
        public uint Threads;

        public static RideEncodeOptions Default => new()
        {
            StructSize = (uint)Marshal.SizeOf<RideEncodeOptions>(),
            TryBothParses = 1,
        };
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RideDecodeOptions
    {
        public uint StructSize;
        public uint Threads;
        public ulong MaxPixels;

        public static RideDecodeOptions Default => new()
        {
            StructSize = (uint)Marshal.SizeOf<RideDecodeOptions>(),
        };
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RideError
    {
        public uint Code;
        public uint Reserved;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 160)]
        public string Message;
    }

    private const string Dll = "RideCodec.dll";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint RideCodecVersion();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint RideCodecEncode(
        byte[] bgra, uint w, uint h, ref RideEncodeOptions opts,
        out IntPtr outPtr, out IntPtr outSize, ref RideError err);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern uint RideCodecDecode(
        byte[] data, UIntPtr size, out uint w, out uint h,
        ref RideDecodeOptions opts, out IntPtr outPtr, ref RideError err);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    private static extern void RideCodecFree(IntPtr ptr);

    private static void RunCase(string name, byte[] bgra, uint w, uint h,
        RideEncodeOptions opts, string rideOutPath)
    {
        var encErr = new RideError();
        uint r = RideCodecEncode(bgra, w, h, ref opts, out IntPtr dataPtr,
            out IntPtr dataSize, ref encErr);
        if (r != 0)
        {
            Console.WriteLine($"[FAIL] {name}: encode err {encErr.Code} " +
                encErr.Message);
            ++failures;
            return;
        }

        long size = dataSize.ToInt64();
        Console.WriteLine($"[ OK ] {name}: encode, {size} bytes " +
            $"({(double)size / (w * h * 4) * 100.0:F1}% of raw)");

        var rideData = new byte[size];
        Marshal.Copy(dataPtr, rideData, 0, rideData.Length);
        RideCodecFree(dataPtr);

        if (rideOutPath != null)
            File.WriteAllBytes(rideOutPath, rideData);

        var decErr = new RideError();
        var decOpts = RideDecodeOptions.Default;
        r = RideCodecDecode(rideData, (UIntPtr)rideData.Length, out uint dw,
            out uint dh, ref decOpts, out IntPtr bgraPtr, ref decErr);
        if (r != 0 || dw != w || dh != h)
        {
            Console.WriteLine($"[FAIL] {name}: decode err {decErr.Code} " +
                decErr.Message);
            ++failures;
            return;
        }

        var decoded = new byte[(long)w * h * 4];
        Marshal.Copy(bgraPtr, decoded, 0, decoded.Length);
        RideCodecFree(bgraPtr);

        bool same = true;
        for (long i = 0; i < (long)w * h * 4 && same; i += 4)
            if (bgra[i] != decoded[i] || bgra[i + 1] != decoded[i + 1] ||
                bgra[i + 2] != decoded[i + 2] ||
                (i % 4 == 3 && decoded[i + 3] != 255))
                same = false;
        Check(same, name + ": roundtrip BGRA");
    }

    [ModuleInitializer]
    internal static void Init()
    {
        // резолвер до JIT-а любых методов, трогающих PDN-типы
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
        {
            string pdn = @"C:\Program Files\paint.net";
            string p = Path.Combine(pdn, name.Name + ".dll");
            return File.Exists(p) ? ctx.LoadFromAssemblyPath(p) : null;
        };
    }

    private static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--dialog")
        {
            DialogTest();
            return;
        }
        Check(RideCodecVersion() != 0, "RideCodecVersion");
        Console.WriteLine($"codec version: {RideCodecVersion() >> 16}." +
            $"{RideCodecVersion() & 0xFFFF}");

        var encOpts0 = RideEncodeOptions.Default;
        var decOpts0 = RideDecodeOptions.Default;
        string outDir = args.Length > 0 ? args[0] : ".";

        // solid
        {
            var b = new byte[64 * 64 * 4];
            for (int i = 0; i < b.Length; i += 4)
            {
                b[i] = 30; b[i + 1] = 144; b[i + 2] = 255; b[i + 3] = 255;
            }
            RunCase("solid 64x64", b, 64, 64, RideEncodeOptions.Default,
                Path.Combine(outDir, "solid.ride"));
        }
        // gradient
        {
            var b = new byte[128 * 128 * 4];
            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                {
                    int o = (y * 128 + x) * 4;
                    b[o] = (byte)(x * 2); b[o + 1] = (byte)(y * 2);
                    b[o + 2] = 128; b[o + 3] = 255;
                }
            RunCase("gradient 128x128", b, 128, 128,
                RideEncodeOptions.Default,
                Path.Combine(outDir, "gradient.ride"));
        }
        // noise RGB
        {
            var b = new byte[200 * 150 * 4];
            for (int i = 0; i < b.Length; i += 4)
            {
                b[i] = Rnd(); b[i + 1] = Rnd(); b[i + 2] = Rnd(); b[i + 3] = 255;
            }
            RunCase("noise 200x150", b, 200, 150, RideEncodeOptions.Default,
                Path.Combine(outDir, "noise.ride"));
        }
        // RGB + entropy
        {
            var b = new byte[100 * 80 * 4];
            for (int i = 0; i < b.Length; i += 4)
            {
                b[i] = Rnd(); b[i + 1] = Rnd(); b[i + 2] = Rnd(); b[i + 3] = 255;
            }
            var opts = RideEncodeOptions.Default;
            opts.ProfileChoice = 1; // RGB
            opts.Entropy = 2;       // ON
            RunCase("noise RGB+entropy", b, 100, 80, opts, null);
        }
        // 4 цвета -> INDEXED + QOI профиль
        {
            var b = new byte[80 * 80 * 4];
            byte[][] pal =
            {
                new byte[] { 255, 0, 0 }, new byte[] { 0, 255, 0 },
                new byte[] { 0, 0, 255 }, new byte[] { 255, 255, 0 },
            };
            for (int y = 0; y < 80; ++y)
                for (int x = 0; x < 80; ++x)
                {
                    int o = (y * 80 + x) * 4;
                    byte[] c = pal[(x / 20 + y / 20) % 4];
                    b[o] = c[2]; b[o + 1] = c[1]; b[o + 2] = c[0];
                    b[o + 3] = 255;
                }
            RunCase("4-color INDEXED", b, 80, 80, RideEncodeOptions.Default,
                Path.Combine(outDir, "indexed.ride"));
            var qoiOpts = RideEncodeOptions.Default;
            qoiOpts.ProfileChoice = 5; // QOI
            RunCase("4-color QOI profile", b, 80, 80, qoiOpts, null);
        }
        // large block option
        {
            var b = new byte[300 * 200 * 4];
            for (int i = 0; i < b.Length; i += 4)
            {
                b[i] = Rnd(); b[i + 1] = Rnd(); b[i + 2] = Rnd(); b[i + 3] = 255;
            }
            var opts = RideEncodeOptions.Default;
            opts.BlockPixels = 1048576;
            RunCase("noise block=1M", b, 300, 200, opts, null);
        }
        // ошибки
        {
            var err = new RideError();
            uint r = RideCodecEncode(null, 10, 10, ref encOpts0,
                out _, out _, ref err);
            Check(r != 0 && err.Code != 0, "encode null -> error");
            RideCodecFree(IntPtr.Zero);
        }

        // Проверка совместимости: декодировать reference .ride (созданные
        // официальным ride_convert) и сравнить с .ppm (P6, RGB).
        if (Directory.GetFiles(outDir, "*.ppm").Length > 0)
        {
            foreach (string ridePath in Directory.GetFiles(outDir, "*.ride"))
            {
                string baseName = Path.GetFileNameWithoutExtension(ridePath);
                string ppmPath = Path.Combine(outDir, baseName + ".ppm");
                if (!File.Exists(ppmPath)) continue;

                byte[] file = File.ReadAllBytes(ridePath);
                var decOpts = RideDecodeOptions.Default;
                var err = new RideError();
                uint r = RideCodecDecode(file, (UIntPtr)file.Length,
                    out uint w, out uint h, ref decOpts, out IntPtr bgraPtr,
                    ref err);
                if (r != 0)
                {
                    Console.WriteLine($"[FAIL] {baseName}: decode err " +
                        err.Code + " " + err.Message);
                    ++failures;
                    continue;
                }
                var bgra = new byte[(long)w * h * 4];
                Marshal.Copy(bgraPtr, bgra, 0, bgra.Length);
                RideCodecFree(bgraPtr);

                byte[] ppm = File.ReadAllBytes(ppmPath);
                // P6 header: P6, width, height, maxval
                int pos = 2;
                int[] fields = new int[3];
                for (int f = 0; f < 3; ++f)
                {
                    while (pos < ppm.Length &&
                           char.IsWhiteSpace((char)ppm[pos])) ++pos;
                    int v = 0;
                    while (pos < ppm.Length && ppm[pos] >= '0' &&
                           ppm[pos] <= '9')
                    {
                        v = v * 10 + (ppm[pos] - '0');
                        ++pos;
                    }
                    fields[f] = v;
                }
                ++pos; // whitespace after maxval
                int ppmW = fields[0], ppmH = fields[1];
                bool ok = ppmW == w && ppmH == h;
                if (ok)
                {
                    for (int y = 0; y < h && ok; ++y)
                        for (int x = 0; x < w && ok; ++x)
                        {
                            long o = (long)(y * w + x) * 4;
                            long po = pos + 3L * w * y + 3L * x;
                            // BGRA vs RGB (PPM)
                            if (bgra[o + 2] != ppm[po] ||
                                bgra[o + 1] != ppm[po + 1] ||
                                bgra[o] != ppm[po + 2])
                            {
                                ok = false;
                                Console.WriteLine($"  MISMATCH x={x} y={y} " +
                                    $"bgra=({bgra[o + 2]},{bgra[o + 1]},{bgra[o]}) " +
                                    $"ppm=({ppm[po]},{ppm[po + 1]},{ppm[po + 2]})");
                            }
                        }
                }
                Check(ok, baseName + ": reference .ride == .ppm");
            }
        }

        Console.WriteLine(failures == 0 ? "ВСЁ ПРОШЛО" : $"ОШИБОК: {failures}");
        Environment.Exit(failures);
    }

    // Эмуляция диалога PDN: дефолтный токен -> значения UI -> Save.
    private static void DialogTest()
    {
        var ft = new PdnRide.RideFileType();
        var token = (PropertyBasedSaveConfigToken)
            ft.CreateDefaultSaveConfigToken();

        Func<int, int, Document> makeDoc = (w, h) =>
        {
            var doc = new Document(w, h);
            var layer = Layer.CreateBackgroundLayer(w, h);
            var surf = layer.Surface;
            uint rng = 12345;
            unsafe
            {
                for (int y = 0; y < h; ++y)
                {
                    ColorBgra* row = surf.GetRowPointerUnchecked(y);
                    for (int x = 0; x < w; ++x)
                    {
                        rng = rng * 1103515245 + 12345;
                        byte r = (byte)(rng >> 16);
                        rng = rng * 1103515245 + 12345;
                        byte g = (byte)(rng >> 16);
                        rng = rng * 1103515245 + 12345;
                        byte b = (byte)(rng >> 16);
                        row[x] = ColorBgra.FromBgra(b, g, r, 255);
                    }
                }
            }
            doc.Layers.Add(layer);
            return doc;
        };

        long SaveWith(Document doc, PropertyBasedSaveConfigToken tok)
        {
            using var ms = new MemoryStream();
            ft.Save(doc, ms, tok);
            return ms.Length;
        }

        using (var doc = makeDoc(96, 96))
            Console.WriteLine("default (Auto/256): " +
                SaveWith(doc, token) + " bytes");

        token.SetPropertyValue("RIDE.Profile", RideProfile.Indexed);
        token.SetPropertyValue("RIDE.QuantizeColors", 1);
        token.SetPropertyValue("RIDE.Dither", true);
        Console.WriteLine("state: Profile=" +
            token.GetProperty("RIDE.Profile").Value + " QC=" +
            token.GetProperty("RIDE.QuantizeColors").Value + " Dither=" +
            token.GetProperty("RIDE.Dither").Value);
        using (var doc = makeDoc(96, 96))
            Console.WriteLine("modified (Indexed/1/FS): " +
                SaveWith(doc, token) + " bytes");

        token.SetPropertyValue("RIDE.Profile", RideProfile.Rgb);
        Console.WriteLine("state: Profile=" +
            token.GetProperty("RIDE.Profile").Value + " QC=" +
            token.GetProperty("RIDE.QuantizeColors").Value);
        using (var doc = makeDoc(96, 96))
        {
            long sz = SaveWith(doc, token);
            Console.WriteLine("modified (RGB): " + sz + " bytes");
        }
    }
}
