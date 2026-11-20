# PdnQoi — FileType-плагин QOI для Paint.NET

Открытие и сохранение `.qoi` ("Quite OK Image", https://qoiformat.org,
формат phoboslab/qoi v1.0) в Paint.NET 5.x (net11.0, проверено на 5.20).

## Сборка

Требуется .NET SDK 11 (Paint.NET 5.20 собран под net11.0).

```bash
cd Plugin
dotnet build Plugin.csproj -c Release
```

Ссылки на PaintDotNet.*.dll берутся из `C:\Program Files\Paint.NET`
(Private=false, без NuGet-пакетов). Локальный `NuGet.config` отключает
fallback-папки (важно, если Visual Studio Shared NuGetPackages лежит
на отсутствующем диске).

## Установка

Скопировать `Plugin/bin/Release/PdnQoi.dll` (без зависимостей, один файл)
в ОДНО из мест и полностью перезапустить Paint.NET:
- `C:\Program Files\Paint.NET\FileTypes\` (нужны права администратора) —
  для классической установки;
- `Documents\paint.net App Files\FileTypes\` — путь для пользовательских
  плагинов (также работает для классической установки в 5.x).

Важно: скопированная в bin сборка зависит только от самой себя; если DLL
не появляется в диалогах — полностью закройте Paint.NET (процесс) и
запустите заново, PDN не перечитывает FileTypes на лету.

## Тесты

```bash
cd TestApp
dotnet run -c Release -- <путь к папке с .qoi>
```

Харнесс чистый (без зависимостей Paint.NET): декодирует reference-файлы
официального кодека (qoi.h), кодирует своим кодеком, декодирует обратно
и сравнивает пиксели. Эталонные файлы создаются официальным qoiconv:

```bash
gcc -O2 qoiconv.c -o qoiconv.exe     # из репозитория phoboslab/qoi
qoiconv.exe image.png image.qoi
```

## Структура

- `QoiCodecLib/` — чистый кодек (Encode/Decode над BGRA-буферами),
  без зависимостей Paint.NET; тестовый харнесс использует его напрямую
- `Plugin/` — адаптер Surface<->BGRA + FileType-класс
- `TestApp/` — консольный тест
