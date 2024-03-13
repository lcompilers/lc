#ifndef CLANG_AST_TO_ASR_H
#define CLANG_AST_TO_ASR_H

#include <libasr/asr_utils.h>

namespace LCompilers::LC {
    int dump_clang_ast(std::string infile, std::string ast_dump_file,
        std::string ast_dump_filter, bool ast_list, bool ast_print,
        bool show_clang_ast, bool parse_all_comments);

    int clang_ast_to_asr(Allocator &al, std::string infile,
        LCompilers::ASR::asr_t*& tu, bool parse_all_comments);
} // namespace LCompilers::LC

#endif
