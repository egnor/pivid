#pragma once

#include <memory>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace pivid {

class DisplayError : public std::exception {};

struct DisplayDeviceName {
    std::filesystem::path dev_file; 
    std::string system_path;
};

std::vector<DisplayDeviceName> list_devices();

}  // namespace pivid
