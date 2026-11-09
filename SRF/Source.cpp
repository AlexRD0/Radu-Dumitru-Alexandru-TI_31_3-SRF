#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <pugixml.hpp>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <cctype>
#include <algorithm>
#include "PorterStemmer.h"

struct DocumentFeatures {
    string id;
    string split;
    map<string, int> frequencies;
    vector<string> topics;
};

using namespace std;

namespace fs = std::filesystem;

set<string> loadStopwords(const string& filepath) {
    set<string> stopwords;
    ifstream file(filepath);
    if (!file.is_open()) {
        cerr << "[Avertisment] Nu s-a putut deschide " << filepath << "\n";
        return stopwords;
    }
    string word;
    while (getline(file, word)) {
        // Trim whitespace
        word.erase(0, word.find_first_not_of(" \t\r\n"));
        word.erase(word.find_last_not_of(" \t\r\n") + 1);
        if (!word.empty()) {
            stopwords.insert(word);
        }
    }
    file.close();
    cout << "[Info] S-au incarcat " << stopwords.size() << " stopwords.\n";
    return stopwords;
}

string toLower(const string& text) {
    string result = text;
    transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return tolower(c); });
    return result;
}

// Normalizeaza textul: separa cuvintele, elimina stopwords si aplica stemming.
vector<string> processText(const string& text, const set<string>& stopwords, PorterStemmer& stemmer) {
    vector<string> processedWords;
    string word;

    // Construieste fiecare cuvant folosind doar caractere alfabetice.
    for (unsigned char character : text) {
        if (isalpha(character)) {
            word.push_back(static_cast<char>(tolower(character)));
            continue;
        }

        // Orice caracter nealfabetic marcheaza sfarsitul unui cuvant.
        if (word.empty()) {
            continue;
        }

        // Pastreaza doar cuvintele relevante si le reduce la forma de baza.
        if (stopwords.find(word) == stopwords.end()) {
            processedWords.push_back(stemmer.StemWord(word));
        }
        word.clear();
    }

    // Proceseaza ultimul cuvant daca textul nu se termina cu punctuatie.
    if (!word.empty() && stopwords.find(word) == stopwords.end()) {
        processedWords.push_back(stemmer.StemWord(word));
    }

    return processedWords;
}

// Citeste un XML, extrage continutul relevant si il salveaza in format TXT.
bool processXmlFile(const fs::path& inputFilePath, const fs::path& outputFilePath,
    const string& split, const set<string>& stopwords, vector<DocumentFeatures>& documents) {
    // Incarca si valideaza structura XML.
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(inputFilePath.c_str());

    if (!result) {
        cerr << "[Eroare Incarcare] " << inputFilePath.filename().string()
            << " | " << result.description() << "\n";
        return false;
    }

    ofstream outFile(outputFilePath);
    if (!outFile.is_open()) {
        cerr << "[Eroare Scriere] Nu s-a putut deschide " << outputFilePath.string() << "\n";
        return false;
    }

    // Pregateste datele care vor fi salvate pentru acest document.
    PorterStemmer stemmer;
    DocumentFeatures features;
    features.split = split;
    pugi::xml_node newsItem = doc.child("newsitem");
    features.id = newsItem.attribute("itemid").value();
    if (features.id.empty()) {
        features.id = inputFilePath.stem().string();
    }

    // Extrage si normalizeaza titlul, apoi actualizeaza frecventele cuvintelor.
    // Extragere TITLU
    pugi::xml_node titleNode = newsItem.child("title");
    outFile << "=== TITLU ===\n";
    if (titleNode) {
        vector<string> titleWords = processText(titleNode.child_value(), stopwords, stemmer);
        for (const string& word : titleWords) {
            outFile << word << " ";
            ++features.frequencies[word];
        }
        outFile << "\n";
    }
    outFile << "\n";

    // Extrage paragrafele si aduna frecventele lor la cele din titlu.
    // Extragere PARAGRAFE
    outFile << "=== PARAGRAFE ===\n";
    pugi::xml_node textNode = newsItem.child("text");
    if (textNode) {
        for (pugi::xml_node p : textNode.children("p")) {
            vector<string> paragraphWords = processText(p.child_value(), stopwords, stemmer);
            for (const string& word : paragraphWords) {
                outFile << word << " ";
                ++features.frequencies[word];
            }
            outFile << "\n\n";
        }
    }
    outFile << "\n";

    // Extrage topicurile din metadata si le pastreaza in document.
    // Extragere METADATA
    outFile << "=== METADATA ===\n";
    pugi::xml_node metadataNode = newsItem.child("metadata");
    if (metadataNode) {
        for (pugi::xml_node codes : metadataNode.children("codes")) {
            if (string(codes.attribute("class").value()) != "bip:topics:1.0") {
                continue;
            }

            for (pugi::xml_node code : codes.children("code")) {
                string codeValue = code.attribute("code").value();
                if (!codeValue.empty()) {
                    features.topics.push_back(codeValue);
                    outFile << codeValue << "\n";
                }
            }
        }
    }

    documents.push_back(features);
    cout << "[Succes] " << inputFilePath.filename().string()
        << " -> " << outputFilePath.filename().string() << "\n";
    return true;
}

