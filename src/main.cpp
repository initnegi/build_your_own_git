#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <zlib.h>
#include <openssl/sha.h>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <map>
#include <set>
#include <ctime>
#include <sys/wait.h>


struct TreeEntry {
    std::string name;
    std::string mode;
    std::string sha;
};


struct PackObject {
    int type;
    std::string data;
};

struct PackParser {
    const std::string& data;
    size_t pos;

    PackParser(const std::string& data) : data(data), pos(0) {}

    unsigned char readByte() {

        if(pos >= data.size()) {
            throw std::runtime_error("Unexpected end of packfile");
        }

        return static_cast<unsigned char>(data[pos++]);
    }

    std::string readBytes(size_t count) {

        if(pos + count > data.size()) {
            throw std::runtime_error("Unexpected end of packfile");
        }

        std::string result = data.substr(pos, count);

        pos += count;

        return result;
    }
};

void parsePackHeader(PackParser& parser, uint32_t& objectCount) {

    std::string signature = parser.readBytes(4);

    if(signature != "PACK") {
        throw std::runtime_error("Invalid packfile signature");
    }

    std::string versionBytes = parser.readBytes(4);

    uint32_t version =
        (static_cast<uint32_t>(
            static_cast<unsigned char>(versionBytes[0])
        ) << 24) |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(versionBytes[1])
        ) << 16) |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(versionBytes[2])
        ) << 8) |
        static_cast<uint32_t>(
            static_cast<unsigned char>(versionBytes[3])
        );

    if(version != 2 && version != 3) {
        throw std::runtime_error("Unsupported packfile version");
    }

    std::string countBytes = parser.readBytes(4);

    objectCount =
        (static_cast<uint32_t>(
            static_cast<unsigned char>(countBytes[0])
        ) << 24) |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(countBytes[1])
        ) << 16) |
        (static_cast<uint32_t>(
            static_cast<unsigned char>(countBytes[2])
        ) << 8) |
        static_cast<uint32_t>(
            static_cast<unsigned char>(countBytes[3])
        );
}


std::string sha1Hex(const std::string& data) {

    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(
        reinterpret_cast<const unsigned char*>(data.data()),
        data.size(),
        hash
    );

    std::string result;

    for(int i = 0; i < SHA_DIGEST_LENGTH; i++) {

        char buffer[3];

        sprintf(buffer, "%02x", hash[i]);

        result += buffer;
    }

    return result;
}

void writeGitObject(const std::filesystem::path& gitDir, const std::string& type, const std::string& content){

    std::string header = type + " " + std::to_string(content.size()) + '\0';

    std::string object = header + content;

    std::string hash = sha1Hex(object);

    uLong compressedSize = compressBound(object.size());

    std::string compressed(compressedSize, '\0');

    compress(
        reinterpret_cast<Bytef*>(compressed.data()),
        &compressedSize,
        reinterpret_cast<const Bytef*>(object.data()),
        object.size()
    );

    compressed.resize(compressedSize);

    std::filesystem::path objectDir = gitDir / "objects" / hash.substr(0, 2);

    std::filesystem::create_directories(objectDir);

    std::filesystem::path objectFile = objectDir / hash.substr(2);

    if(!std::filesystem::exists(objectFile)) {

        std::ofstream output(objectFile, std::ios::binary);

        output.write(compressed.data(), compressed.size());
    }
}

std::string runCurl(const std::string& url, const std::string& extraArgs = ""){

    std::string command = "curl -L -sS --fail " + extraArgs + " \"" + url + "\"";


    FILE* pipe = popen(command.c_str(), "r");

    if(!pipe) {
        throw std::runtime_error("Failed to run curl");
    }

    char buffer[8192];

    std::string result;

    while(true) {

        size_t n = fread(
            buffer,
            1,
            sizeof(buffer),
            pipe
        );

        if(n > 0) {
            result.append(buffer, n);
        }

        if(n < sizeof(buffer)) {
            break;
        }
    }

    int status = pclose(pipe);

    if(status != 0) {
        throw std::runtime_error("curl failed");
    }

    return result;
}

