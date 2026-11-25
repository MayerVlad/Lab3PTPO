#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Lab2Export.h"

#include <nlohmann/json.hpp>

namespace lab2
{
    using Json = nlohmann::json;

    /// <summary>
    /// Хранит результат проверки Markdown-документа и найденные элементы.
    /// В errors записываются ключи сообщений из translations.json.
    /// </summary>
    struct MarkupAnalysis
    {
        bool valid = false;
        std::vector<std::string> errors;
        std::vector<std::string> headings;
        std::vector<std::string> paragraphs;
        std::vector<std::string> lists;
        // Общее количество элементов; в векторах хранятся первые 20 для экономии памяти.
        std::uint64_t headingCount = 0;
        std::uint64_t paragraphCount = 0;
        std::uint64_t listCount = 0;
    };

    /// <summary>
    /// Хранит поля пользовательской анкеты.
    /// </summary>
    struct UserData
    {
        std::string surname;
        std::string firstName;
        std::string patronymic;
        std::string birthDate;
        std::string phone;
        std::string email;
    };

    /// <summary>
    /// Загружает переводы интерфейса из JSON-файла.
    /// </summary>
    /// <param name="filename">Путь к translations.json.</param>
    /// <returns>Объект с переводами для русского, английского и французского языков.</returns>
    LAB2_API Json loadTranslations(const std::filesystem::path& filename);

    /// <summary>
    /// Возвращает локализованную строку интерфейса по языку и ключу.
    /// </summary>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <param name="language">Код языка: ru, en или fr.</param>
    /// <param name="key">Ключ строки в JSON.</param>
    /// <returns>Переведённая строка в UTF-8.</returns>
    LAB2_API std::string tr(const Json& translations, const std::string& language, const std::string& key);

    /// <summary>
    /// Предлагает выбрать язык интерфейса и проверяет введённый номер.
    /// </summary>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <returns>Код выбранного языка: ru, en или fr.</returns>
    LAB2_API std::string chooseLanguage(const Json& translations);

    /// <summary>
    /// Потоково проверяет Markdown-файл, не загружая его полностью в память.
    /// Расширения HTML/AsciiDoc отклоняются сразу; ошибки разметки находятся в result.errors.
    /// </summary>
    /// <param name="filename">Путь к проверяемому файлу.</param>
    /// <param name="result">Результаты проверки и найденные элементы.</param>
    /// <param name="errorKey">Ключ ошибки ввода-вывода или неподдерживаемого типа; пусто при успехе.</param>
    /// <returns>true, если удалось обработать файл (даже если его разметка некорректна).</returns>
    LAB2_API bool analyzeMarkdownFile(
        const std::filesystem::path& filename,
        MarkupAnalysis& result,
        std::string& errorKey,
        const std::function<void(std::uintmax_t, std::uintmax_t)>& progress = {});

    /// <summary>
    /// Проверяет Markdown-текст по тем же правилам, что и потоковый обработчик файла.
    /// </summary>
    /// <param name="text">Текст Markdown-документа.</param>
    /// <returns>Найденные элементы и ключи ошибок.</returns>
    LAB2_API MarkupAnalysis analyzeMarkdown(const std::string& text);

    /// <summary>
    /// Создаёт демонстрационный Markdown-документ на выбранном языке интерфейса.
    /// </summary>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <param name="language">Код выбранного языка интерфейса.</param>
    /// <returns>Пример корректного Markdown-документа.</returns>
    LAB2_API std::string sampleMarkdown(const Json& translations, const std::string& language);

    /// <summary>
    /// Считывает шесть полей анкеты с локализованными приглашениями.
    /// </summary>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <param name="language">Код выбранного языка интерфейса.</param>
    /// <returns>Анкета с исходными значениями пользователя.</returns>
    LAB2_API UserData readUserData(const Json& translations, const std::string& language);

    /// <summary>
    /// Нормализует и проверяет поля анкеты; возвращает ключи ошибок для локализации.
    /// </summary>
    /// <param name="data">Анкета, поля которой будут нормализованы.</param>
    /// <param name="errorKeys">Список ключей сообщений об ошибках.</param>
    /// <returns>true, если все поля корректны; иначе false.</returns>
    LAB2_API bool normalizeAndValidateUserData(UserData& data, std::vector<std::string>& errorKeys);

    /// <summary>
    /// Создаёт случайную тестовую анкету с разными вариантами написания полей.
    /// </summary>
    /// <returns>Случайные пользовательские данные для проверки нормализации.</returns>
    LAB2_API UserData generateRandomUserData();

    /// <summary>
    /// Выводит поля анкеты с подписями на выбранном языке.
    /// </summary>
    /// <param name="data">Анкета для вывода.</param>
    /// <param name="translations">Загруженный объект с переводами.</param>
    /// <param name="language">Код выбранного языка интерфейса.</param>
    LAB2_API void printUserData(
        const UserData& data,
        const Json& translations,
        const std::string& language);
}
