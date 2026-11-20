// Фабрика FileType-плагина: Paint.NET 5.x ищет именно IFileTypeFactory.
using PaintDotNet.FileTypes;

namespace PdnQoi;

internal sealed class QoiFileTypeFactory
    : IFileTypeFactory
{
    public IFileType[] CreateFileTypes(IFileTypeHost host)
    {
        return [new QoiFileType(host)];
    }
}
