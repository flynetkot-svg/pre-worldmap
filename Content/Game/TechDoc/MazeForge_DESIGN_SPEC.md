# MazeForge — ТЗ на плагин прототипирования уровней
### Ремейк Saboteur 2: Avenging Angel · Unreal Engine 5.8.2 · MVP

**Версия:** 0.3 · **Автор:** Senior C++ UE Dev · **Статус:** к реализации
**Проект:** `C:\Projects\Unreal\5DOT8\LevelEditorPlugin25D` · **Плагин:** `Plugins/MazeForge/`

---

## 0. Как я понял задачу (одним абзацем)

Дизайнеру уровней нужен **конвейер «нарисовал → нарезал → играешь»**, который снимает с него
всю техническую часть. Он не должен думать про сублевелы, стриминг, draw calls и бюджеты —
он получает набор готовых, уже нарезанных и уже подключённых к стримингу комнат, заходит
в комнату, декорирует её, идёт в следующую. Тестировщик в это же время бегает по уровню
и смотрит, где просели фреймтаймы при подгрузке. Плагин — это **инструмент, а не игра**:
никакой геймплейной логики Saboteur 2 в нём нет, он отдаёт «серую коробку» с корректной
геометрией, коллизией и стримингом.

**Ключевое наблюдение по геометрии мира.** Saboteur 2 — вид сбоку. Поэтому рабочая плоскость
рисования — **XZ** (X = ширина мира, Z = высота/этажи), а **Y = глубина**, задаваемая
пользователем. Глубина нужна не ради толщины стен, а ради декора: дизайнер расставляет
3D-модели, предметы и ловушки на переднем и заднем плане, а игрок бежит ровно посередине.
Поэтому Y — это три именованных слоя (BACKGROUND / PLAY / FOREGROUND, см. §2.35), а не
одно число экструзии. Инструмент рисования — 3D-воксельный редактор, но **с активным слоем
по Y**: дизайнер рисует как в 2D, а раскладку по глубине выбирает параметром кисти. Это
на порядок быстрее свободного 3D-рисования, при том что модель данных остаётся честно
трёхмерной (нужно для шахт, переходов и многоэтажных комнат).

**Три поставщика данных → одна модель → один конвейер экспорта.** Ручное рисование,
случайная генерация и импорт картинки — это **три разных источника, наполняющих одну и ту же
воксельную сетку**. Всё, что ниже по конвейеру (нарезка на комнаты, мерж геометрии, экспорт
в сублевелы, стриминг), написано один раз и работает одинаково для всех трёх. Это главное
архитектурное решение: оно и даёт «модульность и расширяемость» из п.1, и экономит нам недели.

**Согласованные решения (по итогам вопросов):**

| Развилка | Решение |
|---|---|
| Стриминг | Classic Level Streaming, `ULevelStreamingDynamic`, собственный пул |
| Геометрия комнаты | `UStaticMesh` на слой глубины + Simple Box Collision только на PLAY. Сборка — прямой `FMeshDescription` с отсевом невидимых граней вместо Merge Actors (§6.2) |
| Глубина Y | Три слоя: BACKGROUND / PLAY / FOREGROUND. Игрок в центре PLAY, декор по краям |
| Порядок работ | Вертикальный срез: ручное рисование → нарезка → стриминг. Генераторы — после |

---

## 0.5 Что дал разбор проекта (v0.2)

Проект `LevelEditorPlugin25D` — шаблон Third Person UE 5.8 с активным вариантом **SideScrolling**.
Пять фактов из кода и конфигов, которые уточняют ТЗ:

**Ф1. Ось движения подтверждена кодом.**
`ASideScrollingCharacter::SetPlaneConstraintNormal(FVector(0,1,0))` — движение персонажа
**жёстко заперто в плоскости XZ**. `ASideScrollingCameraManager` ставит камеру в
`Y = Target.Y + CurrentZoom(1000)` и смотрит в −Y. Значит **X = вдоль уровня, Z = высота,
Y = глубина/ось камеры** — ровно та модель, что заложена в ТЗ. Догадка стала фактом.

**Ф2. Игрок никогда не двигается по Y → стриминг это 2D-задача.**
Раз позиция по Y константна, комнаты режутся **только в плоскости XZ и берут всю глубину Y
целиком**. Слайсер получает `RoomSizeX × RoomSizeZ` (без Z-компоненты по глубине), граф
порталов строится по 4 сторонам вместо 6, пул сравнивает 2D-расстояния. Это заметно упрощает
§6.1, §6.3 и §7 — минус код, минус ошибки.

**Ф3. `MainLevel` — обычный уровень, не World Partition.**
`GameDefaultMap = /Game/Game/Maps/MainLevel`, и в `Content/__ExternalActors__/` его нет —
там только шаблонные `Lvl_ThirdPerson / Lvl_Combat / Lvl_Platforming / Lvl_SideScrolling`.
То есть classic level streaming работает как есть, без конвертации. **Ограничение: `MainLevel`
нельзя переводить на World Partition** — сублевелы и `ULevelStreamingDynamic` с ним не
уживаются. Записать в README для команды.

