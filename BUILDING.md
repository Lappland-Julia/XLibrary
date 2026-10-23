# Сборка XLibrary для Windows и Linux

## Linux и CLion

Нужны Linux с `/proc`, компилятор C++23 (GCC 13+ или Clang 17+ с соответствующей libstdc++), CMake 3.21+, Ninja и Qt **6.5+** с Widgets, Concurrent, Network и DBus. Для глобального сочетания и переключения окон в X11 установите заголовки Xlib. Без Xlib сборка поддерживается, но эти две функции недоступны. Qt для Windows нельзя использовать при сборке Linux.

Например, в Ubuntu **26.04** системный Qt удовлетворяет минимальной версии:

```sh
sudo apt-get update
sudo apt-get install build-essential clang cmake ninja-build qt6-base-dev qt6-wayland libx11-dev
cmake --preset linux-debug
cmake --build --preset linux-debug --parallel 4
./cmake-build-linux-debug/XLibrary
```

В других дистрибутивах установите аналогичные development-пакеты. Если системный Qt старее 6.5, установите Linux Qt отдельно и передайте `-DCMAKE_PREFIX_PATH=/path/to/Qt/gcc_64`. SQLite загружается с проверкой хеша. Для автономной конфигурации можно указать уже проверенные исходники: `-DFETCHCONTENT_SOURCE_DIR_SQLITE3=/absolute/path/to/sqlite-amalgamation`.

В `CMakePresets.json` есть профили `linux-debug`, `linux-release`, `linux-clang-debug`, `windows-debug`, `windows-release` с раздельными каталогами. На Linux:

```sh
cmake --preset linux-release
cmake --build --preset linux-release --parallel 4
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug --parallel 4
cmake --install cmake-build-linux-release --prefix "$HOME/.local"
```

Установка Linux добавляет `bin/XLibrary`, ярлык в `share/applications`, значок и лицензию. Для неё нужны системные runtime-пакеты выбранного Qt и Xlib. Если использован отдельный Qt SDK, настройте поиск его библиотек для установленного приложения. Добавьте `$HOME/.local/bin` в PATH для запуска из меню. Для разработки установка приложения не требуется.

В CLion откройте корневой `CMakeLists.txt`, загрузите CMake presets и включите нужный профиль в **Settings → Build, Execution, Deployment → CMake**. Для Linux выберите Linux toolchain. На Windows с WSL выберите toolchain **WSL / Ubuntu**, Linux CMake и Linux Qt внутри WSL. Если профиль скрыт условием ОС Windows-хоста, создайте WSL CMake profile вручную: Build directory `cmake-build-wsl-debug`, Build type `Debug`, без Windows `CMAKE_PREFIX_PATH`. Для Windows выберите совместимые Qt и MSVC/MinGW, как описано ниже. Выберите цель `XLibrary` и Build/Run/Debug. Пользовательские пути храните в игнорируемом `CMakeUserPresets.json`. Не переносите Windows PATH и `QT_QPA_PLATFORM_PLUGIN_PATH` в Linux-конфигурацию запуска.

Особенности Linux:

- Обнаружение использует XDG `.desktop`; пользовательские записи имеют приоритет. Учитываются `Hidden`, `NoDisplay`, `OnlyShowIn`, `NotShowIn`, `TryExec`, локализованные названия, `Exec`, `Icon`, `Path`, `Terminal` и D-Bus activation. Аргументы пользователя добавляются к `Exec`, shell-подстановок нет. Для `Terminal=true` нужен установленный терминал.
- Можно добавлять абсолютные пути к исполняемым файлам, AppImage и скриптам с shebang и разрешением `+x`. Расширение не обязательно; регистр путей учитывается. AppImage требует его собственных runtime-зависимостей. Статус читается из `/proc`; wrapper, заменяющий себя другим executable, может не сопоставиться с записью. Flatpak/Snap учитываются по доступным метаданным процесса; `hidepid` и права ограничивают наблюдение.
- БД: `$XDG_DATA_HOME/XLibrary/xlibrary.db`, иначе `~/.local/share/XLibrary/xlibrary.db`. Настройки: Qt AppLocalDataLocation, обычно `~/.local/share/XLibrary/XLibrary/settings.ini`. Portable-флаг рядом с бинарником включает каталог `data`; `--check-startup` использует временный профиль. Автозапуск: `$XDG_CONFIG_HOME/autostart`, иначе `~/.config/autostart`.
- Встроенное глобальное сочетание и запрос переключения чужого окна доступны в X11. В Wayland назначьте сочетание в настройках рабочего стола для команды `XLibrary`: второй запуск передаёт запрос работающему экземпляру. Переключение чужих окон ограничено композитором; при недоступности выполняется обычный запуск. Трей зависит от рабочего стола; без трея закрытие завершает приложение, а фоновый старт показывает окно.
- Эффекты рамки Windows доступны только Windows. Linux использует Qt и системное оформление окон. Нативные UI-плагины Linux — `.so` в `plugins` рядом с бинарником, собранные с совместимыми Qt и C++ ABI.

