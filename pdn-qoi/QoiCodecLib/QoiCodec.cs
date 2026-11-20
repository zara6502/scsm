// QOI-кодек для плагина Paint.NET. Кодирование/декодирование по
// спецификации https://qoiformat.org (фомат phoboslab/qoi, v1.0).
// Работает напрямую с Surface (Bgra32) без промежуточных буферов кадра.

using System;
using System.Collections.Generic;
using System.IO;

namespace PdnQoi;

public static class QoiCodec
{
    public const int HeaderSize = 14;
    public const int FooterSize = 8;
    public const int MaxFileSize =
        5 + 4096 * 4096 * 4 + HeaderSize + FooterSize; // запас сверху

    // ---------- кодирование: BGRA-буфер -> QOI-поток (без header) ----------

    // buffer: 4 байта на пиксель (B,G,R,A), top-down. alpha=false:
    // альфа форсируется в 255 и каналов считается 3.
    public static byte[] Encode(byte[] bgra, int w, int h, bool alpha)
    {
        long pixel_count = (long)w * h;
        var output = new List<byte>((int)Math.Min(pixel_count * 5L, int.MaxValue / 2));

        var index = new int[64];
        Array.Clear(index, 0, index.Length);

        int run = 0;
        int prev_r = 0, prev_g = 0, prev_b = 0, prev_a = 255;

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                int o = (int)((long)y * w + x) * 4;
                int b = bgra[o], g = bgra[o + 1], r = bgra[o + 2];
                int a = alpha ? bgra[o + 3] : 255;

                if (r == prev_r && g == prev_g && b == prev_b && a == prev_a)
                {
                    ++run;
                    if (run == 62 || (long)y * w + x == pixel_count - 1)
                    {
                        output.Add((byte)(0xC0 | (run - 1)));
                        run = 0;
                    }
                    continue;
                }

                if (run > 0)
                {
                    output.Add((byte)(0xC0 | (run - 1)));
                    run = 0;
                }

                int hash = QoiHash(r, g, b, a);
                if (index[hash] != 0)
                {
                    // index хранит (a<<24|b<<16|g<<8|r)+1
                    int v = index[hash] - 1;
                    if ((byte)v == r && (byte)(v >> 8) == g &&
                        (byte)(v >> 16) == b && (byte)(v >> 24) == a)
                    {
                        output.Add((byte)hash); // QOI_OP_INDEX
                        prev_r = r; prev_g = g; prev_b = b; prev_a = a;
                        continue;
                    }
                }
                index[hash] = (a << 24 | b << 16 | g << 8 | r) + 1;

                if (a == prev_a)
                {
                    int dr = r - prev_r, dg = g - prev_g, db = b - prev_b;
                    int dg_r = dr - dg, dg_b = db - dg;
                    if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 &&
                        db >= -2 && db <= 1)
                    {
                        output.Add((byte)(0x40 | (dr + 2) << 4 | (dg + 2) << 2 |
                                           (db + 2))); // QOI_OP_DIFF
                    }
                    else if (dg_r >= -8 && dg_r <= 7 && dg >= -32 && dg <= 31 &&
                             dg_b >= -8 && dg_b <= 7)
                    {
                        output.Add((byte)(0x80 | (dg + 32))); // QOI_OP_LUMA
                        output.Add((byte)((dg_r + 8) << 4 | (dg_b + 8)));
                    }
                    else
                    {
                        output.Add(0xFE); // QOI_OP_RGB
                        output.Add((byte)r);
                        output.Add((byte)g);
                        output.Add((byte)b);
                    }
                }
                else
                {
                    output.Add(0xFF); // QOI_OP_RGBA
                    output.Add((byte)r);
                    output.Add((byte)g);
                    output.Add((byte)b);
                    output.Add((byte)a);
                }

                prev_r = r; prev_g = g; prev_b = b; prev_a = a;
            }
        }

        return output.ToArray();
    }

    private static int QoiHash(int r, int g, int b, int a) =>
        ((r * 3 + g * 5 + b * 7 + a * 11) & 0x3F); // как %64 в qoi.h (2^6)

    // ---------- декодирование: QOI-поток (без header) -> Surface ----------

    // Возвращает BGRA-буфер (4 байта на пиксель, top-down).
    public static byte[] Decode(byte[] data, int w, int h, int channels)
    {
        int pos = 0;
        var index = new int[64];
        Array.Clear(index, 0, index.Length);

        int r = 0, g = 0, b = 0, a = 255;
        int run = 0;

        long pixel_count = (long)w * h;
        long pixels_done = 0;
        var outBuf = new byte[pixel_count * 4];

        while (pixels_done < pixel_count)
        {
            if (run > 0)
            {
                --run;
            }
            else
            {
                if (pos + 1 > data.Length)
                    throw new InvalidDataException("QOI: unexpected end");
                int b1 = data[pos++];

                if (b1 == 0xFF) // QOI_OP_RGBA
                {
                    if (pos + 4 > data.Length)
                        throw new InvalidDataException("QOI: unexpected end");
                    r = data[pos]; g = data[pos + 1];
                    b = data[pos + 2]; a = data[pos + 3];
                    pos += 4;
                }
                else if (b1 == 0xFE) // QOI_OP_RGB
                {
                    if (pos + 3 > data.Length)
                        throw new InvalidDataException("QOI: unexpected end");
                    r = data[pos]; g = data[pos + 1]; b = data[pos + 2];
                    pos += 3;
                }
                else if ((b1 & 0xC0) == 0x00) // QOI_OP_INDEX
                {
                    int v = index[b1 & 0x3F];
                    if (v == 0)
                        throw new InvalidDataException("QOI: empty index slot");
                    --v;
                    r = (byte)v; g = (byte)(v >> 8);
                    b = (byte)(v >> 16); a = (byte)(v >> 24);
                }
                else if ((b1 & 0xC0) == 0x40) // QOI_OP_DIFF
                {
                    r += ((b1 >> 4) & 3) - 2;
                    g += ((b1 >> 2) & 3) - 2;
                    b += (b1 & 3) - 2;
                    r &= 0xFF; g &= 0xFF; b &= 0xFF; // как uint8_t в qoi.h
                }
                else if ((b1 & 0xC0) == 0x80) // QOI_OP_LUMA
                {
                    if (pos + 1 > data.Length)
                        throw new InvalidDataException("QOI: unexpected end");
                    int dg = (b1 & 0x3F) - 32;
                    int b2 = data[pos++];
                    int dg_r = dg - 8 + ((b2 >> 4) & 0x0F);
                    int dg_b = dg - 8 + (b2 & 0x0F);
                    r += dg_r; g += dg; b += dg_b;
                    r &= 0xFF; g &= 0xFF; b &= 0xFF;
                }
                else // QOI_OP_RUN
                {
                    run = (b1 & 0x3F);
                }
            }

            int o = (int)pixels_done * 4;
            outBuf[o] = (byte)b;
            outBuf[o + 1] = (byte)g;
            outBuf[o + 2] = (byte)r;
            outBuf[o + 3] = (byte)a;
            ++pixels_done;

            index[QoiHash(r, g, b, a)] =
                (a << 24 | b << 16 | g << 8 | r) + 1;
        }

        return outBuf;
    }
}
