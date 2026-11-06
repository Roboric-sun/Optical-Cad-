# Ветки, версии и установщики

Актуализировано 7 ноября 2026 года.

## Две постоянные ветки

| Ветка | Назначение |
|---|---|
| [main](https://github.com/Roboric-sun/Optical-Cad-/tree/main) | Исходники выпущенной версии. Установщики публикует существующий workflow после успешных проверок. |
| [development](https://github.com/Roboric-sun/Optical-Cad-/tree/development) | Общая база для следующих исправлений и функций. Начинайте разработку от неё. |

Готовая программа находится в [Releases](https://github.com/Roboric-sun/Optical-Cad-/releases/latest): Windows — `OpticalCAD-Setup-Windows-x64.exe`, Mac Apple Silicon — `OpticalCAD-macOS-arm64.zip`. Кнопка Code → Download ZIP скачивает исходники, для их запуска нужна сборка.

После объединения версии 1.5 через PR #4 ветка development отставала от main. При уборке development продвинута до опубликованной версии. Дублирующие PR #2 и #3 GitHub автоматически закрыл как объединённые. Код программы при уборке веток не изменялся.

## Работа над новой задачей

1. Начните временную ветку от development, например `codex/audit-fixes-1-5-1`.
2. Внесите изменения, проверьте их и создайте PR в development.
3. После объединения и успешной проверки удалите временную ветку. Постоянную ветку под каждую старую версию держать не требуется.
4. Для выпуска перенесите проверенное состояние development в main через PR. Существующий workflow запустит проверки и создаст Release с установщиками. Номера и точки готовых выпусков сохраняются в Releases и Git-тегах.

Документационное обновление при этой уборке помечено `[skip ci]`, чтобы не публиковать новый установщик только из-за пояснения веток.

## История удалённых рабочих веток

Перед удалением проверено: у каждой ветки было **0 собственных коммитов вне main**. Все перечисленные коммиты остаются достижимы из main и development; удаления исходников или истории не было.

| Завершённая ветка | Сохранённый коммит |
|---|---|
| `codex/configurations-tolerances-1-5` | [`f87b5646f66b`](https://github.com/Roboric-sun/Optical-Cad-/commit/f87b5646f66b2f09346780a7e9ae877c32b22b77) |
| `codex/coordinate-systems` | [`68dc764beed4`](https://github.com/Roboric-sun/Optical-Cad-/commit/68dc764beed4ec19894fcac7798e80d7c51f1a79) |
| `codex/dls-optimization` | [`50a48f1c80ec`](https://github.com/Roboric-sun/Optical-Cad-/commit/50a48f1c80eca6bb407502c5f1fe0a3f2f93448d) |
| `codex/release-1-0` | [`7acd7d739447`](https://github.com/Roboric-sun/Optical-Cad-/commit/7acd7d739447b8b7ac29eaa8bb1a396cf93205f7) |
| `codex/surfaces-apertures-1-3-1` | [`a2da2fb7535f`](https://github.com/Roboric-sun/Optical-Cad-/commit/a2da2fb7535fa356259b14ca7d4520a844360e8f) |
| `codex/tester-feedback` | [`696cf02164e3`](https://github.com/Roboric-sun/Optical-Cad-/commit/696cf02164e38cd0eef2cb0e75dcf164775bd17f) |

Дополнительно локально сохранены `artifacts/branch-cleanup-2026-11-07/before.bundle` и `before.json`. Они исключены из Git. Bundle содержит Git-историю и ссылки на момент уборки; это резервная копия, а не новая рабочая ветка GitHub.

Например, при необходимости прежнюю версию можно выделить в новую ветку из сохранённого коммита:

```sh
git fetch origin
git switch -c restore/v1.2 50a48f1c80eca6bb407502c5f1fe0a3f2f93448d
```

Локальные старые рабочие копии, незаписанные изменения и учебные материалы при уборке GitHub не удалялись.
