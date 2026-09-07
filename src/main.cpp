#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <cstdio>
#include <zlib.h>
#include <openssl/sha.h>

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
        if (argc < 4) {
            return EXIT_FAILURE;
        }
    
        std::string file_name = argv[3];

        std::string file_location = ".git/objects/" + 
                               file_name.substr(0,2) + "/" + 
                               file_name.substr(2);

        std::ifstream file(file_location, std::ios::binary);

        std::string compressed((std::istreambuf_iterator<char>(file)), 
                             (std::istreambuf_iterator<char>()));

        z_stream stream{};

        stream.next_in = (Bytef*)compressed.data();
        stream.avail_in = compressed.size();

        inflateInit(&stream);

        char buffer[4096];
        std::string contents;

        do{
            stream.next_out = (Bytef*)buffer;
            stream.avail_out = sizeof(buffer);

            inflate(&stream, Z_NO_FLUSH);

            contents.append(
                buffer,
                sizeof(buffer) - stream.avail_out
            );
        } 
        
        while(stream.avail_out == 0);

        inflateEnd(&stream);

        size_t header_size = contents.find('\0') + 1;

        std::cout<< contents.substr(header_size);
    }
    else if(command == "hash-object"){
        if(argc < 4){
            return EXIT_FAILURE;
        }

        std::ifstream file(argv[3], std::ios::binary);

        std::string content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

        std::string header = "blob " + std::to_string(content.size()) + '\0';

        std::string object = header + content;

        unsigned char hash[SHA_DIGEST_LENGTH];

        SHA1(
            reinterpret_cast<const unsigned char*>(object.data()),
            object.size(),
            hash
        );

        std::string hash_string;

        for(int i=0; i<SHA_DIGEST_LENGTH; i++){
            char buffer[3];
            sprintf(buffer, "%02x", hash[i]);
            hash_string += buffer;
        }

        uLong compressed_size = compressBound(object.size());

        std::string compressed(compressed_size, '\0');

        compress(
            reinterpret_cast<Bytef*>(compressed.data()),
            &compressed_size,
            reinterpret_cast<const Bytef*>(object.data()),
            object.size()
        );

        compressed.resize(compressed_size);

        std::string object_dir = ".git/objects/" + hash_string.substr(0, 2);

        std::string object_file = object_dir + "/" + hash_string.substr(2);

        std::filesystem::create_directory(object_dir);

        std::ofstream output(object_file, std::ios::binary);

        output.write(compressed.data(), compressed.size());
        

        std::cout<< hash_string << '\n';
    }
    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
