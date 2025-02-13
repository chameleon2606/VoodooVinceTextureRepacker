﻿#include "Repacking.h"
#include <iostream>
#include <filesystem>
#include <imgui.h>
#include <thread>

using json = nlohmann::json;
using namespace std;

static vector<uint8_t> int_to_bytes(const uint32_t value) {
    vector<uint8_t> bytes(4);
    for (int i = 0; i < 4; i++) {
        bytes[i] = static_cast<uint8_t>(value >> (8 * i));
    }
    return bytes;
}

static void create_padding(vector<char>* char_list, const unsigned long* offset)
{
    char_list->insert(char_list->end(), (ceil(*offset / 128.0f) * 128) - *offset, 0x00);
}

void conver_wav(const string& wav_file) {
    ifstream inputFile(SOURCE_FILES_PATH+wav_file,ios::binary);
    if (!inputFile.is_open()) {
        cerr << "Error opening file\n";
        return;
    }
    inputFile.seekg(20, ios::beg);
    unsigned char byte;
    inputFile.read(reinterpret_cast<char*>(&byte),1);
    if (byte!=0x01) {
        cerr << "not pcm\n";
        return;
    }

    inputFile.seekg(64, ios::beg);
    vector<char> buffer(4);
    inputFile.read(buffer.data(), buffer.size());
    string buffer_string;
    for (const char byte1 : buffer) {
        buffer_string += byte1;
    }
    if(buffer_string == "data") {
        // this should be a file that's already good to use
        return;
    }

    cout << "changing file " << wav_file << "\n";

    inputFile.seekg(0, ios::beg);
    vector<char> start_of_file(36);  // wav files exported with audacity will always have a header size of 36
    inputFile.read(start_of_file.data(), start_of_file.size());

    inputFile.seekg(0, ios::end);
    const long long file_size = inputFile.tellg();

    vector<char> rest_of_file(file_size-36);
    inputFile.seekg(36, ios::beg);
    inputFile.read(rest_of_file.data(), rest_of_file.size());
    constexpr char header[28] = {'P', 'A', 'A', 'D', 0x14, 0x00, 0x00, 0x00};

    fstream new_file(SOURCE_FILES_PATH+wav_file, ios::in | ios::out | ios::binary | ios::trunc);
    if (!new_file) {
        cerr << "Error creating new file\n";
        return;
    }

    new_file.write(start_of_file.data(), start_of_file.size());
    new_file.write(header, sizeof(header));
    new_file.write(rest_of_file.data(), rest_of_file.size());
}

static auto get_level_data(const string& data) {
    return jsonData[currentLevel][data];
}

vector<string> get_level_list() {
    vector<string> keys;
    for (auto& [key, value] : jsonData.items()) {
        keys.push_back(key);
    }
    return keys;
}

static vector<char> construct_raw_data() {
    // todo claculate size of raw_data first
    vector<char> raw_data;
    data_index.clear();
    for (const string &file : level_file_list) {
        string src_path;
        if (file.starts_with("lightmap")) {
            src_path = SOURCE_FILES_PATH+"/lightmaps/";
            src_path += currentLevel+"/";
        }
        else {
            src_path = SOURCE_FILES_PATH;
        }
        ifstream src_file(src_path+file, ios::binary);

        if (!src_file.is_open()) {
            cerr << src_path+file << " does not exist!\n";
            exit(-1);
        }
        data_index.push_back(raw_data.size());
        src_file.seekg(0, ios::end);  // seek to the end to calculate the file size
        streamsize data_size = src_file.tellg();  // the position the cursor is at symbolizes the size of the raw data
        data_size -= SRC_FILE_HEADER_SIZE;
        raw_data.resize(raw_data.size() + data_size);
        src_file.seekg(SRC_FILE_HEADER_SIZE, ios::beg);
        src_file.read(raw_data.data() + data_index.back(), data_size);
        const unsigned long offset = raw_data.size();
        create_padding(&raw_data, &offset);
    }
    return raw_data;
}

