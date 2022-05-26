#include <iostream>
#include <cstdio>
#include <map>
#include "iomanip"
#include "lodepng.cpp"
#include "lodepng.h"
#include <chrono>
using namespace std::chrono;

int threads = 0;
int rounds = 1;

std::tuple<int, int> decodePNG(const char* filename, std::vector<unsigned char> &image) {
    //функция расшифровки PNG - заполняет вектор данными в формате 4 байта на пиксель в порядке RGBARGBA
    unsigned width, height;
    unsigned error = lodepng::decode(image, width, height, filename);

    if(error) {
        std::cout << filename << " decoder error " << error << ": " << lodepng_error_text(error) << std::endl; //если есть ошибка - выводим в консоль
        return {0, 0}; //и возвращаем нулевые размеры картинки
    } else return {width, height};
}

void encodePNG(const char* filename, std::vector<unsigned char> &image, unsigned width, unsigned height) {
    //функция сохранения в PNG
    unsigned error = lodepng::encode(filename, image, width, height);
    //если есть ошибка - выводим в консоль
    if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
}

void add(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size(); //окончание - длина вектора file1
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //не вычисляем альфа-канал, чтобы не делать полностью прозрачные изображения,
            // т.е. канал прозрачности всегда будет НЕпрозрачный = 255
            result[i] = 255;
            continue;
        }
        result[i] = std::min((int(file1[i]) + int(file2[i])), 255); //значения яркости пикселей могут быть в диапазоне [0; 255]
    }
}

void sub(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();//окончание - длина вектора file1
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //не вычисляем альфа-канал, чтобы не делать полностью прозрачные изображения,
            // т.е. канал прозрачности всегда будет НЕпрозрачный = 255
            result[i] = 255;
            continue;
        }
        result[i] = std::max((int(file1[i]) - int(file2[i])), 0); //значения яркости пикселей могут быть в диапазоне [0; 255]
    }
}

void extract_grain(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();//окончание - длина вектора file1
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //не вычисляем альфа-канал, чтобы не делать полностью прозрачные изображения,
            // т.е. канал прозрачности всегда будет НЕпрозрачный = 255
            result[i] = 255;
            continue;
        }
        result[i] = std::clamp((int(file1[i]) - int(file2[i])) + 128, 0, 255); //значения яркости пикселей могут быть в диапазоне [0; 255]
    }
}

void merge_grain(std::vector<unsigned char>&file1, std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();//окончание - длина вектора file1
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //не вычисляем альфа-канал, чтобы не делать полностью прозрачные изображения,
            // т.е. канал прозрачности всегда будет НЕпрозрачный = 255
            result[i] = 255;
            continue;
        }
        result[i] = std::clamp(int(file1[i]) + int(file2[i]) - 128, 0, 255); //значения яркости пикселей могут быть в диапазоне [0; 255]
    }
}
//в си switch не умеет кушать строки, поэтому делает такой гнусный хак
enum Modes {
    MODE_ADD,
    MODE_SUB,
    MODE_EXTRACT_GRAIN,
    MODE_MERGE_GRAIN
};
//всё ещё кормим свитч хаками
static std::map<std::string, int> modes = {
        {"add",   MODE_ADD},
        {"sub",   MODE_SUB},
        {"extract",  MODE_EXTRACT_GRAIN},
        {"merge", MODE_MERGE_GRAIN}
};
//печатаем помощь при запуске без параметров и при ошибке режима
void print_help(const std::string& error = ""){
    if (!error.empty()) std::cout << error << std::endl;
    std::cout << "Usage: " << std::endl;
    std::cout << "\timages.exe file1 file2 output mode num_threads num_rounds" << std::endl;
    std::cout << "Required params:" << std::endl;
    std::cout << "Path to file (string)" << std::endl;
    std::cout << "\tfile1\n"
                 "\tfile2\n"
                 "\toutput" << std::endl;
    std::cout << "Supported modes: \n"
                 "\tadd\n"
                 "\tsub\n"
                 "\textract\n"
                 "\tmerge" << std::endl;
    std::cout << "\nOptional params:" << std::endl;
    std::cout << "\tnum_rounds: default = 1" << std::endl;
    std::cout << "\nOnly PNG images with 1:1 aspect ratio are accepted" << std::endl;
}