**Ф4. Камера видит больше, чем точка игрока — и упирается в лимит.**
Камера отнесена на 1000 uu по Y и имеет собственный кадр в XZ, поэтому грузить надо
**по прямоугольнику обзора камеры, а не по точке игрока**, иначе комната «дорисуется» уже
в кадре. Отдельно: `CameraXMinBounds = −400`, `CameraXMaxBounds = 10000` — на длинном
лабиринте камера встанет на X = 100 м и игрок уедет за экран. Лечится в
`UMazeStreamingSubsystem::BeginPlay`: границы берутся из `WorldBounds` манифеста
(или из AABB текущей комнаты, если нужен «покомнатный» кадр).

**Ф5. Лестниц в проекте пока нет, зато есть падение и мягкие платформы.**
В варианте SideScrolling есть `MOVE_Falling`, coyote-time, wall jump, `SideScrollingSoftPlatform`
(проваливание вниз сквозь пол) и `SideScrollingJumpPad`. Лестницы (`MOVE_Custom`) не
реализованы. Значит правило `UMazeRule_VerticalMotion` в MVP закрывает **падение, прыжок
и drop-through**, а ветка Climbing остаётся заглушкой до появления лестниц — интерфейс готов,
кода не пишем.

**Готовый референс в проекте.** `Content/Game/Reference/ImageMap/RefImageMap.uasset` берём
дефолтным входом для режима 3 (тест импорта по картинке).
`Content/Game/Reference/StaticMeshMap/SabRefBigLevel` (OBJ + uasset) используем как эталон
масштаба: из его габаритов калибруем `CellSize`, чтобы нарисованный лабиринт совпадал
по размерам с референсом Saboteur 2.

**Решение по ближней стене (разрез).** Камера смотрит внутрь объёма, поэтому честная экструзия
закрыла бы обзор глухой стеной. Закрыто слоями глубины из §2.35: сгенерированная геометрия
заполняет BACKGROUND и PLAY, а FOREGROUND по умолчанию остаётся пустым — это и есть разрез
как в оригинале, и одновременно рабочее место дизайнера под декор переднего плана.
Отдельный флаг `NearWallMode` не нужен: достаточно добавить `Foreground` в `SolidFillBands`.

---

## 1. Границы MVP

**В скоупе:**
- Плагин `MazeForge`, 3 модуля, C++, без внешних зависимостей.
- Editor Mode с 3D-сеткой, рисованием/стиранием кубами, навигацией по слоям глубины.
- Слои глубины BACKGROUND / PLAY / FOREGROUND: коллизия только на PLAY, края под декор.
- Генератор случайного лабиринта (шахты / переходы / многоэтажные комнаты / границы мира).
- Импорт PNG/JPG с правилами цвета и экструзией по Y.
- Нарезка на комнаты: по сетке (авто) и вручную (боксом).
- Экспорт: мерж в Static Mesh по слоям + Simple Collision + `.umap` на комнату + манифест.
- Рантайм: пул стриминга с правилами (радиус / скорость / направление / падение / лестница),
  менеджер пула, дебаг-оверлей и CSV-профиль загрузок для QA.

**Вне скоупа MVP (но архитектура готова принять):**
Nanite/HLOD-настройки, greedy meshing (v2, за интерфейсом), navmesh-генерация,
мультиплеер, undo для генераторов (только для ручного рисования), автолейаут декора,
геймплей Saboteur 2.

---

## 2. Архитектура

### 2.1 Модули

```
MazeForge/
├── MazeForge.uplugin
└── Source/
    ├── MazeForgeCore/        (Runtime)  Модель данных, ассеты, генераторы. Без Editor-зависимостей.
    ├── MazeForgeStreaming/   (Runtime)  Пул загрузки, менеджер, правила, трекер игрока.
    └── MazeForgeEditor/      (Editor)   EdMode, Slate-панели, слайсеры, экспортёр, мерж.
```

Почему три: `Editor` не должен попадать в билд игры; `Streaming` знает только про **манифест**
(лёгкий DataAsset), но **ничего не знает** про воксельную сетку — это позволяет удалить весь
редакторский пайплайн из шипящей игры и не тащить мегабайты сетки в рантайм.

**Правило зависимостей (жёсткое):**
`Editor → Core`, `Editor → Streaming`, `Streaming → Core`. Обратных стрелок нет.

### 2.2 Поток данных

