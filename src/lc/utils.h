#ifndef LC_UTILS_H
#define LC_UTILS_H

#include <string>
#include <libasr/utils.h>

namespace LCompilers::LC {

void get_executable_path(std::string &executable_path, int &dirname_length);
std::string get_runtime_library_dir();
std::string get_runtime_library_header_dir();
std::string get_runtime_library_c_header_dir();
std::string get_kokkos_dir();
int32_t get_exit_status(int32_t err);

} // LCompilers::LC

#endif // LC_UTILS_H
