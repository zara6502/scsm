// Тест-харнесс QOI-кодека (чистый .NET, без зависимостей Paint.NET):
//  1) декодирование reference .qoi (официальный кодек phoboslab)
//  2) наша кодировка -> файл -> декодирование -> сравнение пикселей
using System;
using System.IO;
using PdnQoi;

internal static class Program
{
    private static int failures = 0;

    private static void Check(bool ok, string what)
    {
        Console.WriteLine((ok ? "[ OK ] " : "[FAIL] ") + what);
        if (!ok) ++failures;
    }

    private static void Main(string[] args)
    {
        string imagesDir = args.Length > 0 ? args[0] : "../../tests/images";

        foreach (string qoiPath in Directory.GetFiles(imagesDir, "*.qoi"))
        {
            string name = Path.GetFileNameWithoutExtension(qoiPath);
            Console.WriteLine("--- " + name + " ---");
            byte[] file = File.ReadAllBytes(qoiPath);

            // заголовок
            int w = (file[4] << 24) | (file[5] << 16) | (file[6] << 8) | file[7];
            int h = (file[8] << 24) | (file[9] << 16) | (file[10] << 8) | file[11];
            int channels = file[12];
            Check(w > 0 && h > 0, $"заголовок {w}x{h} ch={channels}");

            // payload между header и footer
            var payload = new byte[file.Length - 14 - 8];
            Array.Copy(file, 14, payload, 0, payload.Length);

            // 1) декодирование reference -> BGRA
            byte[] s1 = QoiCodec.Decode(payload, w, h, channels);

            // 2) наша кодировка -> файл -> декодирование -> сравнение
            byte[] mine = QoiCodec.Encode(s1, w, h, channels == 4);
            var outBuf = new byte[14 + mine.Length + 8];
            Array.Copy(file, 0, outBuf, 0, 14);
            Array.Copy(mine, 0, outBuf, 14, mine.Length);
            Array.Copy(file, file.Length - 8, outBuf, 14 + mine.Length, 8);
            string outPath = name + "_ours.qoi";
            File.WriteAllBytes(outPath, outBuf);

            byte[] s2 = QoiCodec.Decode(mine, w, h, channels);

            bool same = true;
            for (long i = 0; i < (long)w * h * 4 && same; i += 4)
                if (s1[i + 2] != s2[i + 2] || s1[i + 1] != s2[i + 1] ||
                    s1[i] != s2[i] || s1[i + 3] != s2[i + 3]) same = false;
            Check(same, "roundtrip: пиксели совпадают");

            // 3) наш файл декодируется заново из файла
            byte[] refile = File.ReadAllBytes(outPath);
            var rePayload = new byte[refile.Length - 14 - 8];
            Array.Copy(refile, 14, rePayload, 0, rePayload.Length);
            byte[] s3 = QoiCodec.Decode(rePayload, w, h, channels);
            bool same3 = true;
            for (long i = 0; i < (long)w * h * 4 && same3; i += 4)
                if (s1[i + 2] != s3[i + 2] || s1[i + 1] != s3[i + 1] ||
                    s1[i] != s3[i] || s1[i + 3] != s3[i + 3]) same3 = false;
            Check(same3, "файл -> Decode: пиксели совпадают");
        }

        Console.WriteLine(failures == 0 ? "ВСЁ ПРОШЛО" : $"ОШИБОК: {failures}");
        Environment.Exit(failures);
    }
}