```
   ┌─ UMazeGenerator_Manual  (EdMode: кисть)      ┐
   ├─ UMazeGenerator_Random  (параметры + seed)   ├──►  FMazeGrid  ──►  UMazeGridAsset (.uasset)
   └─ UMazeGenerator_Image   (PNG/JPG + правила)  ┘     (воксели)        источник правды
                                                             │
                                      IMazeRoomSlicer ◄──────┘
                                   (по сетке / вручную)
                                             │
                                      TArray<FMazeRoomDesc>
                                             │
                             FMazeLevelExporter (Editor-only)
                          ┌──────────────────┼──────────────────┐
                    IMazeMeshBuilder    BoxCollisionBuilder   Level I/O
                  (cull → merge → SM)   (жадные боксы)     (.umap на комнату)
                                             │
                                   UMazeWorldManifest (.uasset)
                                   rooms[] { id, level, bounds, neighbors }
                                             │
                          ═══════════ ГРАНИЦА РАНТАЙМА ═══════════
                                             │
                              UMazeStreamingSubsystem (UWorldSubsystem)
                              ├─ UMazeRoomPool          (бюджеты, гистерезис)
                              ├─ TArray<UMazeStreamingRule*>  (data-driven)
                              └─ UMazeStreamingComponent на пешке (позиция/скорость/режим)
                                             │
                                   ULevelStreamingDynamic
```

### 2.3 Модель данных (Core)

```cpp
UENUM() enum class EMazeCellType : uint8 { Empty=0, Solid=1, Floor=2, Ladder=3, Portal=4, Spawn=5 };

USTRUCT() struct FMazeCell            // 2 байта
{
    UPROPERTY() EMazeCellType Type = EMazeCellType::Empty;
    UPROPERTY() uint8 PaletteIndex = 0;   // какой меш/материал из палитры
};

USTRUCT() struct FMazeGrid
{
    UPROPERTY() FVector    CellSize   = FVector(100.f);  // 3.1.2 — задаёт пользователь
    UPROPERTY() FIntVector Dimensions = FIntVector(256, 8, 128); // X, Y(глубина), Z
    UPROPERTY() FVector    WorldOrigin = FVector::ZeroVector;
    UPROPERTY() TMap<FIntVector, FMazeCell> Cells;       // разрежённое хранение

    // API: Get/Set/Clear/IsSolid/AreAllNeighborsSolid/ForEachInBox/GetWorldBounds
};
```

**Разрежённое хранение (TMap) — осознанный выбор:** лабиринт по определению в основном пустой,
и правки идут точечно. При 256×8×128 плотный массив = 262k ячеек в памяти всегда; разрежённый
хранит только нарисованное. Для экспорта делаем локальный плотный кэш на комнату.

**Ассеты (Data Driven Design):**

| Ассет | Класс | Содержит |
|---|---|---|
| Сетка лабиринта | `UMazeGridAsset` | `FMazeGrid`, инстанс генератора, конфиг нарезки, список комнат |
| Палитра/сборка | `UMazeBuildSettings : UDataAsset` | тип ячейки → меш+материал, пути экспорта, правила нейминга, флаги мержа |
| Правила стриминга | `UMazeStreamingRulesAsset : UDataAsset` | инстансы правил, бюджеты, гистерезис |
| Манифест мира | `UMazeWorldManifest : UDataAsset` | комнаты: id, `TSoftObjectPtr<UWorld>`, AABB, координаты, граф соседей |

### 2.35 Глубина Y — слои декорирования (уточнение v0.3)

Глубина по Y задаётся пользователем и существует **не ради толщины стен, а ради декора**:
дизайнер ставит 3D-модели, предметы и ловушки на передний и задний план, а игрок бежит
ровно посередине. Значит Y — это не «сколько ячеек выдавить», а **три именованных слоя**:

```
    Y                                            камера на +Y, смотрит в −Y
    ↑
    ├─ FOREGROUND  (Nf ячеек)  декор переднего плана · БЕЗ коллизии · рендер поверх игрока
    ├──────────────────────────────────────────────────────────────────────
    ├─ PLAY        (Np ячеек)  геометрия лабиринта · ВСЯ коллизия      ← игрок здесь, в центре
    ├──────────────────────────────────────────────────────────────────────
    └─ BACKGROUND  (Nb ячеек)  декор заднего плана · БЕЗ коллизии
```

```cpp
UENUM() enum class EMazeDepthBand : uint8 { Background, Play, Foreground };

USTRUCT() struct FMazeDepthProfile
{
    UPROPERTY(EditAnywhere) int32 BackgroundCells = 3;
    UPROPERTY(EditAnywhere) int32 PlayCells       = 2;   // толщина игрового слоя
    UPROPERTY(EditAnywhere) int32 ForegroundCells = 2;
    // какие слои заполняет сгенерированная геометрия; Foreground по умолчанию
    // оставлен ПУСТЫМ — это рабочее место дизайнера
    UPROPERTY(EditAnywhere) TSet<EMazeDepthBand> SolidFillBands { Background, Play };

    int32       TotalCells() const;
    float       PlayPlaneY() const;        // мировая Y, где стоит пешка
    FInt32Range CollisionRange() const;    // PLAY + радиус капсулы, округлённый до ячеек
};
```

Четыре следствия, каждое экономит нам работу или чинит потенциальный баг:

**С1. Коллизия строится только для полосы PLAY.** Игрок заперт в плоскости и физически не может
дотянуться до декора — генерировать боксы на всю глубину незачем. Полоса = `PlayCells`,
расширенная на радиус капсулы. На профиле `3/2/2` это **28% глубины вместо 100%**: втрое меньше
`FKBoxElem` и ноль шансов зацепиться за фоновую модель.

