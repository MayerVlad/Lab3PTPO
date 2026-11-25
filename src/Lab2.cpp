#include "Lab2.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lab2
{
    namespace
    {
        /// <summary>
        /// Удаляет пробельные символы по краям строки.
        /// </summary>
        std::string trim(const std::string& value)
        {
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }

            const std::size_t last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        /// <summary>
        /// Декодирует UTF-8 в кодовые точки для корректной обработки кириллицы в ФИО.
        /// </summary>
        std::vector<std::uint32_t> decodeUtf8(const std::string& text)
        {
            std::vector<std::uint32_t> codepoints;

            for (std::size_t index = 0; index < text.size();)
            {
                const unsigned char first = static_cast<unsigned char>(text[index]);

                if (first < 0x80)
                {
                    codepoints.push_back(first);
                    ++index;
                }
                else if ((first & 0xE0) == 0xC0 && index + 1 < text.size())
                {
                    const unsigned char second = static_cast<unsigned char>(text[index + 1]);
                    if ((second & 0xC0) != 0x80)
                    {
                        ++index;
                        continue;
                    }

                    codepoints.push_back(((first & 0x1F) << 6) | (second & 0x3F));
                    index += 2;
                }
                else if ((first & 0xF0) == 0xE0 && index + 2 < text.size())
                {
                    const unsigned char second = static_cast<unsigned char>(text[index + 1]);
                    const unsigned char third = static_cast<unsigned char>(text[index + 2]);
                    if ((second & 0xC0) != 0x80 || (third & 0xC0) != 0x80)
                    {
                        ++index;
                        continue;
                    }

                    codepoints.push_back(((first & 0x0F) << 12) |
                                         ((second & 0x3F) << 6) |
                                         (third & 0x3F));
                    index += 3;
                }
                else if ((first & 0xF8) == 0xF0 && index + 3 < text.size())
                {
                    const unsigned char second = static_cast<unsigned char>(text[index + 1]);
                    const unsigned char third = static_cast<unsigned char>(text[index + 2]);
                    const unsigned char fourth = static_cast<unsigned char>(text[index + 3]);
                    if ((second & 0xC0) != 0x80 ||
                        (third & 0xC0) != 0x80 ||
                        (fourth & 0xC0) != 0x80)
                    {
                        ++index;
                        continue;
                    }

                    codepoints.push_back(((first & 0x07) << 18) |
                                         ((second & 0x3F) << 12) |
                                         ((third & 0x3F) << 6) |
                                         (fourth & 0x3F));
                    index += 4;
                }
                else
                {
                    // Некорректный байт пропускается, чтобы он не попал в нормализованное имя.
                    ++index;
                }
            }

            return codepoints;
        }

        /// <summary>
        /// Добавляет кодовую точку Unicode к строке UTF-8.
        /// </summary>
        void appendUtf8(std::string& output, std::uint32_t codepoint)
        {
            if (codepoint <= 0x7F)
            {
                output.push_back(static_cast<char>(codepoint));
            }
            else if (codepoint <= 0x7FF)
            {
                output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else if (codepoint <= 0xFFFF)
            {
                output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
            else
            {
                output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }

        /// <summary>
        /// Проверяет, относится ли кодовая точка к латинской или русской букве.
        /// </summary>
        bool isLetter(std::uint32_t codepoint)
        {
            return (codepoint >= 'A' && codepoint <= 'Z') ||
                   (codepoint >= 'a' && codepoint <= 'z') ||
                   (codepoint >= 0x0410 && codepoint <= 0x044F) ||
                   codepoint == 0x0401 || codepoint == 0x0451;
        }

        /// <summary>
        /// Переводит латинскую или русскую букву в нижний регистр.
        /// </summary>
        std::uint32_t lowerCodepoint(std::uint32_t codepoint)
        {
            if (codepoint >= 'A' && codepoint <= 'Z')
            {
                return codepoint + ('a' - 'A');
            }
            if (codepoint >= 0x0410 && codepoint <= 0x042F)
            {
                return codepoint + 0x20;
            }
            if (codepoint == 0x0401)
            {
                return 0x0451;
            }
            return codepoint;
        }

        /// <summary>
        /// Переводит латинскую или русскую букву в верхний регистр.
        /// </summary>
        std::uint32_t upperCodepoint(std::uint32_t codepoint)
        {
            if (codepoint >= 'a' && codepoint <= 'z')
            {
                return codepoint - ('a' - 'A');
            }
            if (codepoint >= 0x0430 && codepoint <= 0x044F)
            {
                return codepoint - 0x20;
            }
            if (codepoint == 0x0451)
            {
                return 0x0401;
            }
            return codepoint;
        }

        /// <summary>
        /// Переводит русские и латинские буквы UTF-8 строки в нижний регистр.
        /// </summary>
        std::string toLowerUtf8(const std::string& input)
        {
            std::string result;
            for (std::uint32_t codepoint : decodeUtf8(input))
            {
                appendUtf8(result, lowerCodepoint(codepoint));
            }
            return result;
        }

        /// <summary>
        /// Приводит ФИО к виду с заглавной первой буквой и строчными остальными.
        /// </summary>
        std::string normalizeName(const std::string& input)
        {
            const std::vector<std::uint32_t> codepoints = decodeUtf8(trim(input));
            std::string result;
            bool capitalizeNext = true;

            for (std::uint32_t codepoint : codepoints)
            {
                if (isLetter(codepoint))
                {
                    appendUtf8(result, capitalizeNext
                        ? upperCodepoint(lowerCodepoint(codepoint))
                        : lowerCodepoint(codepoint));
                    capitalizeNext = false;
                }
                else if (codepoint == '-' || codepoint == '\'' || codepoint == 0x2019)
                {
                    if (!result.empty() && result.back() != '-' && result.back() != '\'')
                    {
                        result.push_back(codepoint == 0x2019 ? '\'' : static_cast<char>(codepoint));
                        capitalizeNext = true;
                    }
                }

                // Цифры, пробелы и лишняя пунктуация удаляются для исправления простых опечаток.
            }

            while (!result.empty() && (result.back() == '-' || result.back() == '\''))
            {
                result.pop_back();
            }

            return result;
        }

        /// <summary>
        /// Приводит ASCII-символы адреса электронной почты к нижнему регистру.
        /// </summary>
        std::string normalizeEmail(const std::string& input)
        {
            std::string result = trim(input);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return result;
        }

        /// <summary>
        /// Оставляет в строке только цифры ASCII.
        /// </summary>
        std::string digitsOnly(const std::string& input)
        {
            std::string result;
            for (unsigned char character : input)
            {
                if (character >= '0' && character <= '9')
                {
                    result.push_back(static_cast<char>(character));
                }
            }
            return result;
        }

        /// <summary>
        /// Проверяет год на високосность.
        /// </summary>
        bool isLeapYear(int year)
        {
            return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
        }

        /// <summary>
        /// Проверяет существование календарной даты в допустимом диапазоне лет.
        /// </summary>
        bool validCalendarDate(int day, int month, int year)
        {
            if (year < 1900 || month < 1 || month > 12 || day < 1)
            {
                return false;
            }

            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &now);
#else
            localtime_r(&now, &localTime);
#endif
            const int currentYear = localTime.tm_year + 1900;
            const int currentMonth = localTime.tm_mon + 1;
            const int currentDay = localTime.tm_mday;

            // Дата рождения не может находиться в будущем, даже в пределах текущего года.
            if (year > currentYear ||
                (year == currentYear && month > currentMonth) ||
                (year == currentYear && month == currentMonth && day > currentDay))
            {
                return false;
            }

            static const std::array<int, 12> daysInMonth{
                31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
            };

            int maximumDay = daysInMonth[static_cast<std::size_t>(month - 1)];
            if (month == 2 && isLeapYear(year))
            {
                maximumDay = 29;
            }

            return day <= maximumDay;
        }

        /// <summary>
        /// Разбирает несколько распространённых форматов даты и приводит их к ДД.ММ.ГГГГ.
        /// </summary>
        bool normalizeDate(const std::string& input, std::string& normalized)
        {
            const std::string value = trim(input);
            static const std::regex separated(
                R"(^\s*(\d{1,4})[./\-\s](\d{1,2})[./\-\s](\d{1,4})\s*$)");
            static const std::regex compact(R"(^\s*(\d{8})\s*$)");

            std::smatch match;
            int day = 0;
            int month = 0;
            int year = 0;

            if (std::regex_match(value, match, separated))
            {
                const std::string first = match[1].str();
                const int second = std::stoi(match[2].str());
                const std::string third = match[3].str();

                if (first.size() == 4)
                {
                    year = std::stoi(first);
                    month = second;
                    day = std::stoi(third);
                }
                else if (third.size() == 4)
                {
                    day = std::stoi(first);
                    month = second;
                    year = std::stoi(third);
                }
                else
                {
                    return false;
                }
            }
            else if (std::regex_match(value, match, compact))
            {
                const std::string digits = match[1].str();

                if (digits.rfind("19", 0) == 0 || digits.rfind("20", 0) == 0)
                {
                    year = std::stoi(digits.substr(0, 4));
                    month = std::stoi(digits.substr(4, 2));
                    day = std::stoi(digits.substr(6, 2));
                }
                else
                {
                    day = std::stoi(digits.substr(0, 2));
                    month = std::stoi(digits.substr(2, 2));
                    year = std::stoi(digits.substr(4, 4));
                }
            }
            else
            {
                return false;
            }

            if (!validCalendarDate(day, month, year))
            {
                return false;
            }

            std::ostringstream output;
            output << std::setfill('0') << std::setw(2) << day << '.'
                   << std::setw(2) << month << '.' << std::setw(4) << year;
            normalized = output.str();
            return true;
        }

        /// <summary>
        /// Нормализует российский телефон к виду +7XXXXXXXXXX.
        /// </summary>
        bool normalizePhone(const std::string& input, std::string& normalized)
        {
            std::string digits = digitsOnly(input);

            if (digits.size() == 10)
            {
                digits.insert(digits.begin(), '7');
            }
            else if (digits.size() == 11 && digits.front() == '8')
            {
                digits.front() = '7';
            }
            else if (digits.size() != 11 || digits.front() != '7')
            {
                return false;
            }

            // Проверяем допустимый первый знак национальной части российского номера.
            if (digits.size() != 11 || digits.front() != '7' ||
                digits[1] < '3' || digits[1] > '9')
            {
                return false;
            }

            normalized = '+' + digits;
            return true;
        }

        /// <summary>
        /// Проверяет синтаксис электронного адреса и отсутствие двойных точек в локальной части.
        /// </summary>
        bool validEmail(const std::string& email)
        {
            static const std::regex pattern(
                R"(^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$)");

            if (!std::regex_match(email, pattern))
            {
                return false;
            }

            const std::size_t atPosition = email.find('@');
            const std::string localPart = email.substr(0, atPosition);
            return !localPart.empty() &&
                   localPart.front() != '.' &&
                   localPart.back() != '.' &&
                   localPart.find("..") == std::string::npos;
        }

        /// <summary>
        /// Возвращает случайный элемент непустого словаря.
        /// </summary>
        std::string randomChoice(const std::vector<std::string>& values, std::mt19937& engine)
        {
            std::uniform_int_distribution<std::size_t> distribution(0, values.size() - 1);
            return values[distribution(engine)];
        }

        /// <summary>
        /// Генерирует строку заданной длины из случайных цифр.
        /// </summary>
        std::string randomDigits(int count, std::mt19937& engine)
        {
            std::uniform_int_distribution<int> digit(0, 9);
            std::string result;
            for (int index = 0; index < count; ++index)
            {
                result.push_back(static_cast<char>('0' + digit(engine)));
            }
            return result;
        }

        /// <summary>
        /// Вставляет цифру в имя для проверки способности нормализатора исправлять ввод.
        /// </summary>
        std::string insertDigitInUtf8Name(const std::string& input)
        {
            const std::vector<std::uint32_t> codepoints = decodeUtf8(input);
            std::string result;
            const std::size_t insertAfter = codepoints.size() / 2;

            for (std::size_t index = 0; index < codepoints.size(); ++index)
            {
                appendUtf8(result, codepoints[index]);
                if (index + 1 == insertAfter)
                {
                    result.push_back('1');
                }
            }

            if (insertAfter == 0)
            {
                result.insert(result.begin(), '1');
            }

            return result;
        }
    }

    /// <summary>
    /// Загружает переводы интерфейса из JSON-файла.
    /// </summary>
    Json loadTranslations(const std::filesystem::path& filename)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Cannot open translations file");
        }

        Json translations;
        file >> translations;

        // Проверяем наличие обязательных разделов, чтобы ошибка конфигурации была обнаружена сразу.
        translations.at("languages");
        translations.at("ru");
        translations.at("en");
        translations.at("fr");
        return translations;
    }

    /// <summary>
    /// Возвращает локализованную строку интерфейса по языку и ключу.
    /// </summary>
    std::string tr(const Json& translations, const std::string& language, const std::string& key)
    {
        return translations.at(language).at(key).get<std::string>();
    }

    /// <summary>
    /// Предлагает выбрать язык интерфейса и проверяет введённый номер.
    /// </summary>
    std::string chooseLanguage(const Json& translations)
    {
        const std::array<std::string, 3> languageCodes{ "ru", "en", "fr" };

        while (true)
        {
            std::cout << translations.at("language_prompt").get<std::string>() << '\n';
            for (std::size_t index = 0; index < languageCodes.size(); ++index)
            {
                const std::string& code = languageCodes[index];
                std::cout << index + 1 << " - "
                          << translations.at("languages").at(code).get<std::string>() << '\n';
            }
            std::cout << "> ";

            std::string line;
            if (!std::getline(std::cin, line))
            {
                throw std::runtime_error("Input ended before language selection");
            }

            try
            {
                const std::string cleaned = trim(line);
                std::size_t parsed = 0;
                const int choice = std::stoi(cleaned, &parsed);
                if (parsed == cleaned.size() && choice >= 1 &&
                    choice <= static_cast<int>(languageCodes.size()))
                {
                    return languageCodes[static_cast<std::size_t>(choice - 1)];
                }
            }
            catch (const std::exception&)
            {
                // Ошибка разбора обрабатывается ниже единым локализованным сообщением.
            }

            std::cout << translations.at("language_invalid").get<std::string>() << '\n';
        }
    }

    /// <summary>
    /// Создаёт демонстрационный Markdown-документ на выбранном языке интерфейса.
    /// </summary>
    std::string sampleMarkdown(const Json& translations, const std::string& language)
    {
        // Встроенный пример повторяет структуру examples/example.md, включая два отдельных списка.
        std::ostringstream sample;
        sample << "# " << tr(translations, language, "sample_heading") << "\n\n"
               << tr(translations, language, "sample_paragraph") << "\n\n"
               << "## " << tr(translations, language, "sample_list_heading") << "\n\n"
               << "- " << tr(translations, language, "sample_item_one") << "\n"
               << "- " << tr(translations, language, "sample_item_two") << "\n"
               << "- " << tr(translations, language, "sample_item_three") << "\n\n"
               << "1. " << tr(translations, language, "sample_ordered_item_one") << "\n"
               << "2. " << tr(translations, language, "sample_ordered_item_two") << '\n';
        return sample.str();
    }

    /// <summary>
    /// Считывает шесть полей анкеты с локализованными приглашениями.
    /// </summary>
    UserData readUserData(const Json& translations, const std::string& language)
    {
        const auto readField = [&](const std::string& key)
        {
            std::cout << tr(translations, language, key);
            std::string value;
            if (!std::getline(std::cin, value))
            {
                throw std::runtime_error("Input ended while reading questionnaire");
            }
            return value;
        };

        UserData data;
        data.surname = readField("prompt_surname");
        data.firstName = readField("prompt_first_name");
        data.patronymic = readField("prompt_patronymic");
        data.birthDate = readField("prompt_birth_date");
        data.phone = readField("prompt_phone");
        data.email = readField("prompt_email");
        return data;
    }

    /// <summary>
    /// Нормализует и проверяет поля анкеты; возвращает ключи ошибок для локализации.
    /// </summary>
    bool normalizeAndValidateUserData(UserData& data, std::vector<std::string>& errorKeys)
    {
        errorKeys.clear();

        data.surname = normalizeName(data.surname);
        data.firstName = normalizeName(data.firstName);
        data.patronymic = normalizeName(data.patronymic);

        if (data.surname.empty())
        {
            errorKeys.emplace_back("error_surname");
        }
        if (data.firstName.empty())
        {
            errorKeys.emplace_back("error_first_name");
        }
        if (data.patronymic.empty())
        {
            errorKeys.emplace_back("error_patronymic");
        }

        std::string normalizedDate;
        if (normalizeDate(data.birthDate, normalizedDate))
        {
            data.birthDate = normalizedDate;
        }
        else
        {
            errorKeys.emplace_back("error_birth_date");
        }

        std::string normalizedPhone;
        if (normalizePhone(data.phone, normalizedPhone))
        {
            data.phone = normalizedPhone;
        }
        else
        {
            errorKeys.emplace_back("error_phone");
        }

        data.email = normalizeEmail(data.email);
        if (!validEmail(data.email))
        {
            errorKeys.emplace_back("error_email");
        }

        return errorKeys.empty();
    }

    /// <summary>
    /// Создаёт случайную тестовую анкету с разными вариантами написания полей.
    /// </summary>
    UserData generateRandomUserData()
    {
        // Локальные словари распространённых русских фамилий, имён и отчеств.
        static const std::vector<std::string> surnames{
            "Иванов", "Петров", "Сидоров", "Смирнов", "Кузнецов", "Попов", "Васильев", "Соколов",
            "Михайлов", "Новиков", "Фёдоров", "Морозов", "Волков", "Алексеев", "Лебедев", "Семёнов",
            "Егоров", "Павлов", "Козлов", "Степанов", "Николаев", "Орлов", "Андреев", "Макаров",
            "Захаров", "Зайцев", "Соловьёв", "Борисов", "Яковлев", "Григорьев", "Виноградов",
            "Богданов", "Воробьёв", "Фролов", "Киселёв", "Медведев", "Крылов", "Белов", "Гусев"
        };
        static const std::vector<std::string> firstNames{
            "Александр", "Алексей", "Андрей", "Антон", "Артём", "Борис", "Вадим", "Валерий",
            "Василий", "Виктор", "Виталий", "Владимир", "Владислав", "Геннадий", "Георгий", "Григорий",
            "Даниил", "Денис", "Дмитрий", "Евгений", "Иван", "Игорь", "Илья", "Кирилл", "Константин",
            "Леонид", "Максим", "Михаил", "Никита", "Николай", "Олег", "Павел", "Роман", "Сергей",
            "Станислав", "Степан", "Тимофей", "Фёдор", "Юрий", "Ярослав", "Аркадий", "Владлен"
        };
        static const std::vector<std::string> patronymics{
            "Александрович", "Алексеевич", "Андреевич", "Антонович", "Артёмович", "Борисович", "Вадимович",
            "Валерьевич", "Васильевич", "Викторович", "Витальевич", "Владимирович", "Владиславович",
            "Геннадьевич", "Георгиевич", "Григорьевич", "Даниилович", "Денисович", "Дмитриевич",
            "Евгеньевич", "Иванович", "Игоревич", "Ильич", "Кириллович", "Константинович", "Леонидович",
            "Максимович", "Михайлович", "Никитич", "Николаевич", "Олегович", "Павлович", "Романович",
            "Сергеевич", "Станиславович", "Степанович", "Тимофеевич", "Фёдорович", "Юрьевич", "Ярославович"
        };

        static std::mt19937 engine{ std::random_device{}() };
        UserData data;
        data.surname = randomChoice(surnames, engine);
        data.firstName = randomChoice(firstNames, engine);
        data.patronymic = randomChoice(patronymics, engine);

        // Меняем регистр и иногда добавляем цифру, чтобы проверить нормализацию ФИО.
        if (std::uniform_int_distribution<int>(0, 1)(engine) == 1)
        {
            data.firstName = toLowerUtf8(data.firstName);
        }
        if (std::uniform_int_distribution<int>(0, 3)(engine) == 0)
        {
            data.patronymic = insertDigitInUtf8Name(data.patronymic);
        }

        std::uniform_int_distribution<int> yearDistribution(1960, 2006);
        const int year = yearDistribution(engine);
        std::uniform_int_distribution<int> monthDistribution(1, 12);
        const int month = monthDistribution(engine);
        const int maximumDay = month == 2
            ? (isLeapYear(year) ? 29 : 28)
            : ((month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31);
        std::uniform_int_distribution<int> dayDistribution(1, maximumDay);
        const int day = dayDistribution(engine);

        std::ostringstream date;
        switch (std::uniform_int_distribution<int>(0, 2)(engine))
        {
        case 0:
            date << std::setfill('0') << std::setw(4) << year << '-'
                 << std::setw(2) << month << '-' << std::setw(2) << day;
            break;
        case 1:
            date << std::setfill('0') << std::setw(2) << day << '.'
                 << std::setw(2) << month << '.' << std::setw(4) << year;
            break;
        default:
            date << std::setfill('0') << std::setw(2) << day << '/'
                 << std::setw(2) << month << '/' << std::setw(4) << year;
            break;
        }
        data.birthDate = date.str();

        const std::string tenDigits = "9" + randomDigits(9, engine);
        switch (std::uniform_int_distribution<int>(0, 3)(engine))
        {
        case 0:
            data.phone = "+7" + tenDigits;
            break;
        case 1:
            data.phone = "+7 (" + tenDigits.substr(0, 3) + ") " +
                tenDigits.substr(3, 3) + "-" + tenDigits.substr(6, 2) + "-" +
                tenDigits.substr(8, 2);
            break;
        case 2:
            data.phone = "8 " + tenDigits.substr(0, 3) + " " +
                tenDigits.substr(3, 3) + " " + tenDigits.substr(6, 2) + " " +
                tenDigits.substr(8, 2);
            break;
        default:
            data.phone = tenDigits;
            break;
        }

        std::string email = "user" + randomDigits(4, engine) + "@example.com";
        if (std::uniform_int_distribution<int>(0, 1)(engine) == 1)
        {
            for (char& character : email)
            {
                if (std::isalpha(static_cast<unsigned char>(character)) != 0 &&
                    std::uniform_int_distribution<int>(0, 1)(engine) == 1)
                {
                    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                }
            }
        }
        data.email = email;

        return data;
    }

    /// <summary>
    /// Выводит поля анкеты с подписями на выбранном языке.
    /// </summary>
    void printUserData(
        const UserData& data,
        const Json& translations,
        const std::string& language)
    {
        std::cout << tr(translations, language, "label_surname") << data.surname << ' '
                  << data.firstName << ' ' << data.patronymic << '\n'
                  << tr(translations, language, "label_birth_date") << data.birthDate << '\n'
                  << tr(translations, language, "label_phone") << data.phone << '\n'
                  << tr(translations, language, "label_email") << data.email << '\n';
    }
}
