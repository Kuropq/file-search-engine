#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <thread>
#include <cmath>
#include <mutex>

std::mutex freq_dictionary_access;

struct RelativeIndex{
    size_t doc_id;
    float rank;
    bool operator ==(const RelativeIndex& other) const {
        return (doc_id == other.doc_id && rank == other.rank);
    }
};

struct Entry {
public:
    size_t doc_id, count;
    // Данный оператор необходим для проведения тестовых сценариев
    bool operator ==(const Entry& other) const {
        return (doc_id == other.doc_id &&
                count == other.count);
    }
};

//----------------------------------------------INDEX----------------------------------------------

class InvertedIndex {
public:
    InvertedIndex() = default;
    /**
*  Небольшая функция для проверки есть ли в векторе id
*/
    bool FindId(const std::vector<Entry>& vec, const int& id) {
        for (auto& entry : vec) {
            if (entry.doc_id == id) {
                return true;
            }
        }
        return false;
    }
    /**
 * Обновить или заполнить базу документов, по которой будем совершать
 поиск
 * @param texts_input содержимое документов
 */
    void UpdateDocumentBase(const std::vector<std::string>& texts_input) {
        freq_dictionary.clear();
        docs = texts_input;
        std::vector<std::thread> threads;
        for (int id{0}; id < docs.size(); id++) {
            threads.emplace_back([this, id]{
                std::string doc{docs[id]};
                std::regex re("\\w+");
                std::smatch matchBuf;
                while (std::regex_search (doc,matchBuf,re)) {
                    for (auto match : matchBuf) {
                        freq_dictionary_access.lock();
                        if ((freq_dictionary.find(match) == freq_dictionary.end()) || (!FindId(freq_dictionary[match], id))) {
                            Entry entry;
                            entry.doc_id = id;
                            entry.count = 1;
                            freq_dictionary[match].push_back(entry);
                        } else {
                            for (auto& entry : freq_dictionary[match]) {
                                if (entry.doc_id == id) {
                                    entry.count++;
                                }
                            }
                        }
                        freq_dictionary_access.unlock();
                    }
                    doc = matchBuf.suffix().str();
                }});
        }
        for (std::thread & t : threads) {
            t.join();
        }
    }
    /**
* Метод определяет количество вхождений слова word в загруженной базе
 документов
 * @param word слово, частоту вхождений которого необходимо определить
 * @return возвращает подготовленный список с частотой слов
 */
    std::vector<Entry> GetWordCount(const std::string& word){
        return freq_dictionary[word];
    }

private:
    std::vector<std::string> docs; // список содержимого документов
    std::map<std::string, std::vector<Entry>> freq_dictionary; // частотный словарь
};

//----------------------------------------------SEARCH----------------------------------------------

class SearchServer {
public:

    /**
 * @param idx в конструктор класса передаётся ссылка на класс
 InvertedIndex,
 *      чтобы SearchServer мог узнать частоту слов встречаемых в
 запросе
 */
    SearchServer(InvertedIndex& idx) : _index(idx){}
    /**
 * Метод обработки поисковых запросов
 * @param queries_input поисковые запросы взятые из файла
 requests.json
 * @return возвращает отсортированный список релевантных ответов для
 заданных запросов
 */
    std::vector<std::vector<RelativeIndex>> search(const std::vector<std::string>& queries_input, const int& limit) {
        std::vector<Entry> raw_answers;

        //релевантность документов
        std::map<int, float> relevance_map;
        for (const auto& str : queries_input) {
            std::vector<std::string> words;
            std::stringstream ss(str);
            std::string str_buf;
            while (ss >> str_buf) words.push_back(str_buf);
            for (const auto& word : words) {
                raw_answers = _index.GetWordCount(word);
                for (const auto& raw_answer : raw_answers) {
                    relevance_map[raw_answer.doc_id] += raw_answer.count;
                }
            }
        }

        //максимальная релевантность
        int max_value{0};
        for (auto& answer : relevance_map) {
            if (max_value < answer.second) max_value = answer.second;
        }

        for (auto& answer : relevance_map) {
            answer.second = answer.second / max_value;
        }

        //запись ответов
        std::vector<std::vector<RelativeIndex>> result;
        for (const auto& str : queries_input) {
            std::vector<std::string> words;
            std::stringstream ss(str);
            std::string str_buf;
            while (ss >> str_buf) words.push_back(str_buf);
            std::vector<RelativeIndex> tmp_vector;
            max_value = 0;
            //поиск ответа по слову с наибольшим колвом файлов
            raw_answers = _index.GetWordCount(words[0]);
            for (const auto& word : words) {
                if (max_value < _index.GetWordCount(word).size()) {
                    max_value = _index.GetWordCount(word).size();
                    raw_answers = _index.GetWordCount(word);
                }
            }
            //
            std::sort(begin(raw_answers), end(raw_answers), [&relevance_map](const Entry& left, const Entry& right) {
                if (relevance_map[left.doc_id] == relevance_map[right.doc_id]) return left.doc_id < right.doc_id;
                return relevance_map[left.doc_id] > relevance_map[right.doc_id];
            });
            int i{0};
            for (const auto& raw_answer : raw_answers) {
                RelativeIndex relative_index;
                relative_index.doc_id = raw_answer.doc_id;
                relative_index.rank = std::move(relevance_map[raw_answer.doc_id]);
                tmp_vector.push_back(relative_index);
                i++;
                if (i == limit) break;
            }
            std::sort(begin(tmp_vector), end(tmp_vector), [](const RelativeIndex& left, const RelativeIndex& right) {
                if (left.rank == right.rank) return left.doc_id < right.doc_id;
                return left.rank > right.rank;
            });
            result.push_back(tmp_vector);
        }
        return result;
    }
private:
    InvertedIndex& _index; // ссылка на InvertedIndex для доступа к freq_dictionary
};

