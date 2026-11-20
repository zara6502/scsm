// FileType-плагин QOI ("Quite OK Image", https://qoiformat.org) для
// Paint.NET 5.2+. Кодек — QoiCodec (BGRA-буферы), подключается исходником.
// API по официальному образцу paintdotnet/PdnFileTypeSamples.

using System;
using System.IO;
using PaintDotNet;
using PaintDotNet.ComponentModel;
using PaintDotNet.FileTypes;
using PaintDotNet.Imaging;
using PaintDotNet.Rendering;

namespace PdnQoi;

public sealed class QoiFileType
    : FileType
{
    private static ReadOnlySpan<byte> QoiMagic => "qoif"u8;
    private static ReadOnlySpan<byte> QoiFooter => new byte[] { 0, 0, 0, 0, 0, 0, 0, 1 };

    public QoiFileType(IFileTypeHost host)
        : base(
            host,
            "QOI",
            FileTypeOptions.Create() with
            {
                LoadExtensions = [".qoi"],
                SaveExtensions = [".qoi"],
            })
    {
    }

    protected override IFileTypeLoader OnCreateLoader()
    {
        return new Loader(this);
    }

    protected override IFileTypeSaver OnCreateSaver()
    {
        return new Saver(this);
    }

    private sealed class Loader
        : FileTypeLoader
    {
        public Loader(QoiFileType fileType)
            : base(fileType)
        {
        }

        protected override IFileTypeDocument OnLoad(IFileTypeLoadContext context)
        {
            // читаем весь файл целиком (QOI читается только последовательно)
            var ms = new MemoryStream();
            context.Input.CopyTo(ms);
            byte[] file = ms.ToArray();

            if (file.Length < 14 + 8)
                throw new FormatException("QOI: file too small");
            if (!file.AsSpan(0, 4).SequenceEqual(QoiMagic))
                throw new FormatException("QOI: bad magic");

            int w = (file[4] << 24) | (file[5] << 16) | (file[6] << 8) | file[7];
            int h = (file[8] << 24) | (file[9] << 16) | (file[10] << 8) | file[11];
            int channels = file[12];
            if (w <= 0 || h <= 0 || (channels != 3 && channels != 4))
                throw new FormatException("QOI: bad header fields");
            if ((long)w * h > 128L * 1024 * 1024)
                throw new FormatException("QOI: image too large");

            var payload = new byte[file.Length - 14 - 8];
            Array.Copy(file, 14, payload, 0, payload.Length);

            byte[] bgra = QoiCodec.Decode(payload, w, h, channels);

            IFileTypeDocument<ColorBgra32> document =
                context.Factory.CreateDocumentBgra32(w, h);

            IFileTypeBitmapLayer<ColorBgra32> layer = document.CreateBitmapLayer();
            using (IFileTypeBitmapSink<ColorBgra32> sink = layer.GetBitmap())
            using (IFileTypeBitmapLock<ColorBgra32> bitmapLock = sink.Lock())
            {
                RegionPtr<ColorBgra32> region = bitmapLock.AsRegionPtr();
                for (int y = 0; y < h; ++y)
                {
                    bgra.AsSpan((int)((long)y * w) * 4, w * 4)
                        .CopyTo(region.Rows[y].Span.AsBytes());
                }
            }

            document.Layers.Add(layer);
            return document;
        }
    }

    private sealed class Saver
        : FileTypeSaver
    {
        public Saver(QoiFileType fileType)
            : base(fileType)
        {
        }

        protected override void OnSave(IFileTypeSaveContext context)
        {
            IReadOnlyFileTypeDocument document = context.Document;
            if (document.PixelFormat != PixelFormats.Bgra32)
                throw new FormatException("QOI: expected Bgra32 document");

            int w = document.Size.Width;
            int h = document.Size.Height;

            // QOI-заголовок: magic, w/h big-endian, каналы=4, colorspace=0 (sRGB)
            var header = new byte[14];
            QoiMagic.CopyTo(header);
            header[4] = (byte)(w >> 24);
            header[5] = (byte)(w >> 16);
            header[6] = (byte)(w >> 8);
            header[7] = (byte)w;
            header[8] = (byte)(h >> 24);
            header[9] = (byte)(h >> 16);
            header[10] = (byte)(h >> 8);
            header[11] = (byte)h;
            header[12] = 4;
            header[13] = 0;
            context.Output.Write(header);

            // документ -> BGRA-буфер
            var docRef = document.CreateRef<IReadOnlyFileTypeDocument<ColorBgra32>>();
            IReadOnlyFileTypeLayer<ColorBgra32> layer = docRef.Layers[0];
            var bgra = new byte[(long)w * h * 4];
            using (IFileTypeBitmapSource<ColorBgra32> bitmap = layer.GetBitmap())
            using (IFileTypeBitmapLock<ColorBgra32> bitmapLock = bitmap.Lock())
            {
                RegionPtr<ColorBgra32> region = bitmapLock.AsRegionPtr();
                for (int y = 0; y < h; ++y)
                {
                    region.Rows[y].Span.AsBytes()
                        .CopyTo(bgra.AsSpan((int)((long)y * w) * 4, w * 4));
                }
            }

            byte[] payload = QoiCodec.Encode(bgra, w, h, alpha: true);
            context.Output.Write(payload);
            context.Output.Write(QoiFooter);
        }
    }
}