Проверка запуска с временным профилем:

```sh
./cmake-build-linux-debug/XLibrary --check-startup
QT_QPA_PLATFORM=xcb xvfb-run -a dbus-run-session -- ./cmake-build-linux-debug/XLibrary --check-startup
QT_QPA_PLATFORM=offscreen ./cmake-build-linux-debug/XLibrary --check-startup
```

Для Xvfb нужны `xvfb`, `xauth`, `dbus-x11` и runtime-зависимости Qt xcb. CI собирает GCC Debug/Release и Clang Debug, проверяет X11-старт и системную установку. Интерактивные сценарии рабочего стола и пользовательские приложения требуют проверки в конкретной среде.

## Windows

Нужны Windows 10/11 x64, Git, CMake 3.21+, компилятор C++23 и **динамический** Qt 6.5+ с Widgets, Concurrent и Network. Для Ninja нужен `ninja.exe` в PATH. При первой конфигурации CMake скачивает SQLite с проверкой хеша.

Версия приложения задаётся только в корневом файле `VERSION` в формате `major.minor.patch`. CMake использует её для приложения, окна «О программе», ресурсов Windows и упаковщика. После изменения `VERSION` следующая сборка автоматически повторяет конфигурацию.

CI использует Qt **6.8.3**, MSVC 2022 x64 и MinGW **13.1.0** из Qt Online Installer. Для MinGW выбирайте комплект Qt `mingw_64` и соответствующий ему MinGW; для MSVC — `msvc2022_64` и Visual Studio 2022 / Build Tools с Desktop development with C++. Не смешивайте ABI или архитектуры. Скрипты упаковки и проверки требуют **PowerShell 7** (`pwsh`), а не Windows PowerShell 5.1. Упаковка использует `tar.exe` из Windows.

## Debug в CLion

1. Установите совместимые Qt и toolchain. В CLion → Settings → Build, Execution, Deployment → Toolchains выберите MSVC x64 или MinGW из того же набора, что Qt.
2. Создайте CMake profile `Debug`: Build type `Debug`, Build directory `cmake-build-debug`, CMake options `-DCMAKE_PREFIX_PATH=<путь к комплекту Qt>`. Например, сам комплект содержит `bin/Qt6Core.dll` и `lib/cmake/Qt6`.
3. В Run/Debug configuration для XLibrary добавьте к PATH каталоги `bin` выбранных Qt и MinGW (либо используйте среду MSVC toolchain). Если Qt не находит платформенный плагин, задайте `QT_PLUGIN_PATH=<путь к комплекту Qt>/plugins` только в конфигурации запуска IDE.
4. Build/Debug компилирует и запускает приложение с библиотеками среды разработки. Копирования DLL после Build больше нет. Запуск EXE из Проводника проверяйте на развёрнутом пакете.

Из PowerShell, подставив собственные пути:

```powershell
$qt = 'C:/Qt/6.8.3/mingw_64'
$mingw = 'C:/Qt/Tools/mingw1310_64/bin'
$env:PATH = "$mingw;$qt/bin;$env:PATH"
cmake -S . -B cmake-build-debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_CXX_COMPILER=$mingw/g++.exe"
cmake --build cmake-build-debug
./cmake-build-debug/XLibrary.exe
```

## Release

Для MinGW используйте ту же среду и отдельную папку:

```powershell
cmake -S . -B cmake-build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_C_COMPILER=$mingw/gcc.exe" "-DCMAKE_CXX_COMPILER=$mingw/g++.exe" `
  -DXLIBRARY_NATIVE_CPU=OFF
