// Рефлексия API Paint.NET 5.20: FileType, SaveConfigToken, PropertyCollection.
using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;

internal static class Program
{
    private const string PdnDir = @"C:\Program Files\Paint.NET";

    private static void Dump(string asmName, string typeName)
    {
        var path = Path.Combine(PdnDir, asmName);
        var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
        var t = asm.GetType(typeName);
        Console.WriteLine($"== {typeName} @ {asmName} ==");
        if (t == null) { Console.WriteLine("  NOT FOUND"); return; }

        var bf = BindingFlags.Public | BindingFlags.NonPublic |
                 BindingFlags.Instance | BindingFlags.Static |
                 BindingFlags.DeclaredOnly;
        foreach (var c in t.GetConstructors(bf))
            Console.WriteLine("  ctor(" + Params(c.GetParameters()) + ")");
        foreach (var m in t.GetMethods(bf))
            if (!m.IsSpecialName)
                Console.WriteLine("  " + m.ReturnType.Name + " " + m.Name +
                    "(" + Params(m.GetParameters()) + ")" +
                    (m.IsVirtual ? " virtual" : "") +
                    (m.IsAbstract ? " abstract" : ""));
        foreach (var p in t.GetProperties(bf))
        {
            var setter = p.GetSetMethod(true);
            string mod = setter == null ? "no-set"
                : setter.IsPublic ? "public-set"
                : setter.IsPrivate ? "private-set"
                : (setter.ReturnParameter != null &&
                   setter.ReturnParameter.ToString().Contains("IsExternalInit")
                       ? "init-set" : setter.IsPrivate ? "private-set"
                       : "other-set");
            Console.WriteLine("  prop " + p.PropertyType.Name + " " + p.Name +
                              " [" + mod + "]");
        }
    }

    private static string Params(ParameterInfo[] ps)
    {
        var list = new System.Collections.Generic.List<string>();
        foreach (var p in ps)
            list.Add(p.ParameterType.Name + " " + p.Name +
                     (p.HasDefaultValue ? "=def" : ""));
        return string.Join(", ", list);
    }

    private static int Main(string[] args)
    {
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
        {
            var p = Path.Combine(PdnDir, name.Name + ".dll");
            return File.Exists(p) ? ctx.LoadFromAssemblyPath(p) : null;
        };
        Dump("PaintDotNet.Data.dll", "PaintDotNet.FileType");
        foreach (var asm in new[] { "PaintDotNet.Data.dll", "PaintDotNet.Base.dll", "PaintDotNet.Fundamentals.dll", "PaintDotNet.Core.dll", "PaintDotNet.ComponentModel.dll" })
        foreach (var tn in new[] { "PaintDotNet.PropertyCollection", "PaintDotNet.SaveConfigToken", "PaintDotNet.Document" })
        {
            var path = Path.Combine(PdnDir, asm);
            var asm2 = AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
            if (asm2.GetType(tn) != null)
                Console.WriteLine("FOUND: " + tn + " @ " + asm);
        }
        try {
            var core = AssemblyLoadContext.Default.LoadFromAssemblyPath(
                Path.Combine(PdnDir, "PaintDotNet.Core.dll"));
            Console.WriteLine("CORE LOADED OK, types=" +
                core.GetTypes().Length);
        } catch (Exception ex) {
            Console.WriteLine("CORE LOAD FAIL: " + ex.Message);
        }
        Dump("PaintDotNet.Data.dll", "PaintDotNet.SaveConfigToken");
        Dump("PaintDotNet.Data.dll", "PaintDotNet.Document");
        {
            var asm = AssemblyLoadContext.Default.LoadFromAssemblyPath(
                Path.Combine(PdnDir, "PaintDotNet.Data.dll"));
            foreach (var t in asm.GetTypes())
                if (t.Name.Contains("FileTypeOptions") ||
                    t.Name.Contains("PluginOptions"))
                    Console.WriteLine("TYPE: " + t.FullName + " @ Data");
        }
        Dump("PaintDotNet.Data.dll", "PaintDotNet.FileTypeOptions");
        Dump("PaintDotNet.Base.dll", "PaintDotNet.Document");
        return 0;
    }
}
