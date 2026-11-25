#include "Lab2.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lab2
{
    namespace
    {
        // Обычные файлы проверяются полностью. Ограничиваем только длину одной строки
        // и число/размер выводимых примеров, а не число анализируемых строк.
        // Размер одного чтения: 256 КиБ. Для файла 5 ГБ память под блок остаётся той же.
        constexpr std::size_t kReadBlockSize = 256 * 1024;
        // Ограничена только отдельная строка (4 МиБ), а не весь размер файла.
        constexpr std::size_t kMaxLineBytes = 4 * 1024 * 1024;
        // Все элементы считаем, но в итоговом отчёте оставляем максимум 20 примеров каждого вида.
        constexpr std::size_t kMaxExamplesPerKind = 20;
        // Максимальная длина одного примера заголовка/абзаца (в байтах).
        constexpr std::size_t kMaxExampleText = 220;
        // Максимальная длина примера целого списка (в байтах).
        constexpr std::size_t kMaxListPreview = 800;

        /// <summary>
        /// Удаляет пробелы, табуляцию и переводы строк по краям текста без копирования данных.
        /// </summary>
        /// <param name="input">Исходная строка или её часть.</param>
        /// <returns>Часть исходного текста без пробельных символов по краям.</returns>
        std::string_view trimView(std::string_view input)
        {
            // Ищем первый непробельный символ: для пустой строки возвращаем пустой вид.
            const auto first = input.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return {};
            const auto last = input.find_last_not_of(" \t\r\n");
            return input.substr(first, last - first + 1);
        }

        /// <summary>
        /// Приводит буквы в строке к нижнему регистру; применяется для сравнения расширений файлов.
        /// </summary>
        /// <param name="text">Строка, которую нужно преобразовать.</param>
        /// <returns>Строка в нижнем регистре.</returns>
        std::string lowerAscii(std::string text)
        {
            // Преобразуем символы для сравнения расширений без учёта регистра.
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch)
                {
                    return static_cast<char>(std::tolower(ch));
                });
            return text;
        }

        // Обрезаем только отображаемые примеры; исходные строки проверяются целиком.
        /// <summary>
        /// Добавляет фрагмент текста в пример для вывода, не разрезая многобайтный символ UTF-8.
        /// Не сокращает сам проверяемый текст: ограничение относится только к сохранённым примерам.
        /// </summary>
        /// <param name="output">Строка, в которую дописывается пример.</param>
        /// <param name="input">Добавляемый фрагмент текста.</param>
        /// <param name="limit">Максимальная длина результата в байтах.</param>
        void appendPreview(std::string& output, std::string_view input, std::size_t limit)
        {
            if (output.size() >= limit)
                return;

            // Для примеров с UTF-8 не разрываем последовательность байтов посередине.
            for (std::size_t i = 0; i < input.size() && output.size() < limit;)
            {
                const unsigned char first = static_cast<unsigned char>(input[i]);
                std::size_t width = (first & 0x80) == 0 ? 1 :
                    ((first & 0xE0) == 0xC0 ? 2 : ((first & 0xF0) == 0xE0 ? 3 : 4));
                if (i + width > input.size() || output.size() + width > limit)
                    break;
                output.append(input.data() + i, width);
                i += width;
            }
        }

        /// <summary>
        /// Добавляет текст абзаца для отображения: заменяет повторяющиеся пробелы одним
        /// и учитывает границы символов UTF-8 при ограничении размера примера.
        /// </summary>
        /// <param name="output">Накопленный пример абзаца.</param>
        /// <param name="input">Очередной фрагмент текста абзаца.</param>
        /// <param name="limit">Максимальный размер примера в байтах.</param>
        void appendNormalizedPreview(std::string& output, std::string_view input, std::size_t limit)
        {
            bool precedingSpace = !output.empty() && output.back() == ' ';
            for (std::size_t i = 0; i < input.size() && output.size() < limit;)
            {
                const unsigned char ch = static_cast<unsigned char>(input[i]);
                if (std::isspace(ch))
                {
                    if (!precedingSpace && !output.empty())
                    {
                        output.push_back(' ');
                        precedingSpace = true;
                    }
                    ++i;
                    continue;
                }
                const std::size_t width = ch < 128 ? 1 :
                    ((ch & 0xE0) == 0xC0 ? 2 : ((ch & 0xF0) == 0xE0 ? 3 : 4));
                if (i + width > input.size() || output.size() + width > limit)
                    break;
                output.append(input.data() + i, width);
                precedingSpace = false;
                i += width;
            }
        }

        /// <summary>
        /// Распознаёт границу многострочного блока кода Markdown (три и более ` или ~).
        /// Допускает не более трёх пробелов перед маркером.
        /// </summary>
        /// <param name="line">Проверяемая строка.</param>
        /// <param name="marker">Выходной параметр: символ ` или ~.</param>
        /// <param name="count">Выходной параметр: количество символов маркера подряд.</param>
        /// <param name="suffix">Выходной параметр: текст после маркеров.</param>
        /// <returns>true, если строка начинается с допустимой последовательности маркеров.</returns>
        bool parseFence(std::string_view line, char& marker, std::size_t& count, std::string_view& suffix)
        {
            std::size_t indent = 0;
            while (indent < line.size() && indent < 4 && line[indent] == ' ')
                ++indent;
            if (indent > 3 || indent == line.size())
                return false;
            // Символ ограждения блока кода — обратная кавычка или тильда.
            const char candidate = line[indent];
            if (candidate != '`' && candidate != '~')
                return false;
            std::size_t end = indent;
            while (end < line.size() && line[end] == candidate)
                ++end;
            if (end - indent < 3)
                return false;
            marker = candidate;
            count = end - indent;
            suffix = line.substr(end);
            return true;
        }

        // Отбрасываем комментарии до анализа HTML/AsciiDoc, чтобы примеры синтаксиса
        // внутри комментариев не превращались в ложные ошибки языка.
        /// <summary>
        /// Удаляет из проверяемой строки содержимое HTML-комментариев &lt;!-- ... --&gt;.
        /// Запоминает незакрытый комментарий между строками, чтобы его содержимое
        /// не принималось за HTML или AsciiDoc-разметку.
        /// </summary>
        /// <param name="line">Очередная строка исходного документа.</param>
        /// <param name="inComment">Состояние: находимся ли внутри комментария; обновляется функцией.</param>
        /// <returns>Строка без текста HTML-комментариев.</returns>
        std::string withoutComments(std::string_view line, bool& inComment)
        {
            // Если комментария нет, избегаем лишнего копирования частей строки в цикле.
            if (!inComment && line.find("<!--") == std::string_view::npos)
                return std::string(line);

            std::string output;
            for (std::size_t pos = 0; pos < line.size();)
            {
                if (inComment)
                {
                    const auto end = line.find("-->", pos);
                    if (end == std::string_view::npos)
                        break;
                    inComment = false;
                    pos = end + 3;
                }
                else
                {
                    const auto start = line.find("<!--", pos);
                    if (start == std::string_view::npos)
                    {
                        output.append(line.substr(pos));
                        break;
                    }
                    output.append(line.substr(pos, start - pos));
                    inComment = true;
                    pos = start + 4;
                }
            }
            return output;
        }

        // Все проверки линейные и нерекурсивные: std::regex на длинных
        // строках может выбросить std::regex_error или переполнить стек MSVC.
        /// <summary>
        /// Проверяет заголовок Markdown формата ATX: от одного до шести символов # в начале.
        /// </summary>
        /// <param name="line">Исходная строка Markdown.</param>
        /// <param name="heading">Выходной параметр: текст заголовка после символов #.</param>
        /// <returns>true, если строка соответствует синтаксису ATX-заголовка.</returns>
        bool parseLongAtxHeading(std::string_view line, std::string_view& heading)
        {
            // Для ATX-заголовка допускается не больше трёх пробелов перед #.
            const auto indent = line.find_first_not_of(' ');
            if (indent == std::string_view::npos || indent > 3 || line[indent] != '#')
                return false;
            std::size_t cursor = indent;
            while (cursor < line.size() && line[cursor] == '#')
                ++cursor;
            if (cursor - indent > 6)
                return false;
            if (cursor != line.size() && line[cursor] != ' ' && line[cursor] != '\t')
                return false;
            heading = trimView(line.substr(cursor));
            return true;
        }

        /// <summary>
        /// Определяет строку элемента маркированного (-, +, *) или нумерованного (1. или 1) списка.
        /// После маркера должны идти пробельный символ и текст элемента.
        /// </summary>
        /// <param name="line">Строка, которую требуется проверить.</param>
        /// <returns>true, если строка распознана как элемент списка.</returns>
        bool isLongListLine(std::string_view line)
        {
            std::size_t cursor = line.find_first_not_of(" \t");
            if (cursor == std::string_view::npos)
                return false;
            if (line[cursor] == '-' || line[cursor] == '+' || line[cursor] == '*')
                ++cursor;
            else
            {
                const auto start = cursor;
                while (cursor < line.size() && line[cursor] >= '0' && line[cursor] <= '9')
                    ++cursor;
                if (cursor == start || cursor == line.size() ||
                    (line[cursor] != '.' && line[cursor] != ')'))
                    return false;
                ++cursor;
            }
            if (cursor == line.size() || (line[cursor] != ' ' && line[cursor] != '\t'))
                return false;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
                ++cursor;
            return cursor < line.size();
        }

        /// <summary>
        /// Проверяет строку подчёркивания Setext-заголовка: === или ---.
        /// Сам заголовок берётся из текста предыдущего абзаца.
        /// </summary>
        /// <param name="line">Предполагаемая строка подчёркивания.</param>
        /// <returns>true, если строка состоит из допустимых символов подчёркивания.</returns>
        bool isLongSetextLine(std::string_view line)
        {
            const auto clean = trimView(line);
            if (clean.empty() || (clean.front() != '=' && clean.front() != '-'))
                return false;
            if (clean.front() == '-' && clean.size() < 3)
                return false;
            for (char ch : clean)
                if (ch != clean.front())
                    return false;
            return true;
        }

        /// <summary>
        /// Переводит одну латинскую букву A-Z в нижний регистр без учёта локали.
        /// </summary>
        /// <param name="ch">Проверяемый символ.</param>
        /// <returns>Строчная латинская буква или исходный символ.</returns>
        char asciiLower(char ch)
        {
            if (ch >= 'A' && ch <= 'Z')
                return static_cast<char>(ch + ('a' - 'A'));
            return ch;
        }

        /// <summary>
        /// Сравнивает часть текста с заданным шаблоном без учёта регистра латинских букв.
        /// Используется при поиске HTML-тегов и DOCTYPE.
        /// </summary>
        /// <param name="text">Строка, в которой выполняется сравнение.</param>
        /// <param name="offset">Позиция начала сравнения.</param>
        /// <param name="literal">Искомая последовательность символов.</param>
        /// <returns>true, если с указанной позиции находится совпадающий шаблон.</returns>
        bool asciiEqualsAt(std::string_view text, std::size_t offset, std::string_view literal)
        {
            // Не читаем за пределами строки.
            if (offset > text.size() || literal.size() > text.size() - offset)
                return false;
            for (std::size_t i = 0; i < literal.size(); ++i)
                if (asciiLower(text[offset + i]) != asciiLower(literal[i]))
                    return false;
            return true;
        }

        /// <summary>
        /// Проверяет символ после имени HTML-тега, чтобы не принять часть другого слова за тег.
        /// </summary>
        /// <param name="ch">Символ непосредственно после имени тега.</param>
        /// <returns>true для пробела, перевода строки, символов / или &gt;.</returns>
        bool isWordBoundary(char ch)
        {
            return ch == ' ' || ch == '\t' || ch == '\r' ||
                ch == '\n' || ch == '>' || ch == '/';
        }

        // Ищем теги и DOCTYPE без std::regex. Один проход по строке,
        // без глубокой рекурсии независимо от длины текста.
        /// <summary>
        /// Ищет признаки HTML-документа или HTML-фрагмента: DOCTYPE, теги html,
        /// head/body, заголовки и некоторые блочные теги.
        /// Это эвристика, а не полная проверка HTML: Markdown допускает часть HTML-синтаксиса.
        /// </summary>
        /// <param name="text">Строка разметки после исключения комментариев.</param>
        /// <param name="seenHead">Флаг обнаружения head; сохраняется между строками документа.</param>
        /// <param name="seenBody">Флаг обнаружения body; сохраняется между строками документа.</param>
        /// <returns>true при обнаружении признаков другого языка разметки — HTML.</returns>
        bool hasHtmlMarker(std::string_view text, bool& seenHead, bool& seenBody)
        {
            // Ищем открывающую скобку тега; обычные символы текста пропускаем.
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] != '<')
                    continue;
                std::size_t pos = i + 1;
                while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                    ++pos;
                if (asciiEqualsAt(text, pos, "!doctype"))
                {
                    pos += 8;
                    if (pos == text.size() || !std::isspace(static_cast<unsigned char>(text[pos])))
                        continue;
                    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                        ++pos;
                    if (asciiEqualsAt(text, pos, "html") &&
                        (pos + 4 == text.size() || !std::isalnum(static_cast<unsigned char>(text[pos + 4]))))
                        return true;
                    continue;
                }
                // Тег должен совпасть полностью (например, head не равно header).
                const auto isTag = [&](std::string_view tag)
                    {
                        return asciiEqualsAt(text, pos, tag) &&
                            pos + tag.size() < text.size() &&
                            isWordBoundary(text[pos + tag.size()]);
                    };
                if (isTag("html"))
                    return true;
                if (isTag("head"))
                    seenHead = true;
                if (isTag("body"))
                    seenBody = true;
                if (seenHead && seenBody)
                    return true;
            }
            const auto clean = trimView(text);
            if (clean.empty() || clean.front() != '<')
                return false;
            // Одиночные блочные HTML-теги также считаются признаком HTML-фрагмента.
            const std::string_view fragmentTags[] = {
                "div", "section", "article", "ul", "ol", "li", "table",
                "header", "main", "footer", "p"
            };
            for (const auto tag : fragmentTags)
                if (asciiEqualsAt(clean, 1, tag) && clean.size() > tag.size() + 1 &&
                    isWordBoundary(clean[tag.size() + 1]))
                    return true;
            return clean.size() >= 4 && asciiLower(clean[1]) == 'h' &&
                clean[2] >= '1' && clean[2] <= '6' && isWordBoundary(clean[3]);
        }

        /// <summary>
        /// Определяет характерные конструкции AsciiDoc: заголовки с =, атрибуты,
        /// таблицы, директивы, блоки и ссылки AsciiDoc.
        /// Это проверка характерных признаков, а не полноценный парсер AsciiDoc.
        /// </summary>
        /// <param name="line">Строка текста для проверки.</param>
        /// <returns>true, если обнаружена характерная конструкция AsciiDoc.</returns>
        bool isAsciiDocLine(std::string_view line)
        {
            std::size_t indent = 0;
            while (indent < line.size() && line[indent] == ' ' && indent < 4)
                ++indent;
            if (indent > 3 || indent == line.size())
                return false;
            const auto clean = line.substr(indent);
            // Заголовки AsciiDoc записываются с одним или несколькими символами =.
            if (clean.front() == '=')
            {
                std::size_t count = 0;
                while (count < clean.size() && clean[count] == '=') ++count;
                if (count >= 1 && count <= 6 && count + 1 < clean.size() &&
                    (clean[count] == ' ' || clean[count] == '\t'))
                {
                    const auto content = trimView(clean.substr(count));
                    if (!content.empty()) return true;
                }
            }
            // Атрибут AsciiDoc имеет вид :имя: значение.
            if (clean.front() == ':')
            {
                std::size_t pos = 1;
                while (pos < clean.size() &&
                    (std::isalnum(static_cast<unsigned char>(clean[pos])) ||
                        clean[pos] == '-' || clean[pos] == '_')) ++pos;
                if (pos > 1 && pos < clean.size() && clean[pos] == ':')
                    return true;
            }
            // Разделитель таблицы AsciiDoc.
            if (clean.substr(0, 4) == "|===")
                return true;
            if (clean.front() == '[')
            {
                const std::string_view markers[] = {
                    "source", "NOTE", "TIP", "WARNING", "IMPORTANT", "CAUTION"
                };
                for (const auto marker : markers)
                    if (clean.substr(1, marker.size()) == marker &&
                        clean.size() > marker.size() + 1 &&
                        (clean[marker.size() + 1] == ',' || clean[marker.size() + 1] == ']'))
                        return true;
            }
            // Специальные макросы включения, изображений и условных блоков.
            const std::string_view macros[] = {
                "include::", "image::", "audio::", "video::", "ifdef::", "ifndef::", "endif::"
            };
            for (const auto marker : macros)
                if (clean.substr(0, marker.size()) == marker)
                    return true;
            // Ссылки AsciiDoc с адресом и текстом в квадратных скобках.
            const std::string_view links[] = { "link:", "xref:" };
            for (const auto marker : links)
            {
                if (clean.substr(0, marker.size()) != marker)
                    continue;
                const auto bracket = clean.find('[', marker.size());
                if (bracket != std::string_view::npos && bracket > marker.size() &&
                    clean.find(']', bracket + 1) != std::string_view::npos &&
                    clean.substr(marker.size(), bracket - marker.size()).find_first_of(" \t") == std::string_view::npos)
                    return true;
            }
            return false;
        }

        /// <summary>
        /// Распознаёт горизонтальную линию Markdown из трёх и более -, _ или *.
        /// Между одинаковыми маркерами допустимы пробелы.
        /// </summary>
        /// <param name="line">Проверяемая строка.</param>
        /// <returns>true, если строка является горизонтальным разделителем.</returns>
        bool isHorizontalRule(std::string_view line)
        {
            const auto t = trimView(line);
            if (t.size() < 3)
                return false;
            const char marker = t.front();
            if (marker != '-' && marker != '_' && marker != '*')
                return false;
            std::size_t count = 0;
            for (char ch : t)
            {
                if (ch == marker)
                    ++count;
                else if (ch != ' ' && ch != '\t')
                    return false;
            }
            return count >= 3;
        }

        /// <summary>
        /// Построчный анализатор Markdown. Хранит состояние блоков кода, комментариев,
        /// абзацев и списков между вызовами feed(), подсчитывает найденные элементы
        /// и сохраняет ограниченное число примеров для экономии памяти.
        /// </summary>
        class MarkdownScanner
        {
        public:
            // false означает, что ошибка уже известна и дальнейшее чтение не нужно.
            /// <summary>
            /// Анализирует одну строку документа: проверяет чужую разметку,
            /// обрабатывает блоки кода и комментарии, выделяет заголовки, списки и абзацы.
            /// </summary>
            /// <param name="original">Очередная строка Markdown без символа перевода строки.</param>
            /// <returns>true, если анализ можно продолжать; false, если обнаружена ошибка.</returns>
            bool feed(std::string_view original)
            {
                // Защита от гигантской строки без перевода строки, даже в очень большом файле.
                if (original.size() > kMaxLineBytes)
                {
                    analysis_.errors.emplace_back("line_too_long");
                    return false;
                }
                if (!original.empty() && original.back() == '\r')
                    original.remove_suffix(1);
                // В начале файла удаляем UTF-8 BOM, если он присутствует.
                if (!firstLine_)
                {
                    firstLine_ = true;
                    if (original.size() >= 3 &&
                        static_cast<unsigned char>(original[0]) == 0xEF &&
                        static_cast<unsigned char>(original[1]) == 0xBB &&
                        static_cast<unsigned char>(original[2]) == 0xBF)
                        original.remove_prefix(3);
                }

                if (!trimView(original).empty())
                    sawText_ = true;

                char marker = 0;
                std::size_t length = 0;
                std::string_view suffix;
                const bool fenceLine = parseFence(original, marker, length, suffix);

                // Внутри ограждённого блока кода HTML и AsciiDoc не считаются ошибками.
                if (insideFence_)
                {
                    if (fenceLine && marker == fenceMarker_ && length >= fenceLength_ &&
                        trimView(suffix).empty())
                        insideFence_ = false;
                    flushParagraph();
                    flushList();
                    return true;
                }

                // Открываем новый блок кода и запоминаем его тип и длину маркера.
                if (fenceLine && (marker != '`' || suffix.find('`') == std::string_view::npos))
                {
                    insideFence_ = true;
                    fenceMarker_ = marker;
                    fenceLength_ = length;
                    flushParagraph();
                    flushList();
                    return true;
                }

                // Комментарии сохраняем как часть синтаксиса Markdown, но их содержимое
                // не проверяем на признаки чужого языка.
                std::string uncommentedStorage;
                std::string_view uncommented = original;
                if (insideComment_ || original.find("<!--") != std::string_view::npos)
                {
                    uncommentedStorage = withoutComments(original, insideComment_);
                    uncommented = uncommentedStorage;
                }
                const std::string_view clean = trimView(uncommented);
                // Пустая строка отделяет абзацы и списки друг от друга.
                if (clean.empty())
                {
                    flushParagraph();
                    flushList();
                    return true;
                }

                // Распознаём чужой язык разметки без std::regex даже в длинных строках.
                if (clean.find('<') != std::string_view::npos &&
                    hasHtmlMarker(uncommented, seenHead_, seenBody_))
                {
                    analysis_.errors.emplace_back("html_detected");
                    return false;
                }
                // Чужая разметка AsciiDoc — ошибка для варианта 2 (Markdown).
                if (isAsciiDocLine(uncommented))
                {
                    analysis_.errors.emplace_back("asciidoc_detected");
                    return false;
                }

                const std::size_t leadingSpaces = uncommented.find_first_not_of(" \t");
                const char first = leadingSpaces == std::string_view::npos ? '\0' : uncommented[leadingSpaces];
                const bool smallIndent = leadingSpaces <= 3;
                // Заголовок вида # Заголовок, ## Заголовок и т. д.
                if (smallIndent && first == '#')
                {
                    std::string_view heading;
                    if (parseLongAtxHeading(uncommented, heading))
                    {
                        flushParagraph();
                        flushList();
                        const auto closing = heading.find_last_not_of('#');
                        if (closing != std::string_view::npos && closing + 1 < heading.size() &&
                            closing > 0 && (heading[closing] == ' ' || heading[closing] == '\t'))
                            heading = trimView(heading.substr(0, closing + 1));
                        if (!heading.empty())
                            addHeading(heading);
                        return true;
                    }
                }

                // Setext: предыдущая строка абзаца + подчёркивание === или --- => заголовок.
                if (smallIndent && (first == '=' || first == '-') && paragraphOpen_ &&
                    isLongSetextLine(uncommented))
                {
                    flushList();
                    // Учитываем все заголовки, даже когда лимит отображаемых примеров достигнут.
                    ++analysis_.headingCount;
                    if (analysis_.headings.size() < kMaxExamplesPerKind)
                        analysis_.headings.push_back(paragraph_);
                    paragraphOpen_ = false;
                    paragraph_.clear();
                    return true;
                }

                // Горизонтальный разделитель Markdown не является ни абзацем, ни списком.
                if (smallIndent && isHorizontalRule(uncommented))
                {
                    flushParagraph();
                    flushList();
                    return true;
                }

                // Если строка начинается с маркера списка, пытаемся распознать элемент.
                if (first == '-' || first == '*' || first == '+' ||
                    (first >= '0' && first <= '9'))
                {
                    if (isLongListLine(uncommented))
                    {
                        flushParagraph();
                        listOpen_ = true;
                        if (analysis_.lists.size() < kMaxExamplesPerKind)
                        {
                            if (!list_.empty())
                                appendPreview(list_, "\n", kMaxListPreview);
                            appendPreview(list_, clean, kMaxListPreview);
                        }
                        return true;
                    }
                }

                flushList();
                // Всё остальное рассматривается как текст абзаца.
                paragraphOpen_ = true;
                if (analysis_.paragraphs.size() < kMaxExamplesPerKind)
                {
                    if (!paragraph_.empty() && paragraph_.back() != ' ')
                        paragraph_ += ' ';
                    appendNormalizedPreview(paragraph_, clean, kMaxExampleText);
                    while (!paragraph_.empty() && paragraph_.back() == ' ')
                        paragraph_.pop_back();
                }
                return true;
            }

            /// <summary>
            /// Завершает разбор текста, сохраняет открытые абзац и список и проверяет,
            /// не остались ли незакрытые блоки кода или HTML-комментарии.
            /// </summary>
            /// <returns>Итог анализа с признаком valid, статистикой и ошибками.</returns>
            MarkupAnalysis finish()
            {
                flushParagraph();
                flushList();
                // Итоговые проверки после чтения всех строк.
                if (!sawText_)
                    analysis_.errors.emplace_back("empty_document");
                if (insideFence_)
                    analysis_.errors.emplace_back("unclosed_fence");
                if (insideComment_)
                    analysis_.errors.emplace_back("unclosed_html_comment");
                analysis_.valid = analysis_.errors.empty();
                return std::move(analysis_);
            }

            /// <summary>
            /// Досрочно завершает анализ после найденной ошибки, сохраняя её в результате.
            /// </summary>
            /// <returns>Результат анализа с valid = false и информацией об ошибке.</returns>
            MarkupAnalysis abort()
            {
                analysis_.valid = false;
                return std::move(analysis_);
            }

        private:
            /// <summary>
            /// Увеличивает счётчик заголовков и при наличии места сохраняет пример текста.
            /// </summary>
            /// <param name="heading">Текст найденного заголовка.</param>
            void addHeading(std::string_view heading)
            {
                ++analysis_.headingCount;
                if (analysis_.headings.size() < kMaxExamplesPerKind)
                {
                    std::string saved;
                    appendPreview(saved, heading, kMaxExampleText);
                    analysis_.headings.push_back(std::move(saved));
                }
            }

            /// <summary>
            /// Завершает текущий абзац: увеличивает счётчик, сохраняет пример и сбрасывает состояние.
            /// </summary>
            void flushParagraph()
            {
                // Один абзац может занимать несколько строк; считаем его только при завершении.
                if (paragraphOpen_)
                {
                    ++analysis_.paragraphCount;
                    if (analysis_.paragraphs.size() < kMaxExamplesPerKind)
                        analysis_.paragraphs.push_back(std::move(paragraph_));
                }
                paragraphOpen_ = false;
                paragraph_.clear();
            }

            /// <summary>
            /// Завершает текущий список: увеличивает счётчик, сохраняет пример и сбрасывает состояние.
            /// </summary>
            void flushList()
            {
                // Несколько элементов подряд считаются одним списком.
                if (listOpen_)
                {
                    ++analysis_.listCount;
                    if (analysis_.lists.size() < kMaxExamplesPerKind)
                        analysis_.lists.push_back(std::move(list_));
                }
                listOpen_ = false;
                list_.clear();
            }

            MarkupAnalysis analysis_;
            std::string paragraph_;
            std::string list_;
            bool firstLine_ = false;
            bool sawText_ = false;
            bool insideFence_ = false;
            char fenceMarker_ = 0;
            std::size_t fenceLength_ = 0;
            bool insideComment_ = false;
            bool seenHead_ = false;
            bool seenBody_ = false;
            bool paragraphOpen_ = false;
            bool listOpen_ = false;
        };
    }

    /// <summary>
    /// Анализирует Markdown-текст, уже загруженный в оперативную память.
    /// Передаёт строки объекту MarkdownScanner и возвращает сводный результат.
    /// </summary>
    /// <param name="text">Полный текст Markdown-документа.</param>
    /// <returns>Результат проверки и поиска элементов разметки.</returns>
    MarkupAnalysis analyzeMarkdown(const std::string& text)
    {
        MarkdownScanner scanner;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            const auto end = text.find('\n', pos);
            const std::size_t size = end == std::string::npos ? text.size() - pos : end - pos;
            if (!scanner.feed(std::string_view(text.data() + pos, size)))
                return scanner.abort();
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        return scanner.finish();
    }

    /// <summary>
    /// Открывает файл, проверяет допустимость его расширения и анализирует Markdown
    /// потоково блоками по 256 КиБ, не загружая весь файл в память.
    /// Отдельно отслеживает бинарные данные и слишком длинные строки.
    /// </summary>
    /// <param name="filename">Путь к файлу для анализа.</param>
    /// <param name="result">Выходной параметр: найденные элементы и ошибки разметки.</param>
    /// <param name="errorKey">Выходной параметр: ключ ошибки открытия или чтения файла.</param>
    /// <param name="progress">Необязательный обратный вызов с прочитанными и полными байтами.</param>
    /// <returns>
    /// true, если файл удалось открыть и проверить (даже если разметка неверна);
    /// false, если возникла ошибка открытия или чтения. Корректность проверяют по result.valid.
    /// </returns>
    bool analyzeMarkdownFile(
        const std::filesystem::path& filename,
        MarkupAnalysis& result,
        std::string& errorKey,
        const std::function<void(std::uintmax_t, std::uintmax_t)>& progress)
    {
        result = {};
        errorKey.clear();

        // Сначала убеждаемся, что файл существует и доступен: отсутствие
        // файла с расширением .html не должно выглядеть как ошибка разметки.
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            errorKey = "file_open_error";
            return false;
        }

        // Расширение помогает сразу отличить HTML/AsciiDoc, даже до чтения содержимого.
        const std::string extension = lowerAscii(filename.extension().u8string());
        if (extension == ".html" || extension == ".htm" || extension == ".xhtml")
        {
            result.errors.emplace_back("html_detected");
            return true;
        }
        if (extension == ".adoc" || extension == ".asciidoc" ||
            extension == ".asc" || extension == ".ad")
        {
            result.errors.emplace_back("asciidoc_detected");
            return true;
        }
        if (!extension.empty() && extension != ".md" && extension != ".markdown" &&
            extension != ".mdown" && extension != ".mkd" && extension != ".txt")
        {
            result.errors.emplace_back("unsupported_extension");
            return true;
        }

        MarkdownScanner scanner;
        // Размер нужен только для процента прогресса; ошибка определения размера не мешает чтению.
        std::error_code sizeError;
        const auto totalBytes = std::filesystem::file_size(filename, sizeError);
        std::uintmax_t processedBytes = 0;
        std::uintmax_t nextProgress = 64ULL * 1024 * 1024;
        // Буфер размещён в куче: под Windows размер стека по умолчанию
        // обычно 1 МиБ, а чтение больших файлов не должно занимать стек.
        // Читаем файл небольшими частями; буфер создаётся в динамической памяти.
        std::vector<char> block(kReadBlockSize);
        std::string line;
        line.reserve(2048);
        while (true)
        {
            // Очередное чтение 256 КиБ. gcount() сообщает реально считанное число байтов.
            file.read(block.data(), static_cast<std::streamsize>(block.size()));
            const std::streamsize bytes = file.gcount();
            processedBytes += static_cast<std::uintmax_t>(bytes);
            // Нулевой байт — характерный признак бинарного, а не текстового файла.
            if (bytes > 0 && std::memchr(block.data(), '\0', static_cast<std::size_t>(bytes)) != nullptr)
            {
                result.errors.emplace_back("binary_document");
                return true;
            }
            std::size_t pos = 0;
            while (pos < static_cast<std::size_t>(bytes))
            {
                // Ищем перевод строки. Неполная строка переносится в следующий блок чтения.
                const char* begin = block.data() + pos;
                const char* newline = static_cast<const char*>(
                    std::memchr(begin, '\n', static_cast<std::size_t>(bytes) - pos));
                const std::size_t amount = newline != nullptr ?
                    static_cast<std::size_t>(newline - begin) :
                    static_cast<std::size_t>(bytes) - pos;
                // Проверяется суммарная длина строки, даже если она пересекает несколько блоков.
                if (line.size() + amount > kMaxLineBytes)
                {
                    result.errors.emplace_back("line_too_long");
                    return true;
                }
                line.append(begin, amount);
                if (newline != nullptr)
                {
                    // Анализируем только готовую строку; при ошибке прекращаем дальнейшее чтение.
                    if (!scanner.feed(line))
                    {
                        result = scanner.abort();
                        return true;
                    }
                    line.clear();
                    pos += amount + 1;
                }
                else
                    pos += amount;
            }
            // Обновление прогресса примерно через каждые 64 МиБ, не на каждом блоке.
            if (progress && !sizeError && processedBytes >= nextProgress)
            {
                progress(processedBytes, totalBytes);
                nextProgress = processedBytes + 64ULL * 1024 * 1024;
            }
            // EOF — штатное завершение, остальные ошибки чтения возвращаем отдельно.
            if (!file)
            {
                if (!file.eof())
                {
                    errorKey = "file_read_error";
                    return false;
                }
                break;
            }
        }
        // Анализируем последнюю строку, у которой в конце файла нет символа \n.
        if (!line.empty() && !scanner.feed(line))
        {
            result = scanner.abort();
            return true;
        }
        // Закрываем накопленные элементы и получаем итоговый статус корректности.
        result = scanner.finish();
        if (progress && !sizeError && totalBytes != 0)
            progress(processedBytes, totalBytes);
        return true;
    }
}