**С2. Мировой ноль ставим по центру PLAY.** `WorldOrigin.Y` подбирается так, чтобы центр игрового
слоя попал на **Y = 0**. Тогда штатный `SetPlaneConstraintNormal(0,1,0)` из
`ASideScrollingCharacter` работает как есть, с origin по умолчанию — **в коде игры не меняем
ни строчки**.

**С3. Комната экспортируется тремя мешами, по одному на слой.** Разделение почти бесплатное
(тот же билдер трижды), но даёт: коллизию только на PLAY, возможность гасить/фейдить FOREGROUND,
когда он перекрывает игрока, и раздельное декорирование планов. Критерий приёмки №3 обновлён:
**3 актёра и ≤ 3 draw calls на комнату** вместо одного.

**С4. Кисть умеет выбирать глубину.** У инструмента рисования параметр
`DepthApply ∈ { SolidFillBands (по умолчанию), ActiveSliceOnly, AllDepth, CustomRange }`.
Дефолт — «рисую силуэт, он сам ложится в BACKGROUND+PLAY»; остальные режимы для точечной
правки конкретного слоя.

> **Открытый вопрос на потом (не блокер).** Модели переднего плана будут перекрывать игрока.
> Классическое лечение — материал со сферической маской вокруг пешки. В MVP не делаем, но
> FOREGROUND уже отдельный помеченный актёр, поэтому включение фейда позже — правка материала,
> а не пересборка пайплайна.

### 2.4 Точки расширения (интерфейсы)

Все три — `UCLASS(Abstract, EditInlineNew, DefaultToInstanced)`, поэтому **новая реализация
автоматически появляется в выпадающем списке панели, а её параметры — в Details, без единой
строчки UI-кода**. Это и есть требование п.1/п.2 в самой дешёвой форме.

```cpp
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class UMazeGeneratorBase : public UObject
{
    UPROPERTY(EditAnywhere, Category="Seed") int32 Seed = 0;
    virtual void Generate(FMazeGrid& InOut, FRandomStream& Rng) PURE_VIRTUAL(,);
    virtual FText GetDisplayName() const;
};
// Реализации: _Manual (no-op), _Random, _Image.  Добавить свой = 1 файл.

UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class UMazeRoomSlicerBase : public UObject
{
    virtual void Slice(const FMazeGrid&, TArray<FMazeRoomDesc>& Out) PURE_VIRTUAL(,);
};
// Реализации: _UniformGrid, _Manual.  v2: _FloodFill (комната = связная полость).

UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class UMazeStreamingRule : public UObject
{
    UPROPERTY(EditAnywhere) float Weight = 1.f;
    // Пишет "желание" загрузить комнату в диапазоне 0..1
    virtual void Score(const FMazeStreamQuery&, const UMazeWorldManifest&,
                       TMap<FName,float>& InOutDesire) const PURE_VIRTUAL(,);
};
// Реализации: _Radius, _VelocityPredict, _VerticalMotion, _PortalGraph.

// Editor-only, чтобы можно было заменить мерж на greedy meshing без правок экспортёра:
class IMazeMeshBuilder
{
    virtual UStaticMesh* Build(const FMazeRoomDesc&, const FMazeGrid&,
                               const UMazeBuildSettings&, const FString& PackagePath) = 0;
};
// MVP: FMazeMeshBuilder_Merge (MeshMergeUtilities).  v2: FMazeMeshBuilder_Greedy.
```

---

## 3. Режим 1 — ручное рисование (п.3.1)

**Реализация:** `UMazeEdMode : UEdMode` + `FMazeEdModeToolkit` (панель Slate) + три
`UInteractiveTool`: `UMazePaintTool`, `UMazeEraseTool`, `UMazeRoomBoxTool`.
Interactive Tools Framework берёт на себя click-drag, хоткеи и undo — своего кода минимум.

**3.1.1 Отрисовка 3D-сетки.** `FMazeGridRenderer` рисует через `FPrimitiveDrawInterface`:
активный Y-слой — ярко, ±N соседних слоёв — с затуханием по альфе, остальное не рисуем.
Это держит отрисовку в тысячах линий, а не в сотнях тысяч. Габариты сетки (X, Y, Z в ячейках)
редактируются в панели.

**3.1.2 Масштаб.** `CellSize` — `FVector`, дефолт `(100,100,100)`. Неравномерный масштаб
разрешён (например 100×200×100 — «толстый» мир по глубине). Меняется в любой момент,
воксели хранятся в целочисленных координатах и не ломаются.

**3.1.3 / 3.1.4 Кисть и ластик.**
- Луч из курсора → пересечение с активной Y-плоскостью → ячейка. Если под курсором уже есть
  воксель — режим «прилипания к грани» (как в Minecraft): новый ставится на соседнюю ячейку
  со стороны нормали.