vector<char> construct_filenames() {
    vector<char> charlist;
    for (const auto& file : level_file_list) {
        const char* filename_bytes = file.c_str();
        const int byte_length = strlen(filename_bytes);
        int filename_byte_remainder = -byte_length - 4 * static_cast<int>(floor(static_cast<double>(-byte_length) / 4));

        if (filename_byte_remainder == 0) {
            filename_byte_remainder = 4;
        }

        charlist.insert(charlist.end(), filename_bytes, filename_bytes+byte_length);

        charlist.insert(charlist.end(), filename_byte_remainder, 0x00);
    }

    return charlist;
}

vector<char> construct_file_metadata() {
    nlohmann::basic_json<> mystery_number_array;
    if (repackingTextures)
    {
        mystery_number_array = get_level_data("mystery_dds_numbers");
    }
    else
    {
        mystery_number_array = get_level_data("mystery_wav_numbers");
    }
    vector<char> file_info_table;
    //vector<char> dds_header_size_in_bytes(4);
    vector<uint8_t> header_bytes = int_to_bytes(SRC_FILE_HEADER_SIZE);
    const unsigned long level_list_number = level_file_list.size();
    const unsigned long headers_offset = HOT_HEADER_SIZE + ((METADATA_SIZE * level_list_number) - 8) + filename_table.size();
    const unsigned long previous_data = headers_offset + file_headers_size;

    for (int i = 0; i < level_list_number; i++) {
        string src_path;
        if (level_file_list[i].starts_with("lightmap")) {
            src_path = SOURCE_FILES_PATH+"/lightmaps/";
            src_path += currentLevel+"/";
        }
        else {
            src_path = SOURCE_FILES_PATH;
        }
        file_info_table.insert(file_info_table.end(), header_bytes.begin(), header_bytes.end());  // header size
        vector<uint8_t> this_header_offset = int_to_bytes(headers_offset + (SRC_FILE_HEADER_SIZE * i));
        file_info_table.insert(file_info_table.end(), this_header_offset.begin(), this_header_offset.end());  // header offset
        vector<uint8_t> file_size = int_to_bytes(filesystem::file_size(src_path + level_file_list[i]));
        file_info_table.insert(file_info_table.end(), file_size.begin(), file_size.end());  // file size
        file_info_table.insert(file_info_table.end(), 4, 0x00);  // blank
        vector<uint8_t> file_offset = int_to_bytes(previous_data + data_index[i]);  // raw file offset
        file_info_table.insert(file_info_table.end(), file_offset.begin(), file_offset.end());
        file_info_table.insert(file_info_table.end(), 4, 0x00);  // blank
        if (i < level_list_number - 1) {
            vector<uint8_t> mystery_number = int_to_bytes(mystery_number_array[i]);
            file_info_table.insert(file_info_table.end(), mystery_number.begin(), mystery_number.end());  // mystery number
            file_info_table.insert(file_info_table.end(), 4, 0x00);  // blank
        }
    }
    return file_info_table;
}

vector<char> construct_file_headers() {
    // todo extract the header and the raw data at once
    vector<char> file_headers;
    for (int i = 0; i < level_file_list.size(); i++) {
        string src_path;
        if (level_file_list[i].starts_with("lightmap")) {
            src_path = SOURCE_FILES_PATH+"/lightmaps/";
            src_path += currentLevel+"/";
        }
        else {
            src_path = SOURCE_FILES_PATH;
        }
        ifstream src_file(src_path+level_file_list[i]);
        vector<char> buffer(SRC_FILE_HEADER_SIZE);
        src_file.read(buffer.data(), SRC_FILE_HEADER_SIZE);
        file_headers.insert(file_headers.end(), buffer.begin(), buffer.end());
    }
    raw_data_header_index = file_headers.size();
    const unsigned long offset = HOT_HEADER_SIZE + ((METADATA_SIZE * level_file_list.size()) - 8) + filename_table.size() + (SRC_FILE_HEADER_SIZE * level_file_list.size()) + file_headers.size();
    create_padding(&file_headers, &offset);
    return file_headers;
}

