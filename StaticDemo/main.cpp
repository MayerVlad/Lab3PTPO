#include "Lab2.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    /// <summary>
    /// Определяет каталог исполняемого файла, чтобы находить translations.json независимо от рабочей папки.
    /// </summary>
    /// <param name="programPath">Путь запуска программы для платформ, отличных от Windows.</param>
    /// <returns>Каталог, в котором расположен исполняемый файл.</returns>
    std::filesystem::path getExecutableDirectory(const char* programPath)
    {
#ifdef _WIN32
        (void)programPath;
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0 || length >= buffer.size())
        {
            throw std::runtime_error("Cannot determine executable path");
        }

        return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
#else
        if (programPath == nullptr || *programPath == '\0')
        {
            return std::filesystem::current_path();
        }

        return std::filesystem::absolute(std::filesystem::path(programPath)).parent_path();
#endif
    }

    /// <summary>
    /// Считывает целое число с локализованным приглашением и повторяет запрос при ошибке.
    /// </summary>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <param name="language">Код выбранного языка.</param>
    /// <param name="promptKey">Ключ строки-приглашения в JSON.</param>
    /// <returns>Введённое целое число.</returns>
    int readIntegerWithPrompt(
        const lab2::Json& translations,
        const std::string& language,
        const std::string& promptKey)
    {
        while (true)
        {
            std::cout << lab2::tr(translations, language, promptKey);
            std::string line;
            if (!std::getline(std::cin, line))
            {
                throw std::runtime_error("Input ended");
            }

            const std::size_t first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                std::cout << lab2::tr(translations, language, "invalid_integer") << '\n';
                continue;
            }

            const std::size_t last = line.find_last_not_of(" \t\r\n");
            const std::string cleaned = line.substr(first, last - first + 1);

            try
            {
                std::size_t parsed = 0;
                const int value = std::stoi(cleaned, &parsed);
                if (parsed == cleaned.size())
                {
                    return value;
                }
            }
            catch (const std::exception&)
            {
                // Ниже выводится единое сообщение для любого некорректного ввода.
            }

            std::cout << lab2::tr(translations, language, "invalid_integer") << '\n';
        }
    }

    /// <summary>
    /// Выводит найденные элементы документа, используя локализованный заголовок раздела.
    /// </summary>
    void printCollection(
        const std::string& titleKey,
        const std::vector<std::string>& items,
        std::uint64_t totalCount,
        const lab2::Json& translations,
        const std::string& language)
    {
        std::cout << '\n' << lab2::tr(translations, language, titleKey)
            << " (" << totalCount << "):\n";

        if (totalCount > items.size())
        {
            std::cout << "  " << lab2::tr(translations, language, "preview_limited")
                << " " << items.size() << " / " << totalCount << "\n";
        }

        if (items.empty())
        {
            std::cout << "  " << lab2::tr(translations, language, "not_found") << '\n';
            return;
        }

        // Списки выводятся отдельными блоками, чтобы не добавлять номер перед первым маркером.
        if (titleKey == "lists")
        {
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                std::cout << "  " << lab2::tr(translations, language, "list_group")
                    << " " << index + 1 << ":\n";
                std::istringstream listInput(items[index]);
                std::string listLine;
                while (std::getline(listInput, listLine))
                {
                    std::cout << "    " << listLine << '\n';
                }
            }
            return;
        }

        for (std::size_t index = 0; index < items.size(); ++index)
        {
            std::cout << "  " << index + 1 << ". " << items[index] << '\n';
        }
    }

    /// <summary>
    /// Выводит результат проверки Markdown и найденные элементы документа.
    /// </summary>
    void printAnalysis(
        const lab2::MarkupAnalysis& analysis,
        const lab2::Json& translations,
        const std::string& language)
    {
        if (!analysis.valid)
        {
            std::cout << '\n' << lab2::tr(translations, language, "markup_errors") << '\n';
            for (const std::string& errorKey : analysis.errors)
            {
                std::cout << "  - " << lab2::tr(translations, language, errorKey) << '\n';
            }
            std::cout << lab2::tr(translations, language, "elements_not_shown") << '\n';
            return;
        }

        std::cout << '\n' << lab2::tr(translations, language, "markdown_valid") << '\n';
        printCollection("headings", analysis.headings, analysis.headingCount, translations, language);
        printCollection("paragraphs", analysis.paragraphs, analysis.paragraphCount, translations, language);
        printCollection("lists", analysis.lists, analysis.listCount, translations, language);
    }

    /// <summary>
    /// Выполняет проверку Markdown-файла, примера из проекта или встроенного примера.
    /// </summary>
    void markupMenu(
        const lab2::Json& translations,
        const std::string& language,
        const std::filesystem::path& executableDirectory)
    {
        std::cout << lab2::tr(translations, language, "markup_path_prompt");
        std::string path;
        if (!std::getline(std::cin, path))
        {
            throw std::runtime_error("Input ended while reading file path");
        }
        // Windows Explorer / PowerShell может вставлять путь в кавычках.
        // Удаляем только внешние пробелы и парные кавычки; пробелы в имени сохраняем.
        const auto first = path.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            path.clear();
        else
        {
            const auto last = path.find_last_not_of(" \t\r\n");
            path = path.substr(first, last - first + 1);
            if (path.size() >= 2 &&
                ((path.front() == '"' && path.back() == '"') ||
                    (path.front() == '\'' && path.back() == '\'')))
                path = path.substr(1, path.size() - 2);
        }

        lab2::MarkupAnalysis analysis;
        if (path.empty())
        {
            // Файл анализируется потоком, поэтому даже очень большой Markdown
            // не загружается в память целиком.
            const std::vector<std::filesystem::path> defaultCandidates{
                std::filesystem::current_path() / "examples" / "example.md",
                executableDirectory / "examples" / "example.md",
                executableDirectory / "example.md"
            };

            bool exampleLoaded = false;
            for (const std::filesystem::path& candidate : defaultCandidates)
            {
                std::error_code fileError;
                if (!std::filesystem::is_regular_file(candidate, fileError))
                    continue;

                std::string errorKey;
                if (lab2::analyzeMarkdownFile(candidate, analysis, errorKey))
                {
                    exampleLoaded = true;
                    std::cout << lab2::tr(translations, language, "default_example_used") << '\n';
                    break;
                }
            }

            if (!exampleLoaded)
            {
                analysis = lab2::analyzeMarkdown(lab2::sampleMarkdown(translations, language));
                std::cout << lab2::tr(translations, language, "sample_used") << '\n';
            }
        }
        else
        {
            try
            {
                const auto filename = std::filesystem::u8path(path);
                std::error_code sizeError;
                const auto bytes = std::filesystem::file_size(filename, sizeError);
                const bool showProgress = !sizeError && bytes >= 64ULL * 1024 * 1024;
                if (showProgress)
                    std::cout << lab2::tr(translations, language, "scanning_file") << '\n' << std::flush;

                const auto started = std::chrono::steady_clock::now();
                const auto displayProgress = [&](std::uintmax_t completed, std::uintmax_t total)
                    {
                        if (showProgress && total != 0)
                        {
                            const double percent = 100.0 * static_cast<double>(completed) /
                                static_cast<double>(total);
                            std::cout << '\r' << lab2::tr(translations, language, "scanning_progress")
                                << ' ' << std::fixed << std::setprecision(1)
                                << percent << "%     " << std::flush;
                        }
                    };

                std::string errorKey;
                const bool opened = lab2::analyzeMarkdownFile(filename, analysis, errorKey, displayProgress);
                if (showProgress)
                {
                    const auto elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - started).count();
                    std::cout << '\n' << lab2::tr(translations, language, "scan_duration")
                        << ' ' << std::fixed << std::setprecision(2) << elapsed
                        << lab2::tr(translations, language, "seconds_suffix") << '\n';
                }
                if (!opened)
                {
                    std::cout << lab2::tr(translations, language, errorKey) << path << '\n';
                    return;
                }
            }
            catch (const std::exception& error)
            {
                std::cout << '\n' << lab2::tr(translations, language, "processing_error")
                    << " (" << error.what() << ")\n";
                return; // Вернуться в меню вместо закрытия приложения.
            }
        }

        printAnalysis(analysis, translations, language);
    }

    /// <summary>
    /// Запрашивает данные анкеты, проверяет поля и показывает нормализованный результат.
    /// </summary>
    void questionnaireMenu(const lab2::Json& translations, const std::string& language)
    {
        std::cout << '\n' << lab2::tr(translations, language, "questionnaire_intro") << '\n';

        lab2::UserData data = lab2::readUserData(translations, language);
        std::vector<std::string> errorKeys;

        if (lab2::normalizeAndValidateUserData(data, errorKeys))
        {
            std::cout << '\n' << lab2::tr(translations, language, "validation_success") << '\n';
            lab2::printUserData(data, translations, language);
            return;
        }

        std::cout << '\n' << lab2::tr(translations, language, "validation_errors") << '\n';
        for (const std::string& errorKey : errorKeys)
        {
            std::cout << "  - " << lab2::tr(translations, language, errorKey) << '\n';
        }
        std::cout << lab2::tr(translations, language, "repeat_with_correct_data") << '\n';
    }

    /// <summary>
    /// Генерирует случайные анкеты и проверяет каждую тем же валидатором, что и пользовательский ввод.
    /// </summary>
    void generatorMenu(const lab2::Json& translations, const std::string& language)
    {
        int count = 0;
        while (true)
        {
            count = readIntegerWithPrompt(translations, language, "generator_count_prompt");
            if (count >= 1 && count <= 20)
            {
                break;
            }

            std::cout << lab2::tr(translations, language, "invalid_generator_count") << '\n';
        }

        for (int index = 0; index < count; ++index)
        {
            lab2::UserData data = lab2::generateRandomUserData();

            std::cout << '\n' << lab2::tr(translations, language, "record_source")
                << index + 1 << " ---\n";
            lab2::printUserData(data, translations, language);

            std::vector<std::string> errorKeys;
            if (lab2::normalizeAndValidateUserData(data, errorKeys))
            {
                std::cout << lab2::tr(translations, language, "record_result") << '\n';
                lab2::printUserData(data, translations, language);
            }
            else
            {
                std::cout << lab2::tr(translations, language, "generator_failed") << '\n';
                for (const std::string& errorKey : errorKeys)
                {
                    std::cout << "  - " << lab2::tr(translations, language, errorKey) << '\n';
                }
            }
        }
    }
}

