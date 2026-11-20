// Сравнение атрибутов и ссылок рабочих/наших сборок + блок-листы PDN.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

internal static class Program
{
    private static void DumpInterfaces(string dllPath, string typeName)
    {
        var ctx = AssemblyLoadContext.Default;
        var asm = ctx.LoadFromAssemblyPath(dllPath);
        var t = asm.GetType(typeName);
        Console.WriteLine("== " + typeName + " @ " + Path.GetFileName(dllPath));
        if (t == null) { Console.WriteLine("  NULL"); return; }
        Console.WriteLine("  base: " + t.BaseType.FullName);
        foreach (var i in t.GetInterfaces())
            Console.WriteLine("  IMPL: " + i.FullName);
    }

    private static void Main()
    {
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
        {
            string[] alt =
            {
                "C:/Users/jimdg/.zcode/workspace/draft-38d1cc56-ae8c-489e-bbab-35f5de83ad7d/scsm/pdn-qoi/Plugin/bin/Release",
                "C:/Program Files/Paint.NET",
            };
            foreach (var a in alt)
            {
                string p = Path.Combine(a, name.Name + ".dll");
                if (File.Exists(p))
                    return ctx.LoadFromAssemblyPath(p);
            }
            return null;
        };

        DumpInterfaces("C:/Program Files/Paint.NET/PaintDotNet.Data.dll",
            "PaintDotNet.FileType");
        DumpInterfaces("C:/Program Files/Paint.NET/Bundled/JpegXLFileType/JpegXLFileType.dll",
            null);
        {
            var jxl = AssemblyLoadContext.Default.LoadFromAssemblyPath(
                "C:/Program Files/Paint.NET/Bundled/JpegXLFileType/JpegXLFileType.dll");
            foreach (var t2 in jxl.GetTypes())
                if (t2.Name.Contains("FileType") ||
                    t2.Name.Contains("FileType"))
                {
                    Console.WriteLine("  JXL TYPE " + t2.FullName +
                        " base=" + t2.BaseType.FullName);
                    foreach (var i in t2.GetInterfaces())
                        Console.WriteLine("    IMPL " + i.FullName);
                }
        }
        foreach (var dll in new[]
        {
            "C:/Users/jimdg/.zcode/workspace/draft-38d1cc56-ae8c-489e-bbab-35f5de83ad7d/scsm/pdn-qoi/Plugin/bin/Release/QOIPaintDotNet.dll",
            "C:/Users/jimdg/.zcode/workspace/draft-38d1cc56-ae8c-489e-bbab-35f5de83ad7d/scsm/pdn-qoi/Plugin/bin/Release/PdnQoi.dll",
        })
        {
            Console.WriteLine("== " + Path.GetFileName(dll));
            var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(dll);
            foreach (var attr in asm.GetCustomAttributesData())
                Console.WriteLine("  ATTR " + attr.AttributeType.Name);
            Console.WriteLine("  REFS: " + string.Join(", ",
                Array.ConvertAll(asm.GetReferencedAssemblies(),
                    x => x.Name + " " + x.Version)));
            Type t = asm.GetType("QOIPaintDotNet.QoiFileType") ??
                     asm.GetType("PdnQoi.QoiFileType");
            Console.WriteLine("  type: " + (t == null ? "NULL" :
                t.FullName + " base=" + t.BaseType.FullName));
        }
    }
}
