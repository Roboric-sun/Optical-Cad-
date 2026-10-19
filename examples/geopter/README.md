# Численные данные Geopter

`kingslake_doublet.json` — неизменённый файл численных параметров из [Geopter, коммит 0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7](https://github.com/heterophyllus/Geopter/blob/0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7/example/book/kingslake_doublet.json). Исходный проект публикуется под [GPL-3.0](https://github.com/heterophyllus/Geopter/blob/0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7/LICENSE.md). Здесь использованы данные примера; программный код его ядра не включён.

Открывайте через **Файл → Импорт Geopter JSON**. Условия и независимые контрольные числа описаны в [эталонах](../../docs/BENCHMARKS_RU.md). Ручные материалы nd:Vd используют нашу аппроксимацию Коши; апертуры без записи в файле оцениваются нашим импортёром.


`dbgauss.json` — оригинальные численные данные [Double Gauss из того же коммита Geopter](https://github.com/heterophyllus/Geopter/blob/0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7/example/dbgauss.json). Ненулевые коэффициенты виньетирования поддержаны в Optical CAD 0.8. Применение и ограничения описаны в [FIELDS_RU.md](../../docs/FIELDS_RU.md); геометрия и реальные точки STOP проверяются, без утверждения численной эквивалентности исполняемому Geopter.


`sasian_triplet_glass.json` — неизменённые данные [триплета Sasian на каталожных стёклах](https://github.com/heterophyllus/Geopter/blob/0edfbf52fcf0e3fc660e5ae33c354fd7a6be80d7/example/book/sasian_triplet_glass.json) из того же коммита. `aspheric_singlet.json` — собственный синтетический пример, расширение нашей тестовой линзы коникой/A4/A12. Он не является примером автора Geopter.

В 1.0.0-rc1 проведено прямое сравнение работающих движков при согласованных лучах и первичных показателях. [Условия и результаты](../../docs/RELEASE_1_0_RU.md).
