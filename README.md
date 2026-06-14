# combo

Небольшая header-only библиотека **комбинаторов** на C++23 вместе с примерами,
которые показывают две её дуальные стороны.

* **`parser.hpp`** — парсер-комбинаторы. Парсер *потребляет* поток токенов и
  либо возвращает значение с непрожёванным остатком входа, либо ошибку с
  позицией:

  ```cpp
  parser : array_view<Token> -> std::expected<result<Token, R>, error>
  ```

* **`gen.hpp`** — генератор-комбинаторы, категорный дуал. Генератор
  *производит* значение, черпая из источника случайности:

  ```cpp
  generator : RNG& -> T
  ```

Ключевая идея в том, что под обеими сторонами лежит **одна и та же алгебра**.
Построение структурированного вывода — это, по сути, «запуск грамматики в
обратную сторону». Соответствие между комбинаторами видно построчно:

| парсер (потребляет) | генератор (производит) | смысл |
|---|---|---|
| `pure(x)` | `constant(x)` | всегда даёт `x` |
| `sym` / `satisfy` | `range` / `oneof` | один атом |
| `a \| b` | `a \| b` | выбор альтернативы |
| `map(p, f)` | `map(g, f)` | преобразовать значение |
| `bind(p, f)` | `bind(g, f)` | контекстно-зависимый шаг |
| `seq(p…)` | `seq(g…)` | кортеж значений |
| `many(p)` | `repeat(g, n)` | последовательность значений |

## Сборка

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Запуск примеров:

```sh
./build/json                 # разобрать встроенный JSON и показать round-trip
./build/json '{"x":[1,2]}'   # ... или передать свой документ аргументом

./build/json_gen             # сгенерировать JSON по схеме (seed 1, 3 документа)
./build/json_gen 7 5         # seed count

./build/maze                 # лабиринт 20x10, seed 42
./build/maze 30 15 123       # width height seed
```

## Пример 1 — JSON-парсер (`examples/json.cpp`)

Грамматика целиком собрана из комбинаторов — включая лексику (числа, строки,
escape-последовательности): последовательность (`>>`, `<<`, `seq`), выбор (`|`),
повторение (`many1`, `count`, `sep_by`), группировку (`between`), сборку строк
(`cat`, `stringify`) и преобразование значения (`map`). Грамматика рекурсивна —
массивы и объекты содержат значения, — поэтому рекурсия замыкается через
указатель на функцию (`make_parser(&parse_value)`), что не даёт типу разрастись
до бесконечного.

```cpp
const auto jarray = combo::map(
    combo::between(combo::tok('['),
                   combo::sep_by(jvalue, combo::tok(',')),
                   combo::tok(']')),
    [](json::array a) { return json::value{std::move(a)}; });

// число — тоже грамматика, без ручного сканирования:
//   number = '-'? digit+ ('.' digit+)? ([eE] [+-]? digit+)?
const auto int_part = combo::cat(combo::opt(combo::sym('-')), digits);
const auto frac     = combo::cat(combo::sym('.'), digits) | combo::pure("");
const auto jnumber  = combo::map(combo::cat(int_part, frac, exp_part),
                                 [](std::string s) { return value{std::stod(s)}; });
```

При неуспехе `combo::parse` возвращает `std::expected<value, combo::error>`, где
`error` несёт `line`, `col` и сообщение. Диагностика старается быть точной: `|`
при провале всех веток сообщает «самую дальнюю» ошибку, а `sep_by` отличает
«элемента нет» от «элемент начался и сломался».

```text
$ ./build/json '[1, 2, ]'
parse error at 1:8: expected value
$ ./build/json '{"a": 1 "b": 2}'
parse error at 1:9: expected '}'
```

## Пример 2 — JSON-генератор по схеме (`examples/json_gen.cpp`)

Это производящий **дуал** парсера из первого примера. Вы описываете форму
документа — поля, типы значений и диапазоны — и получаете генератор случайного
JSON, который ей соответствует (по сути генератор входных данных для
property-based testing или фаззинга). Билдеры схемы
(`integer`, `number`, `boolean`, `one_of_str`, `array_of`, `obj`, `one_of`,
`optional`) собраны из `gen.hpp` (`range`, `real`, `oneof`, `map`, `repeat`) и
лежат в `jsongen.hpp`.

```cpp
jsongen::schema user = jsongen::obj({
    {"id",     jsongen::integer(1, 100000)},
    {"name",   jsongen::one_of_str({"Alice", "Bob", "Carol"})},
    {"roles",  jsongen::array_of(jsongen::one_of_str({"admin", "viewer"}), 0, 3)},
    {"address", jsongen::obj({
        {"city", jsongen::one_of_str({"NY", "LA", "SF"})},
        {"zip",  jsongen::integer(10000, 99999)},
    })},
});
json::value doc = user(rng);   // свежий случайный документ
```

Здесь дуальность замыкается в кольцо: всё, что выдаёт генератор, по построению
является корректным входом для парсера. И демо, и тесты прогоняют каждый
сгенерированный документ через `json::parse` — generate → serialize → parse.

## Пример 3 — генератор лабиринтов (`examples/maze.cpp`)

Здесь две демонстрации.

1. **Дословный дуал JSON-грамматики.** Сетка — это вложенное повторение по
   выбору клеток. Запись `repeat(repeat(wall | floor, W), H)` в точности
   повторяет структуру JSON «массив массивов». Сама по себе она даёт лишь
   случайный **шум**.

   ```cpp
   auto cell = wall | floor;          // дуал parser:  a | b
   auto row  = g::repeat(cell, W);    // дуал parser:  many(p)
   auto grid = g::repeat(row, H);     // ... вложенно, как массив массивов
   ```

2. **Настоящий лабиринт** (recursive backtracker). Алгоритм по природе stateful
   (работает с сеткой), но **каждое случайное решение** он принимает через
   gen-комбинатор: `gen::range` выбирает стартовую клетку, а `gen::oneof` —
   следующее направление. Получается «честный» perfect-maze, в котором вся
   стохастика проходит через библиотеку.

## Структура

```
parser.hpp            парсер-комбинаторы
gen.hpp               генератор-комбинаторы (дуал)
json.hpp              JSON: модель + сериализатор + парсер (на parser.hpp)
jsongen.hpp           генератор JSON по схеме (на gen.hpp)
examples/json.cpp     демонстрация парсинга
examples/json_gen.cpp демонстрация генерации по схеме (+ round-trip)
examples/maze.cpp     генератор лабиринтов
tests/tests.cpp       юнит-проверки + round-trip generate→parse
CMakeLists.txt
```

## Лицензия

MIT — подробности в файле [LICENSE](LICENSE).