- ЛКМ — поставить, `Shift`+ЛКМ — стереть, drag — непрерывное рисование.
- Drag с зажатым `Ctrl` — прямоугольная заливка/вырезание боксом (главный ускоритель:
  комната 20×20 рисуется одним движением, а не 400 кликами).
- Размер кисти 1…9 (`[` / `]`), тип ячейки из палитры.
- `Ctrl`+колесо — переключение активного Y-слоя; `PgUp/PgDn` — то же.
- Всё через `FScopedTransaction` → штатный Ctrl+Z редактора.

**Превью.** `AMazePreviewActor` с одним `UHierarchicalInstancedStaticMeshComponent` на тип
ячейки. Обновление — батчем в конце кадра (dirty-флаг), а не на каждый клик. Превью
существует только в редакторе и не экспортируется; финальная геометрия — мерженый Static Mesh.

---

## 4. Режим 2 — случайная генерация (п.3.2)

`UMazeGenerator_Random`. Топология подобрана под Saboteur 2 (вертикальные шахты, горизонтальные
переходы, многоэтажные залы), а не под «идеальный лабиринт» из учебника.

**Параметры (все — UPROPERTY, значит панель бесплатная):**

```
World:      SizeX, SizeY(глубина), SizeZ, CellSize, Seed
Shafts:     Count / MinSpacing / Width / bConnectToTop
Corridors:  HeightInCells, MinLength, MaxLength, VerticalStep
Rooms:      Count, MinSize, MaxSize, Margin, FloorsMin, FloorsMax,
            FloorThickness (толщина перекрытий), FloorGapMin (высота этажа), LadderChance
Borders:    Left / Right / Top / Bottom  ∈ { Closed, Open }
Depth:      PlayableDepthCells, WallDepthCells   (экструзия XZ→Y)
```

**Алгоритм (детерминированный от Seed):**
1. Залить объём `Solid`.
2. Разбить XZ BSP-ом на секторы, в каждом с вероятностью — комната случайного размера.
3. Вырезать вертикальные шахты по X с шагом `MinSpacing`, шириной `Width`.
4. В комнатах нарезать этажи: слои `Floor` толщиной `FloorThickness` через `FloorGapMin`,
   в них — проёмы под лестницы (`Ladder`).
5. Соединить: горизонтальные переходы на уровнях этажей между соседями по графу секторов
   и до ближайшей шахты.
6. Границы: `Closed` → стена в 1 клетку; `Open` → оставить выходы наружу (стыковка с соседним
   лабиринтом позже).
7. **Экструзия по Y:** решение XZ растягивается на `PlayableDepthCells` в центре, по бокам —
   `WallDepthCells` сплошняка.
8. **Проверка связности:** flood-fill от точки спавна. Недостижимые полости либо прорубаются
   к основному объёму, либо заливаются обратно (флаг `bCarveIslands`).

Генерация пишет в тот же `FMazeGrid` — дизайнер может дорисовать результат руками. Это важно:
генератор не «конкурирует» с ручным режимом, а является его стартовой точкой.

---

## 5. Режим 3 — генерация по картинке (п.3.3)

`UMazeGenerator_Image`.

- **Источник:** `TSoftObjectPtr<UTexture2D>` (импортированный ассет) **или** путь к файлу
  на диске — читаем через `IImageWrapperModule` (PNG, JPEG, BMP, TGA). Оба пути ведут в один
  буфер `TArray<FColor>`.
- **3.3.1 Глубина:** `ExtrudeDepthCells` + `bHollow` (только оболочка, чтобы не плодить
  невидимые воксели внутри массива).
- **3.3.3 Правила цвета — data-driven:**
  ```cpp
  USTRUCT() struct FMazeColorRule {
      UPROPERTY(EditAnywhere) FLinearColor Color;      // например чёрный
      UPROPERTY(EditAnywhere) float Tolerance = 0.15f; // радиус в RGB
      UPROPERTY(EditAnywhere) EMazeCellType Type;      // → Solid
      UPROPERTY(EditAnywhere) uint8 PaletteIndex = 0;
  };
  ```
  Дефолтный набор: чёрный → `Solid`, белый → `Empty`. Дизайнер может добавить, например,
  красный → `Spawn`, синий → `Ladder`. Первое подошедшее правило выигрывает; ничего не подошло →
  `FallbackType`.
- **Масштаб:** `PixelsPerCell` (даунсемпл по большинству голосов) и ориентация
  (image X→world X, image Y→world **Z**, т.к. картинка = вид сбоку).

---

## 6. Система 1 — нарезка и создание уровней (п.4.1)

### 6.1 Нарезка (4.1.1)
- `UMazeSlicer_UniformGrid`: `RoomSizeXZ` (напр. 32×32 ячейки) + `Origin`. **Комната всегда
  берёт всю глубину Y** — игрок по Y не двигается (Ф2), резать её незачем. Пустые комнаты
  (0 солид-ячеек) отбрасываются. Один клик — вся карта нарезана.
