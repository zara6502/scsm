// Как SpanExtensions.cs из официального сэмпла paintdotnet/PdnFileTypeSamples.
using System;
using System.Runtime.InteropServices;

namespace PdnQoi;

internal static class SpanExtensions
{
    public static Span<byte> AsBytes<T>(this Span<T> span)
        where T : unmanaged =>
        System.Runtime.InteropServices.MemoryMarshal.AsBytes(span);
}