/// <summary>
/// Точка входа программы. Загружает переводы, предлагает выбрать язык и открывает меню лабораторной работы.
/// </summary>
/// <returns>0 при штатном завершении; 1 при ошибке загрузки обязательных файлов.</returns>
int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
#endif

    lab2::Json translations;
    std::filesystem::path executableDirectory;
    try
    {
        executableDirectory = getExecutableDirectory(argc > 0 ? argv[0] : nullptr);
        translations = lab2::loadTranslations(executableDirectory / "translations.json");
    }
    catch (const std::exception&)
    {
        std::cerr << "Не удалось открыть или прочитать translations.json.\n";
        return 1;
    }

    std::string language;
    try
    {
        language = lab2::chooseLanguage(translations);
    }
    catch (const std::exception&)
    {
        return 0;
    }

    try
    {
        while (true)
        {
            std::cout << lab2::tr(translations, language, "title") << '\n' << "1. " << lab2::tr(translations, language, "menu_markup") << '\n'
                << "2. " << lab2::tr(translations, language, "menu_questionnaire") << '\n'
                << "3. " << lab2::tr(translations, language, "menu_generator") << '\n'
                << "0. " << lab2::tr(translations, language, "menu_exit") << '\n';

            const int choice = readIntegerWithPrompt(translations, language, "menu_choice");

            switch (choice)
            {
            case 1:
                markupMenu(translations, language, executableDirectory);
                break;
            case 2:
                questionnaireMenu(translations, language);
                break;
            case 3:
                generatorMenu(translations, language);
                break;
            case 0:
                std::cout << lab2::tr(translations, language, "goodbye") << '\n';
                return 0;
            default:
                std::cout << lab2::tr(translations, language, "invalid_choice") << '\n';
                break;
            }
        }
    }
    catch (const std::exception& error)
    {
        if (std::cin.eof())
            std::cout << '\n' << lab2::tr(translations, language, "input_error") << '\n';
        else
            std::cerr << '\n' << lab2::tr(translations, language, "processing_error")
            << " (" << error.what() << ")\n";
    }

    return 0;
}