//----------------------------------------------JSON----------------------------------------------

class ConverterJSON {
public:
    /**
* конструктор и деструктор класса участвуют в записи ответа в result.json
*/
    ConverterJSON() {
        result.open("..\\result.json", std::ofstream::trunc); // E:\\develop\\QT\\EP\\result.json
        result << ("{\n\t\"answers\":{");
    }
    ~ConverterJSON() {
        result << "\n\t}\n}";
        result.close();
    }

    /**
 * Метод получения содержимого файлов
 * @return Возвращает список с содержимым файлов перечисленных
 *       в config.json
 */
    std::vector<std::string> GetTextDocuments() {
        config.open("..\\config.json"); // E:\\develop\\QT\\EP\\config.json
        std::vector<std::string> textDocuments;
        std::string strBuf;
        std::regex re("\"..[^\"]+");
        std::smatch matchBuf;
        bool isFiles = false;
        while (std::getline(config, strBuf)) {
            std::regex_search(strBuf, matchBuf, re);
            if (matchBuf[0] == "\"files") isFiles = true;
            else if ((isFiles == true) && (matchBuf[0] != "")) {
                textDocuments.push_back(matchBuf[0]);
                textDocuments.back().erase(remove(textDocuments.back().begin(), textDocuments.back().end(), '\"'),textDocuments.back().end());
            }
        }
        config.close();
        return textDocuments;
    }
    /**
 * Метод считывает поле max_responses для определения предельного
 *  количества ответов на один запрос
 * @return
 */
    int GetResponsesLimit() {
        config.open("..\\config.json"); // E:\\develop\\QT\\EP\\config.json
        std::string strBuf;
        std::regex re("\"([A-Za-z0-9_\\./\\-\\s]*)\"");
        std::smatch matchBuf;
        while (std::getline(config, strBuf)) {
            std::regex_search(strBuf, matchBuf, re);
            if (matchBuf[0] == "\"max_responses\"") {
                re = ("\\d+");
                std::regex_search(strBuf, matchBuf, re);
                config.close();
                return std::stoi(matchBuf[0]);
            }
        }
        config.close();
        return 5;
    }
    /**
 * Метод получения запросов из файла requests.json
 * @return возвращает список запросов из файла requests.json
 */
    std::vector<std::string> GetRequests() {
        request.open("..\\request.json"); // E:\\develop\\QT\\EP\\request.json
        std::vector<std::string> result;
        std::string strBuf;
        std::getline(request, strBuf);
        std::getline(request, strBuf);
        std::regex re("\"([A-Za-z0-9_\\./\\-\\s]*)\"");
        std::smatch matchBuf;
        while (std::getline(request, strBuf)) {
            std::regex_search(strBuf, matchBuf, re);
            if (matchBuf[0] != "") {
                result.push_back(matchBuf[0]);
                result.back().erase(remove(result.back().begin(), result.back().end(), '\"'),result.back().end());
            }
        }
        request.close();
        return result;
    }
    /**
 * Положить в файл answers.json результаты поисковых запросов
 */
    void PutAnswers(const std::vector<std::vector<std::pair<int, float>>>& answers) { //
        int i = 0;
        for (const auto& answer : answers) {
            result << "\n\t\t\"request" << i << "\":{";
            if (answer.empty())
                result << "\n\t\t\t\"result\": \"false\"";
            else {
                result << "\n\t\t\t\"result\": \"true\",\n\t\t\t\"relevance\": {";
                int j = answer.size();
                for (const auto& info : answer) {
                    j -= 1;
                    result << "\n\t\t\t\t\"docid\": " << info.first << ", \"rank\" : " << info.second;
                    if (j != 0) result << ",";
                }
                result << "\n\t\t\t}";
            }
            i++;
            result << "\n\t\t}";
            if (i != answers.size()) result << ",";
        }
    }
private:
    std::ifstream config; // файл настройки
    std::ifstream request; // файл запроса
    std::ofstream result; // файл ответа
};