int main(int argc, char *argv[]) {
    if (argc < 5 || *argv[1] == 'h'){ //выводим инфу как пользоваться и выходим, не гадаем что хотел пользователь
        print_help();
        return 0;
    }
    auto overall_start = high_resolution_clock::now(); //считываем таймер общего времени работы программы
    const char* filename1; //считываем пути к картинкам
    const char* filename2;
    std::vector<unsigned char> file1; //создаём векторы для будущего содержимого расшифрованных картинок
    std::vector<unsigned char> file2;
    filename1 = argc > 1 ? argv[1] : "";
    filename2 = argc > 2 ? argv[2] : ""; // если имя не задано, то пусто (просто тернарный оператор хотел else)

    const char* output = argc > 3 ? argv[3] : ""; //считываем путь для картинки-результата
    //считываем режим обработки изображения
    const char* mode = argc > 4 ? argv[4] : "";
    // считываем num_threads и num_rounds для установки кол-ва потоков и повторов обработки
    rounds = argc > 5 ? atoi(argv[5]) : 0;

    //загружаем и расшифровываем картинки из файлов в векторы file1 и file2
    auto [w1, h1] = decodePNG(filename1, file1);
    auto [w2, h2] = decodePNG(filename2, file2);

    //если что-то не загрузилось, или картинки не квадратные, или файлы с картинками разных размеров
    if (!w1 || !h1 || !w2 || !h2 || w1 != w2 || h1 != h2 || w1 != h1){
        std::cout << "Error! Image requirements NOT met: " << std::endl;
        printf_s("w1=%i w2=%i \nh1=%i h2=%i\n", w1, w2, h1, h2);
        return -1;
    }
    //если всё успешно загрузилось и все размеры изображений совпали между собой
    // создаём вектор под результат
    std::vector<unsigned char> result(file1.size());
    // определяем режим обработки, заданный пользователем и выполняем ROUNDS раз обработку для выбранного режима
    int command = modes[mode];
    auto processing_start = high_resolution_clock::now(); //перед запуском сохраняем время начала обработки
    switch (command) {
        case MODE_ADD: {
            for (int i=0; i < rounds; i++) {
                add(file1, file2, result);
            }
            break;
        }
        case MODE_SUB: {
            for (int i=0; i < rounds; i++) {
                sub(file1, file2, result);
            }
            break;
        }
        case MODE_EXTRACT_GRAIN: {
            for (int i=0; i < rounds; i++) {
                extract_grain(file1, file2, result);
            }
            break;
        }
        case MODE_MERGE_GRAIN: {
            for (int i=0; i < rounds; i++) {
                merge_grain(file1, file2, result);
            }
            break;
        }
        default: {
            print_help("Wrong mode!");
            std::cout << mode;
            exit (-1);
            break;
        }
    }
    auto processing_stop = high_resolution_clock::now(); //сохраняем время окончания обработки

    // освобождаем ненужные уже исходные файлы - пересоздаём векторы, при этом они освобождают память
    file1 = std::vector<unsigned char>();
    file2 = std::vector<unsigned char>();
    // сохраняем обработанное изображение в файл
    encodePNG(output, result, w1, h1);

    auto overall_stop = high_resolution_clock::now(); // сохраняем время окончания работы программы
    auto overall_time = duration_cast<microseconds>(overall_stop - overall_start); //высчитываем сколько работала программа
    auto processing_time = duration_cast<microseconds>(processing_stop - processing_start); //высчитываем сколько обрабатывались данные
    std::cout << processing_time.count() << " " << overall_time.count() <<  std::endl; //выводим в консоль время работы и обработки (в микросекундах для точности)
}