cmake --build cmake-build-release --parallel 4
```

Для MSVC откройте **Developer PowerShell for VS 2022 x64**, затем PowerShell 7 с унаследованной средой:

```powershell
$qt = 'C:/Qt/6.8.3/msvc2022_64'
$env:PATH = "$qt/bin;$env:PATH"
cmake -S . -B build-msvc -G 'Visual Studio 17 2022' -A x64 `
  "-DCMAKE_PREFIX_PATH=$qt" -DXLIBRARY_NATIVE_CPU=OFF
cmake --build build-msvc --config Debug --parallel 4
cmake --build build-msvc --config Release --parallel 4
```

Обычный Build не запускает `windeployqt`, архиватор или Inno Setup. Release сохраняет O2 и IPO/LTO, когда toolchain поддерживает IPO. `XLIBRARY_NATIVE_CPU` должен оставаться OFF для распространяемых файлов. Не переиспользуйте один build-каталог для разных Qt/toolchain.

`cmake --install <build> --config Release --prefix <новая staging-папка>` устанавливает EXE, LICENSE и зависимости Qt через Qt deployment script. Эта низкоуровневая операция поддерживает оба toolchain, но **compiler runtime и материалы для распространения добавляет следующий скрипт**.

## Portable и установщик

Из корня репозитория в PowerShell 7:

```powershell
# MinGW, portable ZIP без установщика:
./packaging/build_release.ps1 -BuildPath cmake-build-release
# MSVC, из developer shell:
./packaging/build_release.ps1 -BuildPath build-msvc
# Установщик дополнительно к portable ZIP (Inno Setup 6.3+):
./packaging/build_release.ps1 -BuildPath build-msvc -Installer `
  -InnoCompiler 'C:/Program Files (x86)/Inno Setup 6/ISCC.exe' `
  -OutputPath release/installer-msvc
```

Скрипт сам собирает Release, выполняет install в новой staging-папке, добавляет runtime и лицензии, проверяет запуск и создаёт ZIP. У MSVC он копирует app-local CRT из `VCToolsRedistDir/x64/Microsoft.VC143.CRT`; вне developer shell передайте `-MsvcRuntimePath <каталог CRT>`. Для MinGW пути DLL запрашиваются у **настроенного** `g++.exe` через `-print-file-name`. Лицензии ищутся в `licenses`, `share/doc`, `share/licenses` выбранного toolchain; для другой раскладки передайте `-CompilerLicensePath <каталог лицензий>`. Нужны как минимум `COPYING3`, `COPYING.RUNTIME` и применимые notices MinGW-w64/winpthreads. Отсутствующие обязательные файлы останавливают упаковку.

По умолчанию результат — `release/v0.1.0-pre-release-msvc/` или `release/v0.1.0-pre-release-mingw/`:

- `XLibrary-0.1.0-pre-release-windows-x64-<compiler>-portable.zip` с корневой папкой приложения;
- `XLibrary-0.1.0-pre-release-windows-x64-<compiler>-setup.exe`, если задан `-Installer`;
- `SHA256SUMS.txt` для ZIP и установщика.

`-BuildPath` и `-OutputPath` принимают абсолютные пути или пути от корня репозитория. Существующий OutputPath не перезаписывается. После успеха staging удаляется; после ошибки остаётся в `build/release-<id>` для диагностики. Среда PATH восстанавливается и при ошибках. В Git попадают только исходники и скрипты; build/dist/release игнорируются.

`build-info.json` внутри пакета содержит версию, Qt, семейство и версию компилятора, конфигурацию, архитектуру, текущий commit, состояние рабочего дерева и UTC-время упаковки. Для публичного выпуска используйте чистое дерево на нужном коммите и проверьте `source_dirty=false`. Архивы и время сборки не обещают побитовой идентичности; версии и входные архивы фиксируются для повторяемой процедуры выпуска.

## Лицензии и исходники зависимостей

XLibrary имеет MIT-лицензию; это не меняет лицензии зависимостей. Скрипт ориентирован на официальный динамический open-source Qt. Он кладёт соответствующие исходные архивы распространяемых модулей в `sources/`, их тексты лицензий — в `licenses/`, а уведомления и ссылки — в `THIRD_PARTY_NOTICES.txt`. Qt DLL/plugins остаются заменяемыми совместимыми сборками; отладка таких изменений обратным проектированием разрешена.

Для Qt 6.8.3 SHA256 архивов qtbase, qtsvg и qtimageformats записаны в `packaging/qt_sources.json`. Архивы скачиваются с `download.qt.io`, и хеш проверяется **до** распаковки. SBOM копируется из `Qt/sbom`, если поставщик его предоставил; отсутствие SBOM у старых Qt не заменяется вымышленными данными. Полный соответствующий исходный архив сохраняет также third-party attribution и инструкции сборки. Для другой версии Qt сначала добавьте независимо проверенные SHA256 в manifest (или передайте `-QtSourceManifest`). При дополнительных Qt-модулях расширьте сопоставление библиотек/плагинов с исходниками; неизвестная Qt DLL останавливает упаковку.

