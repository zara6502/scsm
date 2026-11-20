// Фабрика: Paint.NET 5.x обнаруживает плагины типов файлов только через
// IFileTypeFactory. Без неё сборка молча игнорируется.
using PaintDotNet;

namespace PdnRide;

internal sealed class RideFileTypeFactory
    : IFileTypeFactory
{
    public FileType[] GetFileTypeInstances(IFileTypeHost host)
    {
        return [new RideFileType()];
    }
}