//============================================TEST============================================

TEST(TestCaseSearchServer, TestSimple) {
    const std::vector<std::string> docs = {
        "milk milk milk milk water water water",
        "milk water water",
        "milk milk milk milk milk water water water water water",
        "americano cappuccino"
    };
    const std::vector<std::string> request = {"milk water", "sugar"};
    const std::vector<std::vector<RelativeIndex>> expected = {
        {
            {2, 1},
            {0, 0.7},
            {1, 0.3}
        },
        {
        }
    };
    InvertedIndex idx;
    idx.UpdateDocumentBase(docs);
    SearchServer srv(idx);
    std::vector<std::vector<RelativeIndex>> result = srv.search(request, 5);
    ASSERT_EQ(result, expected);
}

TEST(TestCaseSearchServer, TestTop5) {
    const std::vector<std::string> docs = {
        "london is the capital of great britain",
        "paris is the capital of france",
        "berlin is the capital of germany",
        "rome is the capital of italy",
        "madrid is the capital of spain",
        "lisboa is the capital of portugal",
        "bern is the capital of switzerland",
        "moscow is the capital of russia",
        "kiev is the capital of ukraine",
        "minsk is the capital of belarus",
        "astana is the capital of kazakhstan",
        "beijing is the capital of china",
        "tokyo is the capital of japan",
        "bangkok is the capital of thailand",
        "welcome to moscow the capital of russia the third rome",
        "amsterdam is the capital of netherlands",
        "helsinki is the capital of finland",
        "oslo is the capital of norway",
        "stockholm is the capital of sweden",
        "riga is the capital of latvia",
        "tallinn is the capital of estonia",
        "warsaw is the capital of poland",
    };
    const std::vector<std::string> request = {"moscow is the capital of russia"};
    const std::vector<std::vector<RelativeIndex>> expected = {
        {
            {7, 1},
            {14, 1},
            {0, 0.666666687},
            {1, 0.666666687},
            {2, 0.666666687}
        }
    };
    InvertedIndex idx;
    idx.UpdateDocumentBase(docs);
    SearchServer srv(idx);
    std::vector<std::vector<RelativeIndex>> result = srv.search(request, 5);
    ASSERT_EQ(result, expected);
}

//============================================TEST============================================

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);    

    ConverterJSON converter;
    InvertedIndex inverted_index;
    SearchServer search_server(inverted_index);

    std::vector<std::string> docs;
    for (const auto& doc : converter.GetTextDocuments()) { //документы в строки (вне функций для тестов)
        std::ifstream docReader;
        std::string text, tmp_str;
        docReader.open(doc);
        while (docReader >> tmp_str) {
            text = text + " " + tmp_str;
        }
        docs.push_back(text);
        docReader.close();
        text = "";
    }

    inverted_index.UpdateDocumentBase(docs);
    std::vector<std::vector<std::pair<int, float>>> final_answers;
    std::vector<std::vector<RelativeIndex>> answers {search_server.search(converter.GetRequests(), converter.GetResponsesLimit())};
    for (const auto& answer : answers) { //перобразование типа ответа
        std::vector<std::pair<int, float>> tmp_vector;
        for (const auto& data : answer) {
            tmp_vector.push_back(std::make_pair(data.doc_id, data.rank));
        }
        final_answers.push_back(tmp_vector);
    }
    converter.PutAnswers(final_answers);


    return RUN_ALL_TESTS();
}
