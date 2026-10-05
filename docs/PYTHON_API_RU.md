# Python и пакетные расчёты

Нужен Python 3.9+ без сторонних пакетов. В полном пакете есть `scripts/opticalcad.py` и `optics_batch` (в `.app/Contents/MacOS` на macOS, `bin` на Windows). В сборке из исходников SDK сам находит `build/optics_batch`. Для другого расположения задайте `OPTICALCAD_BATCH` или передайте `executable=`.

```python
import sys
sys.path.insert(0, "/путь/к/пакету/scripts")
from opticalcad import Project

p = Project.load("examples/singlet.optcad")
p.system["surfaces"][0]["radius"] = 60
p.autofocus()
print(p.analyze(pupil_grid=17))
p.report("report.json")
p.save("result.optcad")
psf = p.diffraction(field=0, size=64, pupil_grid=33)
p.export_geopter("result.json")
```

`Project.data` — структура `.optcad`; `system` и `scene` дают соответствующие разделы. Изменения словаря проверяются C++ перед расчётом или записью. Единицы модели: мм, мкм, Вт. Высокие коэффициенты в формате 7: `asphere` из десяти A4…A22 и `oddAsphere` из десяти A3…A21. При ручном расширении старого документа задайте `data["version"]=7` и полные массивы. Неизвестные/непредставимые данные нельзя сделать совместимыми сменой номера на старый.

Методы: `validate()`, `analyze(pupil_grid=17)`, `autofocus()`, `optimize()` (сохранённый план, либо стандартный), `diffraction(field=0,size=64,pupil_grid=33)`, `trace_scene()`, `report(path)`, `save(path)`, `export_geopter(path)`, `Project.import_geopter(path)`. `analyze` возвращает EFL/BFL, RMS/центры, OPD, кривые концентрации энергии, дисторсию, T/S-фокусы и первичные показатели стекла. Неопределённая точка кривой представлена JSON `null`; ошибки OPD указаны в `wavefront_error`.

`report()` создаёт повторяемый JSON без времени выполнения. `save()` сначала валидирует данные, затем атомарно заменяет файл. `timeout=120` в конструкторе задаёт предельное время отдельного запроса; `subprocess.TimeoutExpired` означает прекращение этого процесса. Для пакетного набора используйте обычный цикл Python и отдельные имена результатов. Исключение `OpticalCADError` содержит сообщение C++.

Встроенная консоль доступна через **Справка → Python-консоль**. Текущий проект уже находится в переменной `project`. После успешного скрипта валидированный результат применяется как одно действие undo. Исключение, остановка или изменение проекта в другом окне не применяют результат. Скрипт работает с обычными правами пользователя; это консоль автоматизации. Поле пути позволяет выбрать интерпретатор; начальное значение берётся из `OPTICALCAD_PYTHON` или PATH. Текущий вариант не включает дистрибутив CPython в пакет.

Пакетный протокол без Python:

```sh
./build/optics_batch request.json
# либо JSON-запрос на stdin
```

Запрос: `{"operation":"analyze","project":{...полный .optcad...},"options":{"pupil_grid":17}}`.
Операции: `validate`, `analyze`, `autofocus`, `optimize`, `diffraction`, `trace_scene`, `trace_rays`, `export_geopter`, `import_geopter` (последняя принимает корневой `data` вместо проекта). `trace_rays` использует первичную волну; опциональный `pupil_grid` задаёт круговую сетку 3…33, иначе выводятся три луча на поле. JSON содержит начало и направление луча, пересечения в локальных вершинных координатах, OPL до последней физической поверхности и мощность. Лучи этого диагностического метода трассируются без отсечения световыми диаметрами; статус `complete` нужно проверять.

Успех: `{"ok":true,"result":{...}}`, код 0. Ошибка: `{"ok":false,"error":"..."}`, код 1. Изменяющие операции возвращают новый `project`; исходный входной файл CLI не перезаписывает. Ограничение запроса — 32 МБ. CLI основан на Qt Core и не требует оконного сервера.
