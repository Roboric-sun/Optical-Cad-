# Дополнение 1.5

Методы `analyze_configurations()`, `optimize_configurations()`, `tolerances()` и `tolerance_sample(sample, state=0)` вызывают то же C++ ядро, что интерфейс. Совместный поиск меняет проект; Monte Carlo возвращает отчёт, а sample создаёт новый Project. Все индексы API начинаются с 0. Для больших выборок увеличьте timeout. [Полные определения, форматы и пример](STUDIES_1_5_RU.md).

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

Методы: `validate()`, `analyze(pupil_grid=17)`, `autofocus(all_fields=False,pupil_grid=17)`, `optimize()` (сохранённый план, либо стандартный), `diffraction(field=0,size=64,pupil_grid=33)`, `wavefront(field=0,pupil_grid=17)`, `trace_scene()`, `report(path)`, `save(path)`, `export_geopter(path)`, `Project.import_geopter(path)`. `analyze` возвращает EFL/BFL, RMS/центры, OPD, кривые концентрации энергии, дисторсию, T/S-фокусы и первичные показатели стекла. Неопределённая точка кривой представлена JSON `null`; ошибки OPD указаны в `wavefront_error`.

`report()` создаёт повторяемый JSON без времени выполнения. `save()` сначала валидирует данные, затем атомарно заменяет файл. `timeout=120` в конструкторе задаёт предельное время отдельного запроса; `subprocess.TimeoutExpired` означает прекращение этого процесса. Для пакетного набора используйте обычный цикл Python и отдельные имена результатов. Исключение `OpticalCADError` содержит сообщение C++.

Встроенная консоль доступна через **Справка → Python-консоль**. Текущий проект уже находится в переменной `project`. После успешного скрипта валидированный результат применяется как одно действие undo. Исключение, остановка или изменение проекта в другом окне не применяют результат. Скрипт работает с обычными правами пользователя; это консоль автоматизации. Поле пути позволяет выбрать интерпретатор; начальное значение берётся из `OPTICALCAD_PYTHON` или PATH. Текущий вариант не включает дистрибутив CPython в пакет.

Пакетный протокол без Python:

```sh
./build/optics_batch request.json
# либо JSON-запрос на stdin
```

Запрос: `{"operation":"analyze","project":{...полный .optcad...},"options":{"pupil_grid":17}}`.
Операции: `validate`, `analyze`, `autofocus`, `optimize`, `diffraction`, `wavefront`, `trace_scene`, `trace_rays`, `export_geopter`, `import_geopter` (последняя принимает корневой `data` вместо проекта). `trace_rays` использует первичную волну; опциональный `pupil_grid` задаёт круговую сетку 3…65, иначе выводятся три луча на поле. Опция `field` ограничивает поле, `pupils` задаёт до 15000 координат `[x,y]` для независимых сравнений (проверяются тип и диапазон ±2). Ответ также содержит выходную точку/направление, индекс среды и изображение. `wavefront` возвращает OPD по зрачку и RMS/PV на первичной волне. JSON содержит начало и направление луча, пересечения в локальных вершинных координатах, OPL до последней физической поверхности и мощность. Лучи этого диагностического метода трассируются без отсечения световыми диаметрами; статус `complete` нужно проверять.

Успех: `{"ok":true,"result":{...}}`, код 0. Ошибка: `{"ok":false,"error":"..."}`, код 1. Изменяющие операции возвращают новый `project`; исходный входной файл CLI не перезаписывает. Ограничение запроса — 32 МБ. CLI основан на Qt Core и не требует оконного сервера.

## Оптимизация 1.2

`examples/dls_focus.optcad` хранит план DLS в формате 8. `optimize()` возвращает `algorithm` (0 — координатный, 1 — LM), `before`, `after`, `evaluations`, `history`, `termination` и `starts` (valid/before/after/evaluations). В batch операция `autofocus` принимает булев `all_fields` и целый `pupil_grid`. При `all_fields=True` фокус учитывает поля, спектральные веса и мощности лучей. [План, критерии и ограничения](OPTIMIZATION_1_2_RU.md).

## Координаты 1.3

`first_order(step=1e-5)` возвращает дифференциальную матрицу 4×4 до локального изображения и `afocal`. Для CB `analyze()` возвращает геометрию, `image_origin_world_mm`, `spot_coordinates` и явные ошибки неприменимых анализов. `trace_rays` добавляет мировые точки и локальное изображение. [Определения, формат 9, примеры и ограничения](COORDINATES_1_3_RU.md).

## Профили и апертуры 1.3.1

Формат 10 добавляет объект `shape` к поверхностям: `profile` 0/1/2/3 (standard/cylinder X/cylinder Y/toroid), `rotationRadius`, `aperture` 0/1/2 (circle/ellipse/rectangle), `halfX`, `halfY`, `obscuration`. Анализ и первый порядок используют то же ядро, что GUI. [Определения, примеры и ограничения](SURFACES_1_3_1_RU.md).
