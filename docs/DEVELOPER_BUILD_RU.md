# Optical CAD Developer 1.5.2-dev.zemax.1

Отдельная версия для разработчиков и ручного сравнения с Zemax 13. Расчёты импортированных схем разблокированы; графики и JSON-отчёты предупреждают о возможной неточности. Поддержка геометрии, ограничения физических алгоритмов и порядок сравнения описаны в `ZEMAX_13_RU.md`.

Windows x64: установщик `OpticalCAD-Developer-1.5.2-dev.zemax.1-Setup-Windows-x64.exe`. Имя приложения, папка установки, ярлыки и идентификатор удаления отличаются от обычной версии. По умолчанию используется `%LOCALAPPDATA%\Programs\Optical CAD Developer`; настройки сохраняются в отдельном профиле приложения. Можно установить рядом с Optical CAD 1.5.2.

Портативный ZIP: `OpticalCAD-1.5.2-dev.zemax.1-Windows-x64.zip`; распаковать и запустить `bin\optical_cad.exe`. Qt и библиотеки выполнения входят в пакет. Для Python-консоли требуется Python 3.

Сборка из исходников:

```powershell
cmake -S . -B build-developer -DOPTICS_DEVELOPER_BUILD=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build-developer --config Release --parallel
ctest --test-dir build-developer -C Release --output-on-failure
cmake --install build-developer --config Release --prefix release
& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" /DDeveloperBuild=1 /DAppVersion=1.5.2-dev.zemax.1 packaging/windows.iss
```

Workflow `Windows developer build` собирает отдельную ветку, запускает тесты, проверяет портативный пакет, установку и удаление, затем сохраняет установщик, ZIP и SHA256SUMS.txt как артефакт GitHub Actions. Этот workflow не публикует стабильный Release и не меняет основной выпуск.
