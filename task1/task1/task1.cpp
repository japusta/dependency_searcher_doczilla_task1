#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

class DependencySearcher {
private:
    std::string directory;
    std::vector<fs::path> txt_files_list;
    std::string result_path;
    std::unordered_map<fs::path, std::vector<fs::path>> dependencies;
    std::unordered_set<fs::path> visited;
    std::unordered_set<fs::path> resolved;

public:
    DependencySearcher(const std::string& directory) : directory(directory) {
        result_path = directory + "/result.txt";
    }

    void ReplaceQuotesInFile(const fs::path& file_path) {
        std::ifstream in_file(file_path);
        if (!in_file.is_open()) {
            std::cerr << "Не удалось открыть файл для чтения: " << file_path << std::endl;
            return;
        }

        std::string content;
        std::string line;
        while (std::getline(in_file, line)) {
            for (char& ch : line) { // Заменяем специальные кавычки на обычные одинарные кавычки
                if (ch == '‘' || ch == '’') {
                    ch = '\'';
                }
            }
            content += line + "\n";
        }
        in_file.close();

        std::ofstream out_file(file_path);
        if (!out_file.is_open()) {
            std::cerr << "Не удалось открыть файл для записи: " << file_path << std::endl;
            return;
        }
        out_file << content;
        out_file.close();
    }

    void SearchFile(const fs::path& dir) {
        if (fs::exists(dir) && fs::is_directory(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (fs::is_regular_file(entry) &&
                    (entry.path().extension() == ".txt" || entry.path().extension() == ".TXT")) {
                    ReplaceQuotesInFile(entry.path());                      // Заменяем кавычки в файле перед парсингом
                    txt_files_list.push_back(entry.path());
                    ParseDependencies(entry.path());
                }
                else if (fs::is_directory(entry)) {
                    SearchFile(entry);
                }
            }
        }

        if (txt_files_list.empty()) {
            std::cout << "В указанной вами директории файлов формата \".txt\" не найдено, "
                << "файл результата не изменён." << std::endl;
            exit(0);
        }
    }

    void ParseDependencies(const fs::path& file_path) {
        std::ifstream in_file(file_path);
        if (!in_file.is_open()) {
            std::cerr << "Не удалось открыть файл для чтения: " << file_path << std::endl;
            return;
        }

        std::string line;
        while (std::getline(in_file, line)) {             // Удаляем пробелы в начале и конце строки
            line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
            }));
            line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
            }).base(), line.end());

            std::cout << "Анализируем строку: \"" << line << "\"" << std::endl;            // Выводим каждую строку для отладки

            std::string prefix = "require '";
            std::string suffix = "'";
            if (line.substr(0, prefix.size()) == prefix && line.back() == suffix.back()) {
                std::string dependency_path = line.substr(prefix.size(), line.size() - prefix.size() - suffix.size());
                fs::path dep_path(dependency_path);

                // Если расширение отсутствует, добавляем ".txt"
                if (dep_path.extension().empty()) {
                    dep_path.replace_extension(".txt");
                }

                // Если путь не абсолютный, преобразуем его в абсолютный на основе текущего каталога
                if (!dep_path.is_absolute()) {
                    dep_path = fs::absolute(fs::path(directory) / dep_path);
                }

                std::cout << "Сформированный путь: \"" << dep_path.string() << "\"" << std::endl;

                // Проверяем существование файла зависимости
                if (fs::exists(dep_path)) {
                    dependencies[file_path].push_back(dep_path);
                    std::cout << "Найдена зависимость: \"" << dep_path.string() << "\"" << std::endl;
                }
                else {
                    std::cerr << "Зависимый файл не найден: \"" << dep_path.string() << "\"" << std::endl;
                }
            }
        }
        // Для отладки: выводим зависимости для текущего файла
        std::cout << "Зависимости для файла \"" << file_path.string() << "\":" << std::endl;
        for (const auto& dep : dependencies[file_path]) {
            std::cout << "  - \"" << dep.string() << "\"" << std::endl;
        }
        std::cout << std::endl;
    }

    bool BuildOrderUtil(const fs::path& file, std::unordered_set<fs::path>& recursion_stack, std::vector<fs::path>& order) {
        if (resolved.find(file) != resolved.end()) {
            return true;
        }

        if (recursion_stack.find(file) != recursion_stack.end()) {
            // Обнаружен цикл, выводим его
            std::cerr << "Циклическая зависимость обнаружена: " << file << std::endl;
            std::cerr << "Цикл зависимости: ";
            for (const auto& path : recursion_stack) {
                std::cerr << path.string() << " -> ";
            }
            std::cerr << file.string() << std::endl;
            return false;
        }

        recursion_stack.insert(file);

        for (const auto& dep : dependencies[file]) {
            if (!BuildOrderUtil(dep, recursion_stack, order)) {
                return false;
            }
        }

        recursion_stack.erase(file);
        resolved.insert(file);
        order.push_back(file);

        return true;
    }

    bool BuildOrder(std::vector<fs::path>& order) {
        std::unordered_set<fs::path> recursion_stack;

        for (const auto& file : txt_files_list) {
            if (!BuildOrderUtil(file, recursion_stack, order)) {
                return false;
            }
        }
        return true;
    }



    void ReadWrite(const std::vector<fs::path>& order) {
        // Пересоздаем результирующий файл
        std::ofstream out_file(result_path, std::ofstream::trunc);
        if (!out_file.is_open()) {
            std::cerr << "Не удалось открыть файл для записи: " << result_path << std::endl;
            return;
        }

        for (const auto& file : order) {
            // Пропускаем результирующий файл, чтобы он не добавлялся сам в себя
            if (file == result_path) {
                continue;
            }

            std::ifstream inFile(file);
            if (!inFile.is_open()) {
                std::cerr << "Не удалось открыть файл для чтения: " << file << std::endl;
                continue;
            }

            out_file << "Файл: " << file.string().substr(directory.length()) << std::endl;
            out_file << "---------------------" << std::endl;

            //std::string line;
            //while (std::getline(inFile, line)) {
            //    out_file << line << std::endl;
            //}

            out_file << std::endl;
        }

        out_file.close();
    }


    std::string getResultPath() const {
        return result_path;
    }

    static std::string ConsoleInput() {
        std::string directory;
        while (true) {
            std::cout << "Введите директорию для поиска." << std::endl;
            std::getline(std::cin, directory);

            if (directory == "exit") {
                exit(0);
            }

            if (fs::exists(directory) && fs::is_directory(directory)) {
                break;
            }
            else {
                std::cout << "Вы ввели некорректную директорию, пожалуйста, "
                    << "проверьте данные. Для завершения работы программы введите: exit" << std::endl;
            }
        }
        return directory;
    }
};

int main() {
    setlocale(LC_ALL, "Russian");
    std::string directory = DependencySearcher::ConsoleInput();

    DependencySearcher searcher(directory);

    fs::path root_dir(directory);
    searcher.SearchFile(root_dir);

    std::vector<fs::path> order;
    if (!searcher.BuildOrder(order)) {
        std::cerr << "Не удалось построить порядок файлов из-за циклической зависимости." << std::endl;
        return 1;
    }

    searcher.ReadWrite(order);

    std::cout << "Файлы были успешно обработаны и записаны в результирующий файл: " << searcher.getResultPath() << std::endl;

    return 0;
}
