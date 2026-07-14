# Сборка core (Windows)

Требуется Visual Studio 2022 Build Tools с компонентом "Desktop development with C++"
(на машине разработки уже стоит: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`).

## 1. Разово: поднять vcpkg (папка `vcpkg/` в `.gitignore`, не коммитится)

```powershell
git clone --depth 1 https://github.com/microsoft/vcpkg.git vcpkg
.\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

Зависимости (сейчас: Crow + Asio) описаны в `vcpkg.json` (manifest-режим) и ставятся
автоматически при конфигурации CMake — руками `vcpkg install` вызывать не нужно.

## 2. Конфигурация и сборка

Открыть **Developer PowerShell for VS 2022** (или прогнать `Enter-VsDevShell`, см. ниже),
из корня `StreamSoftNative/`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Корневой `CMakeLists.txt` сам подхватывает `vcpkg/scripts/buildsystems/vcpkg.cmake` как
toolchain file, если папка `vcpkg/` существует — отдельно указывать `-DCMAKE_TOOLCHAIN_FILE`
не нужно.

Бинарник: `build\core\Release\streamsoft_core.exe`. Запуск поднимает оверлей на
`http://127.0.0.1:8099/` (`/chat`, `/events`), веб-контент на dev-сборке читается прямо из
`core/web/` (см. `STREAMSOFT_WEB_DIR` в `core/CMakeLists.txt`) — для инсталлятора это
станет папкой рядом с exe.

## Без Developer PowerShell (из обычного скрипта/CI)

```powershell
$vs = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64"
```

## Проверка, что всё живо

```powershell
.\build\core\Release\streamsoft_core.exe
```

и в другом окне:

```powershell
Invoke-WebRequest http://127.0.0.1:8099/events -UseBasicParsing
Invoke-WebRequest http://127.0.0.1:8099/api/test-chat -Method POST
```

`/ws` — обычный WebSocket, на коннект шлёт `{"type":"config",...}` и историю последних
30 чат-сообщений, дальше — `{"type":"chat",...}` / `{"type":"event",...}` по мере
поступления. Формат один в один с Python-референсом (`softforstream/overlay/app.js` его
уже умеет парсить, поэтому текущий `core/web/static/app.js` — просто копия оттуда).