std::string getGitRefs(const std::string& repoUrl) {

    std::string url = repoUrl;

    while(!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    url += "/info/refs?service=git-upload-pack";

    return runCurl(url, "-H \"Accept: application/x-git-upload-pack-advertisement\"");
}

std::vector<std::string> parsePktLines(const std::string& data) {

    std::vector<std::string> packets;

    size_t position = 0;

    while(position + 4 <= data.size()) {

        std::string lengthString = data.substr(position, 4);

        unsigned int length = std::stoul(lengthString, nullptr, 16);

        position += 4;

        if(length == 0 || length == 2) {
            continue;
        }

        if(length == 1) {
            break;
        }

        if(length < 4 ||
           position + length - 4 > data.size()) {
            break;
        }

        packets.push_back(data.substr(position, length - 4));

        position += length - 4;
    }

    return packets;
}

std::string makePktLine(const std::string& content){

    size_t length = content.size() + 4;

    char buffer[5];

    sprintf(buffer, "%04zx", length);

    return std::string(buffer) + content;
}

std::string requestPack(const std::string& repoUrl, const std::string& headSha){
    std::string url = repoUrl;

    while(!url.empty() && url.back() == '/') {
        url.pop_back();
    }

    url += "/git-upload-pack";

    std::string request;

    request += makePktLine("want " + headSha + "\n");

    request += "0000";

    request += makePktLine("done\n");

    std::filesystem::path requestFile =
        std::filesystem::temp_directory_path() /
        ("mini_git_upload_pack_" + std::to_string(std::time(nullptr)) + ".bin");

    {
        std::ofstream output(requestFile,std::ios::binary);

        if(!output) {
            throw std::runtime_error(
                "Could not create temporary request file"
            );
        }

        output.write(request.data(), request.size());
    }

    std::string command =
        "curl -L -sS "
        "-X POST "
        "-H \"Content-Type: application/x-git-upload-pack-request\" "
        "-H \"Accept: application/x-git-upload-pack-result\" "
        "--data-binary @\"" +
        requestFile.string() + "\" \"" + url + "\"";

    FILE* pipe = popen(command.c_str(), "r");

    if(!pipe) {
        std::filesystem::remove(requestFile);

        throw std::runtime_error(
            "Failed to run curl"
        );
    }

    std::string result;

    char buffer[8192];

    while(true) {

        size_t n = fread(
            buffer,
            1,
            sizeof(buffer),
            pipe
        );

        if(n > 0) {
            result.append(buffer, n);
        }

        if(n < sizeof(buffer)) {
            break;
        }
    }

    int status = pclose(pipe);

    std::filesystem::remove(requestFile);

    if(status != 0) {
        throw std::runtime_error(
            "git-upload-pack request failed"
        );
    }

    if(result.empty()) {
        throw std::runtime_error(
            "git-upload-pack returned empty response"
        );
    }

    return result;
}

std::string getHeadSha(const std::string& refs){

    std::vector<std::string> packets = parsePktLines(refs);

    for(const std::string& packet : packets) {

        size_t space = packet.find(' ');

        if(space == std::string::npos) {
            continue;
        }

        std::string sha = packet.substr(0, space);

        size_t nul = packet.find('\0');

        std::string ref;

        if(nul != std::string::npos) {
            ref = packet.substr(nul + 1);
        }
        else {
            ref = packet.substr(space + 1);
        }

        if(ref == "HEAD" ||
           ref.find("HEAD") != std::string::npos) {

            return sha;
        }
    }

    for(const std::string& packet : packets) {

        size_t space = packet.find(' ');

        if(space == std::string::npos) {
            continue;
        }

        std::string sha = packet.substr(0, space);

        size_t nul = packet.find('\0');

        if(nul == std::string::npos) {
            continue;
        }

        std::string ref = packet.substr(nul + 1);

        if(ref.find("refs/heads/main") != std::string::npos) {
            return sha;
        }

        if(ref.find("refs/heads/master") != std::string::npos) {
            return sha;
        }
    }

    throw std::runtime_error("Could not find repository HEAD");
}

uint64_t readPackSize(PackParser& parser, unsigned char firstByte) {

    uint64_t size = firstByte & 0x0f;

    int shift = 4;

    unsigned char byte = firstByte;

    while(byte & 0x80) {

        byte = parser.readByte();

        size |= static_cast<uint64_t>(byte & 0x7f) << shift;

        shift += 7;
    }

    return size;
}

std::string inflatePackObject(PackParser& parser, size_t expectedSize){

    z_stream stream{};

    if(inflateInit(&stream) != Z_OK) {
        throw std::runtime_error(
            "inflateInit failed"
        );
    }

    std::string result;

    char outputBuffer[8192];

    while(true) {

        if(parser.pos >= parser.data.size()) {
            inflateEnd(&stream);

            throw std::runtime_error(
                "Unexpected end of compressed object"
            );
        }

        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(parser.data.data() + parser.pos));

        stream.avail_in = static_cast<uInt>(parser.data.size() - parser.pos);

        stream.next_out = reinterpret_cast<Bytef*>(outputBuffer);

        stream.avail_out = sizeof(outputBuffer);

        int ret = inflate(&stream, Z_NO_FLUSH);

        size_t consumed = (parser.data.size() - parser.pos) - stream.avail_in;

        parser.pos += consumed;

        result.append(outputBuffer, sizeof(outputBuffer) - stream.avail_out);

        if(ret == Z_STREAM_END) {
            break;
        }

        if(ret != Z_OK) {

            inflateEnd(&stream);

            throw std::runtime_error(
                "Failed to inflate pack object"
            );
        }
    }

    inflateEnd(&stream);

    if(expectedSize != 0 &&
       result.size() != expectedSize) {

        throw std::runtime_error(
            "Pack object size mismatch"
        );
    }

    return result;
}

