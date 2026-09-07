#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "zstr/zstr.hpp"

int main(int argc, char *argv[]){

    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    // std::cerr << "Logs from your program will appear here!\n";

    
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
    
            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } 
    else if(command == "cat-file"){
        std::string file_name = argv[3];
        std::string file_location = std::string(".git/objects/") + 
                               std::string(file_name.substr(0,2)) + '/' + 
                               std::string(file_name.substr(2));

        zstr::ifstream file(file_location);

        std::string contents((std::istreambuf_iterator<char>(file)), 
                             (std::istreambuf_iterator<char>()));

        size_t header_size = contents.find('\0') + 1;

        cout<< string_view(contents.begin() + header_size + contents.end());
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