При собственных изменениях Qt замените manifest/источники на точно соответствующие бинарникам и приложите описание изменений. Перед публикацией проверьте применимые условия всех фактически поставляемых зависимостей, в том числе MinGW-w64/winpthreads и права на распространение Microsoft CRT. Не удаляйте licenses/sources из публичного пакета. Исходники XLibrary доступны в репозитории; SQLite находится в public domain.

## Проверка дистрибутива и пользовательских данных

Распакуйте ZIP в доступный для записи каталог, включая путь с пробелами и Unicode:

```powershell
Expand-Archive ./release/v0.1.0-pre-release-mingw/*-portable.zip -DestinationPath './build/Проверка пакета'
$package = (Get-ChildItem './build/Проверка пакета' -Directory).FullName
./.github/scripts/check_package.ps1 -PackagePath $package -Compiler mingw
Get-FileHash ./release/v0.1.0-pre-release-mingw/*-portable.zip -Algorithm SHA256
```

Для MSVC замените каталог и `-Compiler msvc`. Сверьте хеш с `SHA256SUMS.txt`. Проверка проверяет DLL, метаданные и лицензии, запускает копию приложения с очищенным PATH и без Qt-переменных из IDE, из другого рабочего каталога с пробелами/Unicode. `--check-startup` создаёт окно и каталог во временном профиле и завершается с 0; проверяются запуск с флагом portable и без него, а также неизменность существующего portable-профиля. Логи: `build/package-check/`. Скрипт не меняет ваш рабочий пакет.

Перед публичным выпуском дополнительно на чистой Windows без Qt/toolchain:

1. Запустите portable обычным способом. Добавьте приложение, тег, сценарий и измените настройку; завершите XLibrary через меню выхода в трее. Повторно запустите и убедитесь, что данные сохранились в `data/xlibrary.db` и `data/settings.ini` рядом с EXE.
2. Перенесите всю папку в другой путь с Unicode/пробелами и повторите запуск. Текущий рабочий каталог не должен определять расположение данных.
3. Запустите `--check-startup` при существующем каталоге: он не должен менять пользовательские данные. Уберите права записи у тестовой portable-папки: обычный запуск должен сообщить ошибку и не переключаться молча на другой профиль.
4. Установите setup.exe без прав администратора. В `{app}` не должно быть `portable.flag`. Установленная версия сохраняет прежние пути: БД `%LOCALAPPDATA%/XLibrary/xlibrary.db`, настройки `%LOCALAPPDATA%/XLibrary/XLibrary/settings.ini` (Qt AppLocalDataLocation). Portable-профиль от них независим. Проверяйте с резервной копией существующих данных.
5. Повторите проверку сохранения и перезапуска, обновите установку и удалите её. Данные пользовательского профиля должны сохраниться. Схема БД этой задачей не меняется; автоматического переноса между portable и installed нет. Для переноса остановите приложение, сделайте резервную копию и копируйте БД и настройки в соответствующие пути вручную.

CI разделён на три проверки компиляции (MSVC Debug/Release, MinGW Release) и два отдельных package jobs. Пакеты проходят тот же скрипт, затем отдельно распаковываются ZIP и проверяются хеш и запуск. Inno Setup в обычном CI не вызывается; интерактивные сценарии установки, сохранения после перезапуска и отказы ACL остаются ручной проверкой на Windows.

## GitHub pre-release

1. Дождитесь всех checks PR, включая CodeQL, clang-format, actionlint и оба package jobs. Просмотрите diff и `build-info.json`.
2. Выполните ручную проверку выше для ZIP и установщика, включая обновление существующей установки. Сохраните результаты и проверьте отсутствие секретов, сертификатов и локальных путей.
3. С чистого дерева нужного commit соберите публичные файлы выбранного toolchain, сохраните ZIP, setup.exe и SHA256SUMS.txt. CI artifacts — тестовые пакеты, они не публикуются автоматически.
4. После отдельного решения о выпуске создайте тег версии и GitHub Release с отметкой **pre-release**, описанием ограничений Windows x64 и приложенными файлами. Сборка и PR сами не создают Release и не выполняют merge.
