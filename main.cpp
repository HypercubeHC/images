#include <iostream>
#include <cstdio>
#include <map>
#include "iomanip"
#include "lodepng.cpp"
#include "lodepng.h"
#include <omp.h>
#include <chrono>
using namespace std::chrono;

int threads = 0;
int rounds = 1;

std::tuple<int, int> decodePNG(const char* filename, std::vector<unsigned char> &image) {
    unsigned width, height;
    unsigned error = lodepng::decode(image, width, height, filename);

    if(error) {
        std::cout << filename << " decoder error " << error << ": " << lodepng_error_text(error) << std::endl;
        return {0, 0};
    } else return {width, height};
    //the pixels are now in the vector "image", 4 bytes per pixel, ordered RGBARGBA..., use it as texture, draw it, ...
}

void encodePNG(const char* filename, std::vector<unsigned char> &image, unsigned width, unsigned height) {
    //Encode the image
    unsigned error = lodepng::encode(filename, image, width, height);
    //if there's an error, display it
    if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
}

void add(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();
    #pragma omp parallel for private(i) shared(file1, file2, result, stop) num_threads(threads > 0 ? threads : omp_get_max_threads()) default(none)
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //no need to alter alpha-channel, it makes picture void
            result[i] = 255;
            continue;
        }
        result[i] = std::min((int(file1[i]) + int(file2[i])), 255);
    }
}

void sub(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();
    #pragma omp parallel for private(i) shared(file1, file2, result, stop) num_threads(threads > 0 ? threads : omp_get_max_threads()) default(none)
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //no need to alter alpha-channel, it makes picture void
            result[i] = 255;
            continue;
        }
        result[i] = std::max((int(file1[i]) - int(file2[i])), 0);
    }
}

void extract_grain(const std::vector<unsigned char>&file1, const std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();
    #pragma omp parallel for private(i) shared(file1, file2, result, stop) num_threads(threads > 0 ? threads : omp_get_max_threads()) default(none)
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //no need to alter alpha-channel, it makes picture void
            result[i] = 255;
            continue;
        }
        result[i] = std::clamp((int(file1[i]) - int(file2[i])) + 128, 0, 255);
    }
}

void merge_grain(std::vector<unsigned char>&file1, std::vector<unsigned char>&file2, std::vector<unsigned char>&result){
    std::vector<int>::size_type i;
    std::vector<int>::size_type stop = file1.size();
    #pragma omp parallel for private(i) shared(file1, file2, result, stop) num_threads(threads > 0 ? threads : omp_get_max_threads()) default(none)
    for(i = 0; i != stop; i++) {
        if ((i+1) % 4 == 0) { //no need to alter alpha-channel, it makes picture void
            result[i] = 255;
            continue;
        }
        result[i] = std::clamp(int(file1[i]) + int(file2[i]) - 128, 0, 255);
    }
}

enum Modes {
    MODE_ADD,
    MODE_SUB,
    MODE_EXTRACT_GRAIN,
    MODE_MERGE_GRAIN
};

static std::map<std::string, int> modes = {
        {"add",   MODE_ADD},
        {"sub",   MODE_SUB},
        {"extract",  MODE_EXTRACT_GRAIN},
        {"merge", MODE_MERGE_GRAIN}
};

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
    std::cout << "\tnum_threads: default = unlimited \n"
                 "\tnum_rounds: default = 1" << std::endl;
    std::cout << "\nOnly PNG images with 1:1 aspect ratio are accepted" << std::endl;
}


int main(int argc, char *argv[]) {
    if (argc < 5 || *argv[1] == 'h'){ //print usage help and exit: program is not supposed to guess user input
        print_help();
        return 0;
    }
    auto overall_start = high_resolution_clock::now();
    const char* filename1;
    const char* filename2;
    std::vector<unsigned char> file1;
    std::vector<unsigned char> file2;
    filename1 = argc > 1 ? argv[1] : "";
    filename2 = argc > 2 ? argv[2] : "";

    const char* output = argc > 3 ? argv[3] : "";
    //image processing mode
    const char* mode = argc > 4 ? argv[4] : "";
    //num_threads and num_rounds for benchmark
    threads = argc > 5 ? atoi(argv[5]) : 0;
    rounds = argc > 6 ? atoi(argv[6]) : 1;

    //load and decode images directly from files
    auto [w1, h1] = decodePNG(filename1, file1);
    auto [w2, h2] = decodePNG(filename2, file2);

    //if everything is loaded and images' sizes are equal
    if (!w1 || !h1 || !w2 || !h2 || w1 != w2 || h1 != h2 || w1 != h1){
        std::cout << "Error! Image requirements NOT met: " << std::endl;
        printf_s("w1=%i w2=%i \nh1=%i h2=%i\n", w1, w2, h1, h2);
        return -1;
    }
    // now we create a result vector
    std::vector<unsigned char> result(file1.size());
    //figure out what's supposed to do
    int command = modes[mode];
    auto processing_start = high_resolution_clock::now();
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
    auto processing_stop = high_resolution_clock::now();

    //free unused data - we're trying to keep memory free
    file1 = std::vector<unsigned char>();
    file2 = std::vector<unsigned char>();
    //save completed job
    encodePNG(output, result, w1, h1);
    //stop benchmarking, output score
    auto overall_stop = high_resolution_clock::now();
    auto overall_time = duration_cast<microseconds>(overall_stop - overall_start);
    auto processing_time = duration_cast<microseconds>(processing_stop - processing_start);
    std::cout << processing_time.count() << " " << overall_time.count() <<  std::endl;
}