vector<char> construct_header() {
    vector<char> header;
    char signature[8] = {'H','O','T',' ', 0x01, 0x00, 0x00, 0x00};  // header magic + version
    header.insert(header.end(), signature, signature+8);
    vector<uint8_t> value = int_to_bytes(HOT_HEADER_SIZE + metadata.size() + filename_table.size());  // start of headers
    header.insert(header.end(), value.begin(), value.end());
    vector<uint8_t> value2 = int_to_bytes(HOT_HEADER_SIZE + ((METADATA_SIZE * level_file_list.size()) - 8)
        + filename_table.size() + raw_data_header_index);
    header.insert(header.end(), value2.begin(), value2.end());
    vector<uint8_t> value3 = int_to_bytes(HOT_HEADER_SIZE + metadata.size() + filename_table.size() + file_headers_size + raw_data_size);  // total size
    header.insert(header.end(), value3.begin(), value3.end());
    vector<uint8_t> value4 = int_to_bytes(HOT_HEADER_SIZE + metadata.size());  // writes the offset of the filename table
    header.insert(header.end(), value4.begin(), value4.end());
    vector<uint8_t> value5 = int_to_bytes(level_file_list.size());  // writes the amount of textures or sounds
    header.insert(header.end(), value5.begin(), value5.end());

    return header;
}

void validate_directory() {
    outputPath = game_path_input;
    if (!filesystem::exists(outputPath + "\\vincedata\\")) {
        cerr << "Invalid game folder\n";
        exit(-1);
    }

    outputPath += "\\vincedata\\";

    if (IS_DEBUG) {
        for (const string& area : get_level_list()) {
            if (!filesystem::exists(jsonData[area]["path"])) {
                filesystem::create_directories(outputPath + string(jsonData[area]["path"]));
            }
        }
    }
}

int get_file_amount(const string& prefix)
{
    int count = 0;
    for (const auto& [key, value] : jsonData.items())
    {
        if (key.starts_with(prefix))
        {
            count++;
        }
    }
    return count;
}

void repack()
{
    validate_directory();
    isRepacking = true;
    float i = 0;
    startTime = chrono::high_resolution_clock::now();
    for (const string& area : get_level_list()) {
        level_file_list.clear();
        if (repackingTextures && !area.starts_with("area")) {
            continue;
        }
        currentLevel = area;

        if (!filesystem::exists(outputPath + string(get_level_data("path")))) {
            cerr << "Invalid folder\n";
            exit(-1);
        }
        
        fstream hot(outputPath + string(get_level_data("path")) + file_type + ".hot", ios::in | ios::out | ios::binary | ios::trunc);

        level_file_list = get_level_data(file_type);
        if (!repackingTextures)
        {
            for (const string& file : level_file_list)
            {
                conver_wav(file);
            }
        }

        raw_data_bytes = construct_raw_data();
        raw_data_size = raw_data_bytes.size();
        filename_table = construct_filenames();

        vector<char> header_placeholder;
        header_placeholder.insert(header_placeholder.end(), HOT_HEADER_SIZE, 0x00);
        hot.write(header_placeholder.data(), header_placeholder.size());

        vector<char> file_headers = construct_file_headers();
        file_headers_size = file_headers.size();

        metadata = construct_file_metadata();
        hot.write(metadata.data(), metadata.size());

        hot.write(filename_table.data(), filename_table.size());
        hot.write(file_headers.data(), file_headers_size);
        hot.write(raw_data_bytes.data(), raw_data_bytes.size());

        vector<char> header_bytes = construct_header();
        hot.seekg(0, ios::beg);
        hot.write(header_bytes.data(), header_bytes.size());

        i++;
        level_progress = (i - 0.0f) / (static_cast<float>(max_progress_value) - 0.0f);
    }
    isRepacking = false;
}