// Parcurge un director si proceseaza fiecare fisier XML gasit direct in acesta.
void processDirectory(const fs::path& inputDir, const fs::path& outputDir, const string& dirName,
    const set<string>& stopwords, vector<DocumentFeatures>& documents) {
    cout << "\n=== PROCESARE " << dirName << " ===\n";

    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        cerr << "[Eroare] Folderul de intrare nu exista: " << fs::absolute(inputDir) << "\n";
        return;
    }

    try {
        fs::create_directories(outputDir);
    }
    catch (const fs::filesystem_error& e) {
        cerr << "[Eroare] Nu s-a putut crea folderul de iesire: " << e.what() << "\n";
        return;
    }

    int processedCount = 0;

    // Verifica fiecare intrare si ignora directoarele sau fisierele non-XML.
    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (entry.is_regular_file()) {
            string ext = entry.path().extension().string();
            transform(ext.begin(), ext.end(), ext.begin(), 
                [](unsigned char c) { return tolower(c); });

            if (ext == ".xml") {
                // Pastreaza numele fisierului, dar schimba extensia pentru iesirea TXT.
                fs::path xmlPath = entry.path();
                fs::path txtPath = outputDir / xmlPath.filename();
                txtPath.replace_extension(".txt");

                if (processXmlFile(xmlPath, txtPath, dirName, stopwords, documents)) {
                    processedCount++;
                }
            }
        }
    }

    cout << "Procesare " << dirName << " completa! Au fost procesate " << processedCount << " fisiere XML.\n";
}

// Genereaza dictionarul global si reprezentarile VSM pentru toate documentele.
void writeVectorSpaceFiles(const fs::path& outputDirectory, const vector<DocumentFeatures>& documents) {
    // Colecteaza fiecare termen distinct folosit in documente.
    set<string> dictionary;
    for (const DocumentFeatures& document : documents) {
        for (const auto& [word, frequency] : document.frequencies) {
            dictionary.insert(word);
        }
    }

    ofstream dictionaryFile(outputDirectory / "global_dictionary.txt");
    ofstream vectorsFile(outputDirectory / "sparse_vectors.txt");
    ofstream datasetFile(outputDirectory / "feature_vectors.txt");
    if (!dictionaryFile.is_open() || !vectorsFile.is_open() || !datasetFile.is_open()) {
        cerr << "[Eroare Scriere] Nu s-au putut crea fisierele VSM.\n";
        return;
    }

    // Asociaza fiecarui termen o pozitie stabila in vectorul de caracteristici.
    map<string, size_t> positions;
    size_t position = 0;
    for (const string& word : dictionary) {
        positions[word] = position;
        dictionaryFile << "@attribute " << word << "\n";
        datasetFile << "@attribute " << word << "\n";
        ++position;
    }
    datasetFile << "@data\n";

    // Scrie doar termenii prezenti, pastrand vectorii intr-un format rar.
    for (const DocumentFeatures& document : documents) {
        vectorsFile << document.id;
        for (const auto& [word, frequency] : document.frequencies) {
            // Format: pozitia termenului:frecventa in document.
            vectorsFile << " " << positions[word] << ":" << frequency;
        }
        vectorsFile << "\n";

        // Adauga si topicurile si impartirea documentului pentru setul de date.
        datasetFile << document.id << " #";
        bool firstVector = true;
        for (const auto& [word, frequency] : document.frequencies) {
            datasetFile << (firstVector ? " " : " ") << positions[word] << ":" << frequency;
            firstVector = false;
        }
        datasetFile << " # ";
        for (size_t topicIndex = 0; topicIndex < document.topics.size(); ++topicIndex) {
            if (topicIndex > 0) datasetFile << " ";
            datasetFile << document.topics[topicIndex];
        }
        datasetFile << " # " << document.split << "\n";
    }

    cout << "[Succes] Dicționar global: " << dictionary.size()
        << " termeni; vectori rari: " << documents.size() << " documente.\n";
}

int main() {
    // Incarcare stopwords
    set<string> stopwords = loadStopwords("stopwords.txt");
    vector<DocumentFeatures> documents;

    // Procesare TESTING
    fs::path testingInput = "ReutersDataSet/Reuters/Reuters_34/Testing";
    fs::path testingOutput = "Data/Testing";
    processDirectory(testingInput, testingOutput, "Testing", stopwords, documents);

    // Procesare TRAINING
    fs::path trainingInput = "ReutersDataSet/Reuters/Reuters_34/Training";
    fs::path trainingOutput = "Data/Training";
    processDirectory(trainingInput, trainingOutput, "Training", stopwords, documents);

    writeVectorSpaceFiles("Data", documents);

    cout << "\n=== FINALIZARE ===\n";
    cout << "Toate fisierele XML au fost procesate cu succes!\n";
    cout << "Transformari aplicate:\n";
    cout << "  - Conversia in litere mici\n";
    cout << "  - Stemming\n";
    cout << "  - Eliminarea stopwords\n";
    return 0;
}