std::string writeCommit(const std::string& tree_sha, const std::string& parent_sha, const std::string& message){
    
    std::string author = "John Doe <john@example.com> 1234567890 +0000";

    std::string committer = "John Doe <john@example.com> 1234567890 +0000";

    std::string commit_content;


    commit_content += "tree " + tree_sha + "\n";

    if(!parent_sha.empty()) {
        commit_content += "parent " + parent_sha + "\n";
    }

    commit_content += "author " + author + "\n";

    commit_content += "committer " + committer + "\n";
    commit_content += "\n";
    commit_content += message + "\n";



    
    std::string header = "commit " + std::to_string(commit_content.size()) + '\0';

    std::string object = header + commit_content;

    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(
        reinterpret_cast<const unsigned char*>(object.data()),
        object.size(),
        hash
    );

    std::string hash_string;

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
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

    std::filesystem::create_directories(object_dir);

    std::ofstream output(object_file, std::ios::binary);

    output.write(compressed.data(), compressed.size());

    return hash_string;
}

std::string writeTree(const std::filesystem::path& directory) {

    std::vector<TreeEntry> entries;

    for(const auto& entry : std::filesystem::directory_iterator(directory)) {

        if(entry.path().filename() == ".git") {
            continue;
        }

        std::string name = entry.path().filename().string();

        if(entry.is_directory()) {

            std::string sha = writeTree(entry.path());

            entries.push_back({name, "40000", sha});

        }
        else if(entry.is_regular_file()) {

            std::ifstream file(entry.path(), std::ios::binary);

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

            for(int i = 0; i < SHA_DIGEST_LENGTH; i++){
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

            entries.push_back({name, "100644", hash_string});
        }
    }

    std::sort(entries.begin(), entries.end(), 
            [](const TreeEntry& a, const TreeEntry& b) {
                return a.name < b.name;
            }
    );

    std::string tree_content;

    for(const auto& entry : entries) {

        tree_content += entry.mode + " " + entry.name + '\0';

        for(size_t i = 0; i < entry.sha.size(); i += 2) {

            unsigned char byte = std::stoi(entry.sha.substr(i, 2), nullptr, 16);

            tree_content += byte;
        }
    }

    std::string header =
        "tree " + std::to_string(tree_content.size()) + '\0';

    std::string object = header + tree_content;

    unsigned char hash[SHA_DIGEST_LENGTH];

    SHA1(
        reinterpret_cast<const unsigned char*>(object.data()),
        object.size(),
        hash
    );

    std::string hash_string;

    for(int i = 0; i < SHA_DIGEST_LENGTH; i++) {
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

    return hash_string;
}

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
    else if(command == "ls-tree"){
        if(argc < 4){
            return EXIT_FAILURE;
        }

        std::string file_name = argv[3];

        std::string file_location = ".git/objects/" + file_name.substr(0,2) + "/" + file_name.substr(2);

        std::ifstream file(file_location, std::ios::binary);

        std::string compressed(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );

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

        size_t position = contents.find('\0') + 1;

        while(position < contents.size()){

            size_t null_pos = contents.find('\0', position);

            std::string entry = contents.substr(
                position,
                null_pos - position
            );

            size_t space = entry.find(' ');

            std::string name = entry.substr(space + 1);

            std::cout << name << '\n';

            position = null_pos + 1 + 20;
        }
    }
    else if(command == "write-tree"){
        std::string hash = writeTree(".");
        std::cout << hash << '\n';
    }
    else if(command == "commit-tree") {
        if(argc < 7) {
            return EXIT_FAILURE;
        }

        std::string tree_sha = argv[2];

        std::string parent_sha;
        std::string message;

        for(int i = 3; i < argc; i++) {

            if(std::string(argv[i]) == "-p") {

                parent_sha = argv[i + 1];
                i++;

            }
            else if(std::string(argv[i]) == "-m") {

                message = argv[i + 1];
                i++;
            }
        }

        std::string hash =
            writeCommit(
                tree_sha,
                parent_sha,
                message
            );

        std::cout << hash << '\n';
    }
    else if(command == "clone") {
        if(argc < 4) {
            std::cerr << "Usage: clone <repository-url> <directory>\n";
            return EXIT_FAILURE;
        }

        std::string repoUrl = argv[2];
        std::filesystem::path targetDir = argv[3];

        try {
            cloneRepository(repoUrl, targetDir);
        }
        catch(const std::exception& e) {
            std::cerr << "Clone failed: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}