- `UMazeSlicer_Manual`: дизайнер тянет бокс в вьюпорте (`UMazeRoomBoxTool`) → «Create Room».
  Пересечения комнат подсвечиваются как ошибка.
- Результат: `TArray<FMazeRoomDesc>` в `UMazeGridAsset`; каждая комната рисуется в вьюпорте
  цветным габаритом с именем — дизайнер видит будущую карту стриминга ещё до экспорта.

### 6.2 Сборка геометрии (4.1.2)
Порядок в `FMazeMeshBuilder_Merge`:
1. **Отсев скрытых ячеек:** ячейка пропускается, если все 6 соседей `Solid`. Убирает ~70–90%
   геометрии на плотных участках. Одна дешёвая проверка — крупнейший выигрыш в пайплайне.
2. Спавн кубов-компонентов во временном `UWorld` по палитре.
3. `IMeshMergeUtilities::MergeComponentsToStaticMesh` → один `UStaticMesh` на комнату
   (материалы объединяются по палитре, при `bMergeMaterials` — в атлас).
4. **Simple Collision:** отдельно от рендер-меша и **только для слоя PLAY** (С1). Жадное
   объединение солид-ячеек в боксы (прогон по X → слияние по Z → слияние по Y) → десятки
   `FKBoxElem` вместо тысяч. `CollisionTraceFlag = CTF_UseSimpleAsComplex`, сложная коллизия
   у меша выключена. Мешы BACKGROUND и FOREGROUND получают `NoCollision`.
5. Шаги 1–4 прогоняются **по одному разу на слой глубины** (С3). Ассеты сохраняются как
   `/Game/MazeForge/Meshes/SM_<RoomId>_<Band>`.

> **Отступление от первоначального выбора (зафиксировано на P2a).** Merge Actors склеивает
> кубы целиком, со всеми шестью гранями каждого: комната из 1000 видимых ячеек дала бы
> 12 000 треугольников, из которых видна пятая часть. Плюс он требует заспавнить тысячи
> временных компонентов в мире — минуты на сотню комнат. Поэтому реализация
> `FMazeMeshBuilder_Faces` строит `FMeshDescription` напрямую и **создаёт только те грани,
> за которыми пустота**; стык двух массивов не даёт ни одного треугольника. Результат для
> дизайнера тот же — один меш на слой комнаты с простой коллизией. Merge-реализацию можно
> добавить рядом за тем же `IMazeMeshBuilder`.

*Расширение v3:* склейка копланарных квадов (полноценный greedy meshing) — ещё минус
треугольники на больших плоских стенах.

### 6.3 Экспорт в уровни (4.1.3)
Для каждой комнаты:
1. Создать/открыть пакет уровня `/Game/MazeForge/Maps/L_Room_<Id>.umap`.
2. Заспавнить **три** `AStaticMeshActor` (`_BG` / `_Play` / `_FG`, С3) + `AMazeRoomAnchor`
   (id, AABB, порталы, `FMazeDepthProfile`, дебаг-виз). Якорь рисует в редакторе границы слоёв
   глубины и сетку ячеек — дизайнер видит, куда можно ставить декор и где проходит игрок.
3. Пометить **всё сгенерированное** тегом `MazeForge.Generated`.
4. Сохранить пакет; добавить `ULevelStreamingDynamic` в persistent-уровень (по умолчанию
   выключен — грузит пул); записать запись в `UMazeWorldManifest`.

> **Правило безопасного ре-экспорта (обязательное).** Повторный экспорт удаляет из уровня
> **только актёров с тегом `MazeForge.Generated`** и не трогает ничего, что положил дизайнер.
> Без этого весь смысл «дизайнер декорирует комнату» ломается на первой же правке лабиринта.
> Перед перезаписью — отчёт «будет заменено N актёров, сохранено M пользовательских».

**Граф соседей.** При экспорте комнаты сканируются **4 боковые грани** (±X, ±Z; по Y комнаты
не граничат — Ф2): если через общую грань есть проходимая (`Empty`/`Ladder`) ячейка — это портал. Порталы пишутся в манифест и дают рантайму **графовую**
близость вместо евклидовой. В лабиринте с толстыми стенами это принципиально: соседняя по
воздуху комната может быть в 200 метрах пути.

---

## 7. Система 2 — пул стриминга (п.4.2)

### 7.1 Состав

| Класс | Роль |
|---|---|
| `UMazeStreamingSubsystem : UWorldSubsystem` | Точка входа. Держит манифест, пул, правила. Тик 10 Гц. |
| `UMazeRoomPool` | Бюджеты, гистерезис, очередь загрузки/выгрузки, состояния комнат. |
| `UMazeStreamingComponent` | Вешается на пешку. Отдаёт позицию, скорость, режим движения. |
| `UMazeStreamingRule` (×4) | Data-driven правила приоритета. |
| `FMazeStreamDebugger` | HUD-оверлей, `stat MazeForge`, CSV-лог для QA. |

### 7.2 Цикл (4.2.1 — правила загрузки)

