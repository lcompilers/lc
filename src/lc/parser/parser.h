#ifndef LC_PARSER_PARSER_H
#define LC_PARSER_PARSER_H

#include <libasr/containers.h>
#include <libasr/diagnostics.h>
#include <lc/parser/tokenizer.h>

namespace LCompilers::LC {

class Parser
{
public:
    std::string inp;

public:
    diag::Diagnostics &diag;
    Allocator &m_a;
    Tokenizer m_tokenizer;
    Vec<LC::AST::stmt_t*> result;
    Vec<LC::AST::type_ignore_t*> type_ignore;

    Parser(Allocator &al, diag::Diagnostics &diagnostics)
            : diag{diagnostics}, m_a{al} {
        result.reserve(al, 32);
        type_ignore.reserve(al, 4);
    }

    void parse(const std::string &input, uint32_t prev_loc);
    void handle_yyerror(const Location &loc, const std::string &msg);
};


// Parses Python code to AST
Result<LC::AST::Module_t*> parse(Allocator &al,
    const std::string &s, uint32_t prev_loc,
    diag::Diagnostics &diagnostics);

Result<LC::AST::ast_t*> parse_python_file(Allocator &al,
        const std::string &runtime_library_dir,
        const std::string &infile,
        diag::Diagnostics &diagnostics,
        uint32_t prev_loc, bool new_parser);

} // namespace LCompilers::LC

#endif
