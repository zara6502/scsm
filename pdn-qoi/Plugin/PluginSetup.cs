// Точка входа плагина: регистрация FileType в Paint.NET.
// В PDN 5.x достаточно атрибута в имени сборки/класса или явной
// регистрации через SupportInfo; используем классический подход —
// статический список FileType через PluginSupportInfo.

using System;
using System.Collections.Generic;

namespace PdnQoi;

// Paint.NET обнаруживает FileType-плагины автоматически по сборке:
// ищет классы-наследники FileType. Явная точка входа не требуется,
// но оставляем маркер для диагностики.
public static class PluginSetup
{
    public const string PluginName = "QOI";
    public const string Author = "zara6502";
    public const string Version = "1.0.0";
}