```
1. Query = { Location, Velocity, MovementMode(Walking|Falling|Climbing|Flying), CurrentRoomId }
2. Desire: TMap<FName,float> = Σ Rule[i].Score(Query) * Rule[i].Weight
3. Пул сортирует по Desire:
      Desire ≥ LoadThreshold (0.35)   и есть место в бюджете → LOAD
      Desire ≤ UnloadThreshold (0.15) и прошло MinTimeLoaded  → UNLOAD
   (два порога = гистерезис: комната не мигает на границе)
4. Одновременных загрузок ≤ MaxConcurrentLoads (2); StreamingPriority = Desire
5. Комната под игроком не выгружается никогда (жёсткий пин)
```

**Правила:**
- `UMazeRule_CameraFrame` — базовое (заменяет чистый радиус, Ф4): прямоугольник обзора камеры
  в XZ, расширенный на `MarginCells`; попал в него — `Score = 1`, дальше спад до `FalloffCells`.
  Именно камера, а не игрок, определяет, что обязано быть загружено **сейчас**.
- `UMazeRule_Radius` — подстраховка: `1 - Dist2D/Radius` до AABB комнаты (в XZ, Ф2).
- `UMazeRule_VelocityPredict` — точка-зонд `Loc + Vel * LookAheadSeconds`; `LookAhead` растёт
  со скоростью (быстро бежит → грузим дальше вперёд). Это и есть «в зависимости от скорости».
- `UMazeRule_VerticalMotion` — по `MovementMode`:
  *Falling* → зонд вниз на `v²/2g` (прогноз точки приземления, до 3 комнат) — покрывает и
  проваливание сквозь `SideScrollingSoftPlatform`, и wall jump;
  *Walking* → лёгкий буст по направлению движения;
  *Climbing* → заглушка до появления лестниц в проекте (Ф5): ветка есть, кода нет.
- `UMazeRule_PortalGraph` — BFS по графу порталов от текущей комнаты, `Score = 1/(Depth+1)`,
  глубина ≤ `MaxDepth` (3). Самое надёжное правило в лабиринте; остальные его дополняют.

### 7.3 Менеджер и диагностика (4.2.2)
- **Границы камеры:** на `BeginPlay` подсистема выставляет `CameraXMinBounds/CameraXMaxBounds`
  у `ASideScrollingCameraManager` из `WorldBounds` манифеста (Ф4). Без этого камера упирается
  в дефолтный лимит X = 10000 и игрок уезжает за кадр.
- Бюджеты: `MaxLoadedRooms`, `MaxLoadedTrisEstimate` (из манифеста), `MaxConcurrentLoads`.
- Фолбэк: игрок вошёл в незагруженную комнату → блокирующая загрузка + `LogMazeForge Warning`
  + счётчик `StreamingMiss` в CSV. **Ноль промахов** — критерий приёмки для QA.
- Дебаг: `MazeForge.DebugStreaming 1` — список комнат (состояние, Desire, время загрузки мс),
  габариты загруженного в вьюпорте, `MazeForge.DumpStreamCSV` — выгрузка для отчёта
  тестировщиков. Именно этим команда тестирования измеряет «плавность».

---

## 8. Карта файлов

```
MazeForgeCore/
  Public/Data/      MazeTypes.h  MazeGrid.h  MazeRoomDesc.h
  Public/Assets/    MazeGridAsset.h  MazeBuildSettings.h  MazeWorldManifest.h
  Public/Generators/MazeGeneratorBase.h  MazeGenerator_Manual.h
                    MazeGenerator_Random.h  MazeGenerator_Image.h
  Public/Slicers/   MazeRoomSlicerBase.h  MazeSlicer_UniformGrid.h  MazeSlicer_Manual.h

MazeForgeStreaming/
  Public/           MazeStreamingSubsystem.h  MazeRoomPool.h  MazeStreamingComponent.h
                    MazeStreamingRulesAsset.h
  Public/Rules/     MazeStreamingRule.h  MazeRule_Radius.h  MazeRule_VelocityPredict.h
                    MazeRule_VerticalMotion.h  MazeRule_PortalGraph.h
  Private/          MazeStreamDebugger.cpp

MazeForgeEditor/
  Public/Mode/      MazeEdMode.h  MazeEdModeToolkit.h
  Public/Tools/     MazePaintTool.h  MazeEraseTool.h  MazeRoomBoxTool.h
  Public/Render/    MazeGridRenderer.h  MazePreviewActor.h
  Public/Export/    MazeLevelExporter.h  MazeMeshBuilder.h (IMazeMeshBuilder)
                    MazeMeshBuilder_Merge.h  MazeCollisionBuilder.h
  Private/UI/       SMazeForgePanel.cpp
```

Ориентир: **~35 файлов, ≤300 строк каждый**. Файл длиннее 300 строк — повод разрезать
(требование п.1.1).

---

## 9. План работ

