// Paint.NET FileType plugin: QOI ("Quite OK Image", https://qoiformat.org)
// Открытие и сохранение .qoi (RGB/RGBA, sRGB — по спецификации v1.0).
// Paint.NET 5.20 (net11.0), API сверено рефлексией по установленным сборкам.

using System;
using System.IO;
using PaintDotNet;
using PaintDotNet.Data;

namespace PdnQoi;

public sealed class QoiFileType : FileType
{
    private sealed class QoiSaveConfigToken : SaveConfigToken
    {
        public bool WriteAlpha; // RGBA (4 канала) или RGB (3)

        public QoiSaveConfigToken(bool writeAlpha)
        {
            WriteAlpha = writeAlpha;
        }

        public QoiSaveConfigToken(QoiSaveConfigToken copyMe)
        {
            WriteAlpha = copyMe.WriteAlpha;
        }

        public override object Clone() => new QoiSaveConfigToken(this);
    }

    public QoiFileType()
        : base(
            "QOI",
            new FileTypeOptions
            {
                LoadExtensions = new[] { ".qoi" },
                SaveExtensions = new[] { ".qoi" },
            })
    {
    }

    protected override SaveConfigToken OnCreateDefaultSaveConfigToken()
    {
        return new QoiSaveConfigToken(true);
    }

    protected override void OnSave(
        Document input,
        Stream output,
        SaveConfigToken token,
        Surface scratchSurface,
        ProgressEventHandler progressCallback)
    {
        bool alpha = true;
        if (token is QoiSaveConfigToken t) alpha = t.WriteAlpha;

        scratchSurface.Clear();
        input.Flatten(scratchSurface);

        int w = scratchSurface.Width;
        int h = scratchSurface.Height;

        var header = new byte[14];
        header[0] = (byte)'q';
        header[1] = (byte)'o';
        header[2] = (byte)'i';
        header[3] = (byte)'f';
        header[4] = (byte)(w >> 24);
        header[5] = (byte)(w >> 16);
        header[6] = (byte)(w >> 8);
        header[7] = (byte)w;
        header[8] = (byte)(h >> 24);
        header[9] = (byte)(h >> 16);
        header[10] = (byte)(h >> 8);
        header[11] = (byte)h;
        header[12] = (byte)(alpha ? 4 : 3);
        header[13] = 0; // sRGB с linear alpha (каноничный вариант)
        output.Write(header, 0, 14);

        // Surface -> BGRA-буфер
        var bgra = new byte[(long)w * h * 4];
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                int o = (y * w + x) * 4;
                ColorBgra px = scratchSurface[x, y];
                bgra[o] = px.B;
                bgra[o + 1] = px.G;
                bgra[o + 2] = px.R;
                bgra[o + 3] = px.A;
            }
        byte[] payload = QoiCodec.Encode(bgra, w, h, alpha);
        output.Write(payload, 0, payload.Length);

        // terminator: 7x00 + 0x01
        var end = new byte[] { 0, 0, 0, 0, 0, 0, 0, 1 };
        output.Write(end, 0, 8);
    }

    protected override Document OnLoad(Stream input)
    {
        var header = new byte[14];
        int got = 0;
        while (got < 14)
        {
            int n = input.Read(header, got, 14 - got);
            if (n <= 0) throw new InvalidDataException("QOI: truncated header");
            got += n;
        }

        if (header[0] != 'q' || header[1] != 'o' || header[2] != 'i' ||
            header[3] != 'f')
            throw new InvalidDataException("QOI: bad magic");

        int w = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) |
                header[7];
        int h = (header[8] << 24) | (header[9] << 16) | (header[10] << 8) |
                header[11];
        int channels = header[12];
        if (w <= 0 || h <= 0 || (channels != 3 && channels != 4))
            throw new InvalidDataException("QOI: bad header fields");
        if ((long)w * h > 128L * 1024 * 1024)
            throw new InvalidDataException("QOI: image too large");

        var rest = new MemoryStream();
        input.CopyTo(rest);
        byte[] data = rest.ToArray();

        byte[] bgra = QoiCodec.Decode(data, w, h, channels);

        var surface = new Surface(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                int o = (y * w + x) * 4;
                surface[x, y] = ColorBgra.FromBgra(bgra[o], bgra[o + 1],
                                                   bgra[o + 2], bgra[o + 3]);
            }

        var doc = new Document(w, h);
        doc.Layers.Add(new BitmapLayer(surface, true));
        surface.Dispose();
        return doc;
    }
}