void init()
{
    repacking_mode = "texture";
    SOURCE_FILES_PATH = filesystem::current_path().string() + TEXTURE_PATH;
    valid_source_dir = filesystem::exists(SOURCE_FILES_PATH);
    SRC_FILE_HEADER_SIZE = DDS_HEADER_SIZE;
    repackingTextures = true;
    
    filesystem::path source_files_path = filesystem::current_path() / "source files";
    string source_files_path_str = source_files_path.string();
    strncpy_s(source_files_path_input, source_files_path_str.c_str(), IM_ARRAYSIZE(source_files_path_input) - 1);
    source_files_path_input[IM_ARRAYSIZE(source_files_path_input) - 1] = '\0';

    max_progress_value = get_file_amount("area");
}

void update()
{
    ImGui::Text("Chameleon's texture and sound repacker");
    ImGui::Dummy(ImVec2(0, 20));

    if (isRepacking) ImGui::BeginDisabled();
    
    if (ImGui::InputText("Source files folder", source_files_path_input, IM_ARRAYSIZE(source_files_path_input)))
    {
        SOURCE_FILES_PATH = source_files_path_input;
        valid_source_dir = filesystem::exists(SOURCE_FILES_PATH + "\\textures");
        if (repackingTextures)
        {
            SOURCE_FILES_PATH = source_files_path_input;
            SOURCE_FILES_PATH += "\\textures\\";
        }
        else
        {
            SOURCE_FILES_PATH = source_files_path_input;
            SOURCE_FILES_PATH += "\\sounds\\";
        }
    }
    
    if (ImGui::InputText("Game folder path", game_path_input, IM_ARRAYSIZE(game_path_input)))
    {
        outputPath = string(game_path_input);
        valid_game_dir = filesystem::exists(outputPath + "\\vincedata\\");
    }
    
    ImGui::Dummy(ImVec2(0,30));
    
    if (ImGui::RadioButton("textures", &e, 0))
    {
        repacking_mode = "textures";
        SRC_FILE_HEADER_SIZE = DDS_HEADER_SIZE;
        SOURCE_FILES_PATH = filesystem::current_path().string() + TEXTURE_PATH;
        repackingTextures = true;
        file_type = "textures";
        max_progress_value = get_file_amount("area");
    }
    if (ImGui::RadioButton("sounds", &e, 1))
    {
        repacking_mode = "sounds";
        SRC_FILE_HEADER_SIZE = WAV_HEADER_SIZE;
        SOURCE_FILES_PATH = filesystem::current_path().string() + SOUNDS_PATH;
        repackingTextures = false;
        file_type = "sounds";
        max_progress_value = jsonData.size();
    }
    
    ImGui::Dummy(ImVec2(0,30));

    if (!valid_game_dir || !valid_source_dir)ImGui::BeginDisabled();
    if (ImGui::Button("Repack"))
    {
        thread taskThread(repack);
        taskThread.detach();
    }
    if (!valid_game_dir || !valid_source_dir)ImGui::EndDisabled();

    if (isRepacking) ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0,30));
    
    ImGui::ProgressBar(level_progress, ImVec2(-1.0f, 0.0f));
    ImGui::SameLine();
    ImGui::Text("level progress");

    if (isRepacking)
    {
        endTime = chrono::high_resolution_clock::now();
        duration = chrono::duration_cast<chrono::seconds>(endTime - startTime);
    }
    
    ImGui::Text("repacked in %d seconds", duration);
    //ImGui::Dummy(ImVec2(0,30));
    //ImGui::Text("SOURCE FILES PATH = %s",SOURCE_FILES_PATH.c_str());
    //ImGui::Text("SRC FILE HEADER SIZE = %d",SRC_FILE_HEADER_SIZE);
    //ImGui::Text("repacking mode = %s",repacking_mode.c_str());
    //ImGui::Text("file type = %s",file_type.c_str());
}