| Фаза | Содержание | Оценка |
|---|---|---|
| **P0** | Скелет плагина, 3 модуля, типы, `UMazeGridAsset` + фабрика | 0.5 дн |
| **P1** | EdMode: сетка, кисть, ластик, боксовая заливка, слои, HISM-превью | 2 дн |
| **P2** | Слайсеры + экспортёр: cull → merge → box collision → `.umap` → манифест | 2 дн |
| **P3** | Стриминг: subsystem, пул, 4 правила, компонент, дебаг+CSV | 1.5 дн |
| **P4** | `UMazeGenerator_Random` (шахты/переходы/этажи/границы/связность) | 1.5 дн |
| **P5** | `UMazeGenerator_Image` (ImageWrapper + правила цвета + экструзия) | 0.5 дн |
| **P6** | Полировка, тест-карта 100+ комнат, замер фреймтаймов, README для дизайнеров | 1 дн |
| | **Итого** | **~9 рабочих дней** |

**Играбельный вертикальный срез — после P3 (конец 6-го дня):** дизайнер уже рисует руками,
режет, экспортирует и бегает по стримящемуся лабиринту. P4–P5 добавляют скорость наполнения.

---

## 10. Критерии приёмки

1. Дизайнер за 10 минут без помощи программиста рисует лабиринт ≥ 100×100 ячеек, режет его
   и жмёт Play — игрок бегает, комнаты грузятся/выгружаются.
2. Декор, добавленный в `L_Room_007`, **переживает** повторный экспорт лабиринта.
3. Комната из ≥1000 солид-ячеек = **3 актёра (BG/Play/FG), ≤ 3 draw calls**, коллизия — только
   Simple и только на слое PLAY; BACKGROUND и FOREGROUND не мешают движению пешки.
3a. Дизайнер ставит модель на передний и задний план, игрок пробегает мимо, не задевая её.
4. При беге/падении/подъёме по лестнице — **0 блокирующих загрузок** (`StreamingMiss == 0`)
   при `MaxLoadedRooms = 9`.
5. Один и тот же `Seed` даёт побитово одинаковый лабиринт.
6. Генератор по картинке 512×512 отрабатывает < 2 с.
7. Плагин собирается в `Development Editor` и `Shipping` (в Shipping редакторский модуль
   исключён), 0 варнингов.

---

## 11. Риски и как закрываем

| Риск | Митигация |
|---|---|
| `MergeComponentsToStaticMesh` медленный на больших комнатах | Cull скрытых ячеек до мержа; кап на размер комнаты в слайсере; прогресс-бар с отменой; v2 — greedy meshing за тем же интерфейсом |
| Швы/z-fighting на стыках комнат | Комнаты режутся строго по границам ячеек, мерж в локальных координатах комнаты, пивот в углу AABB |
| Дребезг стриминга на границе комнаты | Два порога + `MinTimeLoaded` + жёсткий пин текущей комнаты |
| Interactive Tools Framework — незнакомый API у команды | Инструменты изолированы в `Tools/`, вся логика правки — в `FMazeGrid`; при проблемах меняем UI-слой, не трогая модель |
| Дизайнер сотрёт свою работу ре-экспортом | Тег `MazeForge.Generated` + отчёт-подтверждение перед перезаписью |
| Тысячи `ULevelStreamingDynamic` в persistent | Записи создаются лениво — только для комнат, которые пул реально просит |

---

## 12. Стартовые условия (закрыто в v0.2)

| Пункт | Статус |
|---|---|
| Папка проекта | ✅ `C:\Projects\Unreal\5DOT8\LevelEditorPlugin25D` подключена |
| Проект / модуль игры | ✅ `LevelEditorPlugin25D`, Runtime, UE 5.8 |
| Persistent-уровень | ✅ `/Game/Game/Maps/MainLevel` — не WP, стриминг заводится как есть |
| GameMode / пешка | ✅ `BP_SideScrollingGameMode` → `ASideScrollingCharacter` |
| Путь плагина | `Plugins/MazeForge/` (папки `Plugins/` пока нет — создаём) |
| Экспорт комнат | `/Game/MazeForge/Maps/L_Room_<Id>.umap` |
| Мешы комнат | `/Game/MazeForge/Meshes/SM_<RoomId>_<Band>` |
| Референс маски | `/Game/Game/Reference/ImageMap/RefImageMap` |
| Референс масштаба | `/Game/Game/Reference/StaticMeshMap/SabRefBigLevel` |

**Открытых блокеров нет — можно начинать с P0.**

---

## 13. Идеи из предыдущей редакции ТЗ (отложены в v2)

Заменённый документ содержал ряд вещей сверх текущего скоупа. Они не выброшены, а положены
в бэклог, потому что каждая — отдельные дни работы:
слои карты (`U25DMapLayer`), отдельный custom asset editor с 14 вкладками вместо EdMode,
кисть разметки streaming-зон, пайплайн валидаторов (spawn point, дубли маркеров, зоны без
target level, битые ссылки), пресеты генерации Cave / Hybrid, разбор/слияние зон.
Любой пункт добавляется поверх текущей архитектуры без её ломки — скажите, если что-то
из этого нужно уже в MVP.
