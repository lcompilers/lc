#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/ASTConsumers.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Driver/Options.h>

#include <libasr/pickle.h>
#include <libasr/pass/intrinsic_array_function_registry.h>
#include <lc/clang_ast_to_asr.h>

#include <set>

namespace LCompilers {

enum SpecialFunc {
    Printf,
    Exit,
    View,
    Shape,
    Empty,
    Fill,
    All,
    Any,
    NotEqual,
    Equal,
    Exp,
    Abs,
    Sin,
    Cos,
    AMax,
    AMin,
    Sum,
    Range,
    Size,
    Pow,
    Reshape,
    Iota,
    Sqrt,
    PushBack,
    Clear,
    Data,
    Reserve,

    TorchOnes,
    TorchEmpty,
    TorchFromBlob,
    TorchTensorItem,
};

std::map<std::string, SpecialFunc> special_function_map = {
    {"printf", SpecialFunc::Printf},
    {"exit", SpecialFunc::Exit},
    {"view", SpecialFunc::View},
    {"submdspan", SpecialFunc::View},
    {"shape", SpecialFunc::Shape},
    {"extent", SpecialFunc::Shape},
    {"empty", SpecialFunc::Empty},
    {"fill", SpecialFunc::Fill},
    {"all", SpecialFunc::All},
    {"full_extent", SpecialFunc::All},
    {"any", SpecialFunc::Any},
    {"not_equal", SpecialFunc::NotEqual},
    {"equal", SpecialFunc::Equal},
    {"exp", SpecialFunc::Exp},
    {"abs", SpecialFunc::Abs},
    {"sin", SpecialFunc::Sin},
    {"cos", SpecialFunc::Cos},
    {"amax", SpecialFunc::AMax},
    {"amin", SpecialFunc::AMin},
    {"sum", SpecialFunc::Sum},
    {"range", SpecialFunc::Range},
    {"size", SpecialFunc::Size},
    {"pow", SpecialFunc::Pow},
    {"reshape", SpecialFunc::Reshape},
    {"operator\"\"i", SpecialFunc::Iota},
    {"sqrt", SpecialFunc::Sqrt},
    {"push_back", SpecialFunc::PushBack},
    {"clear", SpecialFunc::Clear},
    {"data", SpecialFunc::Data},
    {"reserve", SpecialFunc::Reserve},

    {"torch::ones", SpecialFunc::TorchOnes},
    {"torch::empty", SpecialFunc::TorchEmpty},
    {"torch::from_blob", SpecialFunc::TorchFromBlob},
    {"torch::abs", SpecialFunc::Abs},
    {"torch::any", SpecialFunc::Any},
    {"item", SpecialFunc::TorchTensorItem},
};

class OneTimeUseString {

    private:

        std::string value;

    public:

        OneTimeUseString(): value{""} {}

        std::string get() {
            std::string value_ = value;
            value = "";
            return value_;
        }

        void set(std::string value_) {
            value = value_;
        }
};

class OneTimeUseBool {

    private:

        bool value;

    public:

        OneTimeUseBool(): value{false} {}

        bool get() {
            bool value_ = value;
            value = false;
            return value_;
        }

        void set(bool value_) {
            value = value_;
        }
};

template <typename T>
class OneTimeUseASRNode {

    private:

        T* node;

    public:

        OneTimeUseASRNode(): node{nullptr} {}

        T* get() {
            T* node_ = node;
            node = nullptr;
            return node_;
        }

        void operator=(T* other) {
            node = other;
        }

        bool operator!=(T* other) {
            return node != other;
        }

        bool operator==(T* other) {
            return node == other;
        }

};

enum ThirdPartyCPPArrayTypes {
    XTensorArray,
    MDSpanArray,
    PyTorchArray,
};

class ClangASTtoASRVisitor: public clang::RecursiveASTVisitor<ClangASTtoASRVisitor> {

public:

    std::string ast;
    SymbolTable *current_scope=nullptr;
    Allocator& al;
    ASR::asr_t*& tu;
    OneTimeUseASRNode<ASR::asr_t> tmp;
    Vec<ASR::stmt_t*>* current_body;
    bool is_stmt_created;
    std::vector<std::map<std::string, std::string>> scopes;
    OneTimeUseString cxx_operator_name_obj, member_name_obj;
    ASR::expr_t* assignment_target;
    Vec<ASR::expr_t*>* print_args;
    bool is_all_called, is_range_called;
    OneTimeUseASRNode<ASR::expr_t> range_start, range_end, range_step;
    Vec<ASR::case_stmt_t*>* current_switch_case;
    Vec<ASR::stmt_t*>* default_stmt;
    OneTimeUseBool is_break_stmt_present;
    bool interpret_init_list_expr_as_list;
    bool enable_fall_through;
    std::map<ASR::symbol_t*, std::map<std::string, ASR::expr_t*>> struct2member_inits;
    std::map<SymbolTable*, std::vector<ASR::symbol_t*>> scope2enums;
    clang::ForStmt* for_loop;
    bool inside_loop;
    std::set<ASR::symbol_t*> read_only_symbols;

    explicit ClangASTtoASRVisitor(clang::ASTContext *Context_,
        Allocator& al_, ASR::asr_t*& tu_):
        Context(Context_), al{al_}, tu{tu_},
        current_body{nullptr}, is_stmt_created{true},
        assignment_target{nullptr}, print_args{nullptr},
        is_all_called{false}, is_range_called{false},
        current_switch_case{nullptr}, default_stmt{nullptr},
        interpret_init_list_expr_as_list{false}, enable_fall_through{false},
        for_loop{nullptr}, inside_loop{false} {}

    template <typename T>
    Location Lloc(T *x) {
        Location l;
        l.first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        l.last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return l;
    }

    template <typename T>
    std::string get_file_name(T* x) {
        clang::SourceLocation loc = x->getLocation();
        if( loc.isInvalid() ) {
            return "";
        }

        clang::FullSourceLoc full_source_loc = Context->getFullLoc(loc);
        return std::string(full_source_loc.getPresumedLoc().getFilename());
    }

    template <typename T>
    std::string loc(T *x) {
        uint64_t first = Context->getFullLoc(x->getBeginLoc()).getFileOffset();
        uint64_t last = Context->getFullLoc(x->getEndLoc()).getFileOffset();
        return std::to_string(first) + ":" + std::to_string(last);
    }

    ASR::symbol_t* declare_dummy_variable(std::string var_name, SymbolTable* scope, Location& loc, ASR::ttype_t* var_type) {
        var_name = scope->get_unique_name(var_name, false);
        SetChar variable_dependencies_vec;
        variable_dependencies_vec.reserve(al, 1);
        ASRUtils::collect_variable_dependencies(al, variable_dependencies_vec, var_type);
        ASR::asr_t* variable_asr = ASR::make_Variable_t(al, loc, scope,
                                        s2c(al, var_name), variable_dependencies_vec.p,
                                        variable_dependencies_vec.size(), ASR::intentType::Local,
                                        nullptr, nullptr, ASR::storage_typeType::Default,
                                        var_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                                        ASR::presenceType::Required, false);
        ASR::symbol_t* dummy_variable_sym = ASR::down_cast<ASR::symbol_t>(variable_asr);
        scope->add_symbol(var_name, dummy_variable_sym);
        return dummy_variable_sym;
    }

    void construct_program() {
        // Convert the main function into a program
        ASR::TranslationUnit_t* tu = (ASR::TranslationUnit_t*)this->tu;
        Location& loc = tu->base.base.loc;

        Vec<ASR::stmt_t*> prog_body;
        prog_body.reserve(al, 1);
        SetChar prog_dep;
        prog_dep.reserve(al, 1);
        SymbolTable *program_scope = al.make_new<SymbolTable>(tu->m_symtab);

        std::string main_func = "main";
        ASR::symbol_t *main_sym = tu->m_symtab->resolve_symbol(main_func);
        if (main_sym == nullptr) {
            return;
        }
        ASR::Function_t *main = ASR::down_cast<ASR::Function_t>(main_sym);
        LCOMPILERS_ASSERT(main);

        // Make a FunctionCall to main
        ASR::ttype_t *int32_type = ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4));
        ASR::asr_t* func_call_main = ASR::make_FunctionCall_t(al, loc, main_sym, main_sym,
             nullptr, 0, int32_type, nullptr, nullptr);

        ASR::symbol_t* exit_variable_sym = declare_dummy_variable("exit_code", program_scope, loc, int32_type);
        ASR::expr_t* variable_var = ASRUtils::EXPR(ASR::make_Var_t(al, loc, exit_variable_sym));
        ASR::asr_t* assign_stmt = ASR::make_Assignment_t(al, loc, variable_var, ASRUtils::EXPR(func_call_main), nullptr);

        prog_body.push_back(al, ASRUtils::STMT(assign_stmt));

        prog_dep.push_back(al, s2c(al, main_func));

        std::string prog_name = "main_program";
        ASR::asr_t *prog = ASR::make_Program_t(
            al, loc,
            /* a_symtab */ program_scope,
            /* a_name */ s2c(al, prog_name),
            prog_dep.p,
            prog_dep.n,
            /* a_body */ prog_body.p,
            /* n_body */ prog_body.n);
        tu->m_symtab->add_symbol(prog_name, ASR::down_cast<ASR::symbol_t>(prog));
    }

    void remove_special_functions() {
        ASR::TranslationUnit_t* tu = (ASR::TranslationUnit_t*)this->tu;
        if (tu->m_symtab->resolve_symbol("printf")) {
            tu->m_symtab->erase_symbol("printf");
        }
        if (tu->m_symtab->resolve_symbol("exit")) {
            tu->m_symtab->erase_symbol("exit");
        }
    }

    bool TraverseTranslationUnitDecl(clang::TranslationUnitDecl *x) {
        SymbolTable *parent_scope = al.make_new<SymbolTable>(nullptr);
        current_scope = parent_scope;
        Location l = Lloc(x);
        tu = ASR::make_TranslationUnit_t(al, l, current_scope, nullptr, 0);

        for (auto D = x->decls_begin(), DEnd = x->decls_end(); D != DEnd; ++D) {
            TraverseDecl(*D);
        }

        construct_program();
        remove_special_functions();

        return true;
    }

    ASR::ttype_t* flatten_Array(ASR::ttype_t* array) {
        if( !ASRUtils::is_array(array) ) {
            return array;
        }
        ASR::Array_t* array_t = ASR::down_cast<ASR::Array_t>(array);
        ASR::ttype_t* m_type_flattened = flatten_Array(array_t->m_type);
        if( !ASR::is_a<ASR::Array_t>(*m_type_flattened) ) {
            return array;
        }

        ASR::Array_t* array_t_flattened = ASR::down_cast<ASR::Array_t>(m_type_flattened);
        ASR::dimension_t row = array_t->m_dims[0];
        Vec<ASR::dimension_t> new_dims; new_dims.reserve(al, array_t->n_dims + array_t_flattened->n_dims);
        new_dims.push_back(al, row);
        for( size_t i = 0; i < array_t_flattened->n_dims; i++ ) {
            new_dims.push_back(al, array_t_flattened->m_dims[i]);
        }
        array_t->m_type = array_t_flattened->m_type;
        array_t->m_dims = new_dims.p;
        array_t->n_dims = new_dims.size();
        std::map<ASR::array_physical_typeType, int> physicaltype2priority = {
            {ASR::array_physical_typeType::DescriptorArray, 2},
            {ASR::array_physical_typeType::PointerToDataArray, 1},
            {ASR::array_physical_typeType::FixedSizeArray, 0}
        };
        ASR::array_physical_typeType physical_type;
        if( physicaltype2priority[array_t->m_physical_type] >
            physicaltype2priority[array_t_flattened->m_physical_type] ) {
            physical_type = array_t->m_physical_type;
        } else {
            physical_type = array_t_flattened->m_physical_type;
        }
        array_t->m_physical_type = physical_type;
        return array;
    }

    ASR::ttype_t* ClangTypeToASRType(const clang::QualType& qual_type,
        Vec<ASR::dimension_t>* xshape_result=nullptr,
        ThirdPartyCPPArrayTypes* array_type=nullptr,
        bool* is_third_party_cpp_array=nullptr) {
        const clang::SplitQualType& split_qual_type = qual_type.split();
        const clang::Type* clang_type = split_qual_type.asPair().first;
        const clang::Qualifiers qualifiers = split_qual_type.asPair().second;
        Location l; l.first = 1, l.last = 1;
        ASR::ttype_t* type = nullptr;
        if (clang_type->isVoidType() ) {
            // do nothing
        } else if( clang_type->isEnumeralType() ) {
            std::string enum_name = qual_type.getAsString().erase(0, 5);
            ASR::symbol_t* enum_sym = current_scope->resolve_symbol(enum_name);
            if( !enum_sym ) {
                throw std::runtime_error(enum_name + " not found in current scope.");
            }
            type = ASRUtils::TYPE(ASR::make_Enum_t(al, l, enum_sym));
        } else if( clang_type->isCharType() ) {
            type = ASRUtils::TYPE(ASR::make_Character_t(al, l, 1, -1, nullptr));
        } else if( clang_type->isBooleanType() ) {
            type = ASRUtils::TYPE(ASR::make_Logical_t(al, l, 4));
        } else if( clang_type->isIntegerType() ) {
            type = ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4));
        } else if( clang_type->isFloatingType() ) {
            type = ASRUtils::TYPE(ASR::make_Real_t(al, l, 8));
        } else if( clang_type->isPointerType() ) {
            type = ClangTypeToASRType(qual_type->getPointeeType());
            if( !ASRUtils::is_character(*type) ) {
                type = ASRUtils::TYPE(ASR::make_Pointer_t(al, l, type));
            }
        } else if( clang_type->isConstantArrayType() ) {
            const clang::ArrayType* array_type = clang_type->getAsArrayTypeUnsafe();
            const clang::ConstantArrayType* fixed_size_array_type =
                reinterpret_cast<const clang::ConstantArrayType*>(array_type);
            type = ClangTypeToASRType(array_type->getElementType());
            llvm::APInt ap_int = fixed_size_array_type->getSize();
            uint64_t size = ap_int.getZExtValue();
            Vec<ASR::dimension_t> vec; vec.reserve(al, 1);
            ASR::dimension_t dim;
            dim.loc = l; dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                al, l, size, ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            vec.push_back(al, dim);
            type = ASRUtils::TYPE(ASR::make_Array_t(al, l, type, vec.p, vec.size(),
                ASR::array_physical_typeType::FixedSizeArray));
            type = flatten_Array(type);
        } else if( clang_type->isVariableArrayType() ) {
            const clang::ArrayType* array_type = clang_type->getAsArrayTypeUnsafe();
            const clang::VariableArrayType* variable_array_type =
                reinterpret_cast<const clang::VariableArrayType*>(array_type);
            type = ClangTypeToASRType(array_type->getElementType());
            clang::Expr* expr = variable_array_type->getSizeExpr();
            TraverseStmt(expr);
            Vec<ASR::dimension_t> vec; vec.reserve(al, 1);
            ASR::dimension_t dim;
            dim.loc = l; dim.m_length = ASRUtils::EXPR(tmp.get());
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
            vec.push_back(al, dim);
            type = ASRUtils::make_Array_t_util(al, l, type, vec.p, vec.size());
            type = flatten_Array(type);
        } else if( clang_type->getTypeClass() == clang::Type::LValueReference ) {
            const clang::LValueReferenceType* lvalue_reference_type = clang_type->getAs<clang::LValueReferenceType>();
            clang::QualType pointee_type = lvalue_reference_type->getPointeeType();
            type = ClangTypeToASRType(pointee_type, xshape_result, array_type, is_third_party_cpp_array);
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::Elaborated ) {
            const clang::ElaboratedType* elaborated_type = clang_type->getAs<clang::ElaboratedType>();
            clang::QualType desugared_type = elaborated_type->desugar();
            type = ClangTypeToASRType(desugared_type, xshape_result, array_type, is_third_party_cpp_array);
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::TemplateSpecialization ) {
            const clang::TemplateSpecializationType* template_specialization = clang_type->getAs<clang::TemplateSpecializationType>();
            std::string template_name = template_specialization->getTemplateName().getAsTemplateDecl()->getNameAsString();
            if( template_name == "xtensor" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() != 2 ) {
                    throw std::runtime_error("xtensor type must be initialised with element type and rank.");
                }
                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                clang::Expr* clang_rank = template_arguments.at(1).getAsExpr();
                TraverseStmt(clang_rank);
                int rank = 0;
                if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                    throw std::runtime_error("Rank provided in the xtensor initialisation must be a constant.");
                }
                tmp = nullptr;
                Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, rank);
                for( int dim = 0; dim < rank; dim++ ) {
                    ASR::dimension_t empty_dim;
                    empty_dim.loc = l;
                    empty_dim.m_start = nullptr;
                    empty_dim.m_length = nullptr;
                    empty_dims.push_back(al, empty_dim);
                }
                if( array_type && is_third_party_cpp_array ) {
                    *is_third_party_cpp_array = true;
                    *array_type = ThirdPartyCPPArrayTypes::XTensorArray;
                }
                type = ASRUtils::TYPE(ASR::make_Array_t(al, l,
                    ClangTypeToASRType(qual_type),
                    empty_dims.p, empty_dims.size(),
                    ASR::array_physical_typeType::DescriptorArray));
                type = ASRUtils::TYPE(ASR::make_Allocatable_t(al, l, type));
            } else if( template_name == "mdspan" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() != 2 ) {
                    throw std::runtime_error("mdspan type must be initialised with element type, index type and rank.");
                }
                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                const clang::QualType& shape_type = template_arguments.at(1).getAsType();
                Vec<ASR::dimension_t> xtensor_fixed_dims; xtensor_fixed_dims.reserve(al, 1);
                ClangTypeToASRType(shape_type, &xtensor_fixed_dims, array_type, is_third_party_cpp_array);
                // Only allocatables are made for Kokkos::mdspan
                if( array_type && is_third_party_cpp_array ) {
                    *is_third_party_cpp_array = true;
                    *array_type = ThirdPartyCPPArrayTypes::MDSpanArray;
                    xshape_result->from_pointer_n(xtensor_fixed_dims.p, xtensor_fixed_dims.size());
                }
                type = ASRUtils::TYPE(ASR::make_Array_t(al, l,
                    ClangTypeToASRType(qual_type),
                    xtensor_fixed_dims.p, xtensor_fixed_dims.size(),
                    ASR::array_physical_typeType::DescriptorArray));
                type = ASRUtils::TYPE(ASR::make_Allocatable_t(al, l, type));
            } else if( template_name == "xtensor_fixed" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() != 2 ) {
                    throw std::runtime_error("xtensor_fixed type must be initialised with element type and shape.");
                }
                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                const clang::QualType& shape_type = template_arguments.at(1).getAsType();
                Vec<ASR::dimension_t> xtensor_fixed_dims; xtensor_fixed_dims.reserve(al, 1);
                ClangTypeToASRType(shape_type, &xtensor_fixed_dims, array_type, is_third_party_cpp_array);
                type = ASRUtils::TYPE(ASR::make_Array_t(al, l,
                    ClangTypeToASRType(qual_type),
                    xtensor_fixed_dims.p, xtensor_fixed_dims.size(),
                    ASR::array_physical_typeType::FixedSizeArray));
            } else if( template_name == "xshape" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( xshape_result == nullptr ) {
                    throw std::runtime_error("Result Vec<ASR::dimention_t>* not provided.");
                }

                ASR::expr_t* zero = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                for( int i = 0; i < template_arguments.size(); i++ ) {
                    clang::Expr* clang_rank = template_arguments.at(i).getAsExpr();
                    TraverseStmt(clang_rank);
                    int rank = 0;
                    if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                        throw std::runtime_error("Rank provided in the xshape must be a constant.");
                    }
                    ASR::dimension_t dim; dim.loc = l;
                    dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, rank,
                        ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                    dim.m_start = zero;
                    xshape_result->push_back(al, dim);
                }
                return nullptr;
            } else if( template_name == "extents" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( xshape_result == nullptr ) {
                    throw std::runtime_error("Result Vec<ASR::dimention_t>* not provided.");
                }

                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                ASR::ttype_t* index_type = ClangTypeToASRType(qual_type);
                if( !ASR::is_a<ASR::Integer_t>(*index_type) ||
                    ASRUtils::extract_kind_from_ttype_t(index_type) != 4 ) {
                    throw std::runtime_error("Only int32_t should be used for index type in Kokkos::dextents.");
                }
                ASR::expr_t* zero = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                for( int i = 1; i < template_arguments.size(); i++ ) {
                    clang::Expr* clang_rank = template_arguments.at(i).getAsExpr();
                    TraverseStmt(clang_rank);
                    int rank = 0;
                    if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                        throw std::runtime_error("Rank provided in the xshape must be a constant.");
                    }
                    ASR::dimension_t dim; dim.loc = l;
                    dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, rank,
                        ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                    dim.m_start = zero;
                    xshape_result->push_back(al, dim);
                }
                return nullptr;
            } else if( template_name == "dextents" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( xshape_result == nullptr ) {
                    throw std::runtime_error("Result Vec<ASR::dimention_t>* not provided.");
                }

                ASR::expr_t* zero = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, l, 0,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, l, 4))));
                const clang::QualType& index_type = template_arguments.at(0).getAsType();
                if( !ASR::is_a<ASR::Integer_t>(*ClangTypeToASRType(index_type)) ||
                    ASRUtils::extract_kind_from_ttype_t(ClangTypeToASRType(index_type)) != 4 ) {
                    throw std::runtime_error("Only int32_t should be used for index type in Kokkos::dextents.");
                }
                clang::Expr* clang_rank = template_arguments.at(1).getAsExpr();
                TraverseStmt(clang_rank);
                int rank = 0;
                if( !ASRUtils::extract_value(ASRUtils::EXPR(tmp.get()), rank) ) {
                    throw std::runtime_error("Rank provided in the Kokkos::dextents must be a constant.");
                }
                for( int i = 0; i < rank; i++ ) {
                    ASR::dimension_t dim; dim.loc = l;
                    dim.m_length = nullptr;
                    dim.m_start = zero;
                    xshape_result->push_back(al, dim);
                }
                return nullptr;
            } else if( template_name == "complex" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() > 1 ) {
                    throw std::runtime_error("std::complex accepts only one argument.");
                }

                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                ASR::ttype_t* complex_subtype = ClangTypeToASRType(qual_type, xshape_result,
                    array_type, is_third_party_cpp_array);
                if( !ASRUtils::is_real(*complex_subtype) ) {
                    throw std::runtime_error(std::string("std::complex accepts only real types, found: ") +
                        ASRUtils::type_to_str(complex_subtype));
                }
                type = ASRUtils::TYPE(ASR::make_Complex_t(al, l,
                    ASRUtils::extract_kind_from_ttype_t(complex_subtype)));
            } else if( template_name == "vector" ) {
                const std::vector<clang::TemplateArgument>& template_arguments = template_specialization->template_arguments();
                if( template_arguments.size() > 1 ) {
                    throw std::runtime_error("std::vector accepts only one argument.");
                }

                const clang::QualType& qual_type = template_arguments.at(0).getAsType();
                ASR::ttype_t* vector_subtype = ClangTypeToASRType(qual_type, xshape_result,
                    array_type, is_third_party_cpp_array);
                type = ASRUtils::TYPE(ASR::make_List_t(al, l, vector_subtype));
            } else {
                throw std::runtime_error(std::string("Unrecognized type ") + template_name);
            }
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::Record ) {
            const clang::CXXRecordDecl* record_type = clang_type->getAsCXXRecordDecl();
            std::string name = record_type->getNameAsString();
            std::string qualified_name = record_type->getQualifiedNameAsString();
            if( name == "xtensor_container" || name == "vector" || name == "mdspan" ) {
                return nullptr;
            }
            ASR::symbol_t* type_t = current_scope->resolve_symbol(name);
            if( !type_t ) {
                if( qualified_name == "at::Tensor" ) {
                    if( array_type == nullptr || is_third_party_cpp_array == nullptr ) {
                        throw std::runtime_error("IEC: array_type and is_third_party_cpp_array couldn't be set.");
                    }
                    *is_third_party_cpp_array = true;
                    *array_type = ThirdPartyCPPArrayTypes::PyTorchArray;
                    type = ASRUtils::TYPE(ASR::make_Array_t(al, l, ASRUtils::TYPE(ASR::make_Real_t(al, l, 8)),
                        nullptr, 0, ASR::array_physical_typeType::DescriptorArray));
                    return type;
                } else if( qualified_name == "c10::Scalar" ) {
                    return nullptr;
                }
                throw std::runtime_error(qualified_name + " not defined.");
            }
            if( clang_type->isUnionType() ) {
                type = ASRUtils::TYPE(ASR::make_Union_t(al, l, type_t));
            } else {
                type = ASRUtils::TYPE(ASR::make_Struct_t(al, l, type_t));
            }
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::Using ) {
            const clang::UsingType* using_type = clang_type->getAs<clang::UsingType>();
            return ClangTypeToASRType(using_type->getUnderlyingType(), xshape_result,
                array_type, is_third_party_cpp_array);
        } else if( clang_type->getTypeClass() == clang::Type::TypeClass::SubstTemplateTypeParm ||
                   clang_type->getTypeClass() == clang::Type::TypeClass::Typedef ) {
            return nullptr;
        } else {
            throw std::runtime_error("clang::QualType not yet supported " +
                std::string(clang_type->getTypeClassName()));
        }

        if( qualifiers.hasConst() ) {
            type = ASRUtils::TYPE(ASR::make_Const_t(al, l, type));
        }
        return type;
    }

    bool TraverseCXXRecordDecl(clang::CXXRecordDecl* x) {
        for( auto constructors = x->ctor_begin(); constructors != x->ctor_end(); constructors++ ) {
            clang::CXXConstructorDecl* constructor = *constructors;
            if( constructor->isTrivial() || constructor->isImplicit() ) {
                continue ;
            }
            for( auto ctor = constructor->init_begin(); ctor != constructor->init_end(); ctor++ ) {
                clang::CXXCtorInitializer* ctor_init = *ctor;
                clang::Expr* init_expr = ctor_init->getInit();
                if( init_expr->isConstantInitializer(*Context, false) ) {
                    continue;
                }
                if( init_expr->getStmtClass() == clang::Stmt::StmtClass::InitListExprClass ) {
                    init_expr = static_cast<clang::InitListExpr*>(init_expr)->getInit(0);
                }
                if( init_expr->getStmtClass() == clang::Stmt::StmtClass::CXXConstructExprClass ) {
                    init_expr = static_cast<clang::CXXConstructExpr*>(init_expr)->getArg(0);
                }
                if( init_expr->getStmtClass() != clang::Stmt::StmtClass::ImplicitCastExprClass ||
                    static_cast<clang::ImplicitCastExpr*>(init_expr)->getSubExpr()->getStmtClass() !=
                    clang::Stmt::StmtClass::DeclRefExprClass ) {
                    throw std::runtime_error("Initialisation expression in constructor should "
                                             "only be the argument itself.");
                }
            }
        }

        SymbolTable* parent_scope = current_scope;
        current_scope = al.make_new<SymbolTable>(parent_scope);
        std::string name = x->getNameAsString();
        Vec<char*> field_names; field_names.reserve(al, 1);
        for( auto field = x->field_begin(); field != x->field_end(); field++ ) {
            clang::FieldDecl* field_decl = *field;
            TraverseFieldDecl(field_decl);
            field_names.push_back(al, s2c(al, field_decl->getNameAsString()));
        }
        if( x->isUnion() ) {
            ASR::symbol_t* union_t = ASR::down_cast<ASR::symbol_t>(ASR::make_UnionType_t(al, Lloc(x), current_scope,
                s2c(al, name), nullptr, 0, field_names.p, field_names.size(), ASR::abiType::Source,
                ASR::accessType::Public, nullptr, 0, nullptr));
                parent_scope->add_symbol(name, union_t);
        } else {
            ASR::symbol_t* struct_t = ASR::down_cast<ASR::symbol_t>(ASR::make_StructType_t(al, Lloc(x), current_scope,
                s2c(al, name), nullptr, 0, field_names.p, field_names.size(), ASR::abiType::Source,
                ASR::accessType::Public, false, x->isAbstract(), nullptr, 0, nullptr, nullptr));
            parent_scope->add_symbol(name, struct_t);
        }
        current_scope = parent_scope;
        return true;
    }

    bool TraverseFieldDecl(clang::FieldDecl* x) {
        std::string name = x->getName().str();
        if( current_scope->get_symbol(name) ) {
            throw std::runtime_error(name + std::string(" is already defined."));
        }

        ASR::ttype_t *asr_type = ClangTypeToASRType(x->getType());
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, name), nullptr, 0, ASR::intentType::Local, nullptr, nullptr,
            ASR::storage_typeType::Default, asr_type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false));
        current_scope->add_symbol(name, v);
        is_stmt_created = false;
        return true;
    }

    bool TraverseEnumDecl(clang::EnumDecl* x) {
        std::string enum_name = x->getName().str();
        if( current_scope->get_symbol(enum_name) ) {
            throw std::runtime_error(enum_name + std::string(" is already defined."));
        }

        SymbolTable* parent_scope = current_scope;
        current_scope = al.make_new<SymbolTable>(parent_scope);
        Vec<char*> field_names; field_names.reserve(al, 1);
        int64_t last_enum_value = 0;
        for( auto enum_const_itr = x->enumerator_begin();
             enum_const_itr != x->enumerator_end(); enum_const_itr++ ) {
            clang::EnumConstantDecl* enum_const = *enum_const_itr;
            std::string enum_const_name = enum_const->getNameAsString();
            field_names.push_back(al, s2c(al, enum_const_name));
            ASR::expr_t* init_expr = nullptr;
            if( enum_const->getInitExpr() ) {
                TraverseStmt(enum_const->getInitExpr());
                init_expr = ASRUtils::EXPR(tmp.get());
                last_enum_value = enum_const->getInitVal().getSExtValue() + 1;
            } else {
                init_expr = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x),
                    last_enum_value, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
                last_enum_value += 1;
            }
            ASR::symbol_t* v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
                current_scope, s2c(al, enum_const_name), nullptr, 0, ASR::intentType::Local,
                init_expr, init_expr, ASR::storage_typeType::Default, ASRUtils::expr_type(init_expr),
                nullptr, ASR::abiType::Source, ASR::accessType::Public, ASR::presenceType::Required, false));
            current_scope->add_symbol(enum_const_name, v);
        }

        ASR::enumtypeType enum_value_type = ASR::enumtypeType::NonInteger;
        ASR::ttype_t* common_type = ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4));
        int8_t IntegerConsecutiveFromZero = 1;
        int8_t IntegerNotUnique = 0;
        int8_t IntegerUnique = 1;
        std::map<int64_t, int64_t> value2count;
        for( auto sym: current_scope->get_scope() ) {
            ASR::Variable_t* member_var = ASR::down_cast<ASR::Variable_t>(sym.second);
            ASR::expr_t* value = ASRUtils::expr_value(member_var->m_symbolic_value);
            int64_t value_int64 = -1;
            ASRUtils::extract_value(value, value_int64);
            if( value2count.find(value_int64) == value2count.end() ) {
                value2count[value_int64] = 0;
            }
            value2count[value_int64] += 1;
        }
        int64_t prev = -1;
        for( auto itr: value2count ) {
            if( itr.second > 1 ) {
                IntegerNotUnique = 1;
                IntegerUnique = 0;
                IntegerConsecutiveFromZero = 0;
                break ;
            }
            if( itr.first - prev != 1 ) {
                IntegerConsecutiveFromZero = 0;
            }
            prev = itr.first;
        }
        if( IntegerConsecutiveFromZero ) {
            if( value2count.find(0) == value2count.end() ) {
                IntegerConsecutiveFromZero = 0;
                IntegerUnique = 1;
            } else {
                IntegerUnique = 0;
            }
        }
        LCOMPILERS_ASSERT(IntegerConsecutiveFromZero + IntegerNotUnique + IntegerUnique == 1);
        if( IntegerConsecutiveFromZero ) {
            enum_value_type = ASR::enumtypeType::IntegerConsecutiveFromZero;
        } else if( IntegerNotUnique ) {
            enum_value_type = ASR::enumtypeType::IntegerNotUnique;
        } else if( IntegerUnique ) {
            enum_value_type = ASR::enumtypeType::IntegerUnique;
        }

        ASR::symbol_t* enum_t = ASR::down_cast<ASR::symbol_t>(ASR::make_EnumType_t(al, Lloc(x),
            current_scope, s2c(al, enum_name), nullptr, 0, field_names.p, field_names.size(),
            ASR::abiType::Source, ASR::accessType::Public, enum_value_type, common_type, nullptr));
        parent_scope->add_symbol(enum_name, enum_t);
        current_scope = parent_scope;
        scope2enums[current_scope].push_back(enum_t);
        return true;
    }

    bool TraverseParmVarDecl(clang::ParmVarDecl* x) {
        std::string name = x->getName().str();
        if( name == "" ) {
            name = current_scope->get_unique_name("param");
        }

        bool is_third_party_array_type = false;
        ThirdPartyCPPArrayTypes array_type;
        Vec<ASR::dimension_t> shape_result; shape_result.reserve(al, 1);
        ASR::ttype_t* type = ClangTypeToASRType(x->getType(), &shape_result,
            &array_type, &is_third_party_array_type);
        if( is_third_party_array_type &&
            array_type == ThirdPartyCPPArrayTypes::PyTorchArray ) {
            if( !x->getDefaultArg() ) {
                throw std::runtime_error("torch::Tensor type arguments must have default arguments.");
            }
        }
        ASR::intentType intent_type = ASR::intentType::InOut;
        if( ASR::is_a<ASR::Const_t>(*type) ) {
            intent_type = ASR::intentType::In;
            type = ASRUtils::type_get_past_const(type);
        }
        if( ASRUtils::is_allocatable(type) ) {
            type = ASRUtils::type_get_past_allocatable(type);
        }
        if( x->getType()->getTypeClass() != clang::Type::LValueReference &&
            ASRUtils::is_array(type) ) {
            throw std::runtime_error("Array objects should be passed by reference only.");
        }
        tmp = ASR::make_Variable_t(al, Lloc(x), current_scope, s2c(al, name),
            nullptr, 0, intent_type, nullptr, nullptr,
            ASR::storage_typeType::Default, type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false);
        ASR::symbol_t* tmp_sym = ASR::down_cast<ASR::symbol_t>(tmp.get());
        current_scope->add_symbol(name, tmp_sym);
        ASR::asr_t* tmp_ = ASR::make_Var_t(al, Lloc(x), tmp_sym);

        clang::Expr *init = x->getDefaultArg();
        ASR::expr_t* asr_init = nullptr;
        if (init) {
            ASR::expr_t* assignment_target_copy = assignment_target;
            assignment_target = ASRUtils::EXPR(tmp_);
            TraverseStmt(init);
            if( tmp != nullptr && !is_stmt_created ) {
                asr_init = ASRUtils::EXPR(tmp.get());
            }
        }

        // TODO: For PyTorch tensor create an intrinsic empty
        // and then fill the initialiser value with a call
        // to that intrinsic.

        tmp = tmp_;
        is_stmt_created = false;
        return true;
    }

    bool TraverseImplicitCastExpr(clang::ImplicitCastExpr* x) {
        clang::CastKind cast_kind = x->getCastKind();
        ASR::cast_kindType asr_cast_kind;
        switch( cast_kind ) {
            case clang::CastKind::CK_IntegralToFloating: {
                asr_cast_kind = ASR::cast_kindType::IntegerToReal;
                break;
            }
            case clang::CastKind::CK_FloatingCast: {
                asr_cast_kind = ASR::cast_kindType::RealToReal;
                break;
            }
            case clang::CastKind::CK_FloatingToIntegral: {
                asr_cast_kind = ASR::cast_kindType::RealToInteger;
                break;
            }
            default: {
                clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseImplicitCastExpr(x);
                return true;
            }
        }
        clang::Expr* sub_expr = x->getSubExpr();
        TraverseStmt(sub_expr);
        ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
        tmp = ASRUtils::make_Cast_t_value(al, Lloc(x), arg, asr_cast_kind,
                ClangTypeToASRType(x->getType()));
        is_stmt_created = false;
        return true;
    }

    ASR::expr_t* evaluate_compile_time_value_for_StructInstanceMember(
        ASR::expr_t* base, const std::string& member_name) {
        if( ASR::is_a<ASR::Var_t>(*base) ) {
            ASR::Var_t* var_t = ASR::down_cast<ASR::Var_t>(base);
            ASR::symbol_t* v = ASRUtils::symbol_get_past_external(var_t->m_v);
            if( struct2member_inits.find(v) == struct2member_inits.end() ) {
                return nullptr;
            }
            return struct2member_inits[v][member_name];
        }

        return nullptr;
    }

    bool TraverseMemberExpr(clang::MemberExpr* x) {
        TraverseStmt(x->getBase());
        ASR::expr_t* base = ASRUtils::EXPR(tmp.get());
        ASR::ttype_t* base_type = ASRUtils::extract_type(ASRUtils::expr_type(base));
        std::string member_name = x->getMemberDecl()->getNameAsString();
        if( ASR::is_a<ASR::Struct_t>(*base_type) ) {
            ASR::Struct_t* struct_t = ASR::down_cast<ASR::Struct_t>(base_type);
            ASR::StructType_t* struct_type_t = ASR::down_cast<ASR::StructType_t>(
                   ASRUtils::symbol_get_past_external(struct_t->m_derived_type));
            ASR::symbol_t* member = struct_type_t->m_symtab->resolve_symbol(member_name);
            if( !member ) {
                throw std::runtime_error(member_name + " not found in the scope of " + struct_type_t->m_name);
            }
            std::string mangled_name = current_scope->get_unique_name(
                member_name + "@" + struct_type_t->m_name);
            member = ASR::down_cast<ASR::symbol_t>(ASR::make_ExternalSymbol_t(
                al, Lloc(x), current_scope, s2c(al, mangled_name), member,
                struct_type_t->m_name, nullptr, 0, s2c(al, member_name),
                ASR::accessType::Public));
            current_scope->add_symbol(mangled_name, member);
            tmp = ASR::make_StructInstanceMember_t(al, Lloc(x), base, member, ASRUtils::symbol_type(member),
                evaluate_compile_time_value_for_StructInstanceMember(base, member_name));
        } else if( ASR::is_a<ASR::Union_t>(*base_type) ) {
            ASR::Union_t* union_t = ASR::down_cast<ASR::Union_t>(base_type);
            ASR::UnionType_t* union_type_t = ASR::down_cast<ASR::UnionType_t>(
                   ASRUtils::symbol_get_past_external(union_t->m_union_type));
            ASR::symbol_t* member = union_type_t->m_symtab->resolve_symbol(member_name);
            if( !member ) {
                throw std::runtime_error(member_name + " not found in the scope of " + union_type_t->m_name);
            }
            std::string mangled_name = current_scope->get_unique_name(
                member_name + "@" + union_type_t->m_name);
            member = ASR::down_cast<ASR::symbol_t>(ASR::make_ExternalSymbol_t(
                al, Lloc(x), current_scope, s2c(al, mangled_name), member,
                union_type_t->m_name, nullptr, 0, s2c(al, member_name),
                ASR::accessType::Public));
            current_scope->add_symbol(mangled_name, member);
            tmp = ASR::make_UnionInstanceMember_t(al, Lloc(x),
                base, member, ASRUtils::symbol_type(member), nullptr);
        } else if( special_function_map.find(member_name) != special_function_map.end() ) {
            member_name_obj.set(member_name);
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseMemberExpr(x);
        } else {
            throw std::runtime_error("clang::MemberExpr only supported for struct, union and special functions.");
        }
        return true;
    }

    bool TraverseCXXMemberCallExpr(clang::CXXMemberCallExpr* x) {
        clang::Expr* callee = x->getCallee();
        TraverseStmt(callee);
        ASR::expr_t* asr_callee = ASRUtils::EXPR(tmp.get());
        if( !check_and_handle_special_function(x, asr_callee) ) {
             throw std::runtime_error("Only xt::xtensor::shape, "
                "xt::xtensor::fill, xt::xtensor::size is supported.");
        }

        return true;
    }

    bool TraverseCXXOperatorCallExpr(clang::CXXOperatorCallExpr* x) {
        #define generate_code_for_binop(op) if( x->getNumArgs() != 2 ) {    \
                throw std::runtime_error(cxx_operator_name + " accepts two arguments, found " + std::to_string(x->getNumArgs()));    \
            }    \
            clang::Expr** args = x->getArgs();    \
            TraverseStmt(args[0]);    \
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
            TraverseStmt(args[1]);    \
            ASR::expr_t* value = ASRUtils::EXPR(tmp.get());    \
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_array(ASRUtils::expr_type(value)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(value)) ) {    \
                CreateBinOp(obj, value, op, Lloc(x));    \
            } else {    \
                throw std::runtime_error(cxx_operator_name + " is supported only for arrays and complex types, found "    \
                    + ASRUtils::type_to_str(ASRUtils::expr_type(obj)));    \
            }    \

        #define generate_code_for_cmpop(op) if( x->getNumArgs() != 2 ) {    \
                throw std::runtime_error(cxx_operator_name + " accepts two arguments, found " + std::to_string(x->getNumArgs()));    \
            }    \
            clang::Expr** args = x->getArgs();    \
            TraverseStmt(args[0]);    \
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
            TraverseStmt(args[1]);    \
            ASR::expr_t* value = ASRUtils::EXPR(tmp.get());    \
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(obj)) ||    \
                ASRUtils::is_array(ASRUtils::expr_type(value)) ||    \
                ASRUtils::is_complex(*ASRUtils::expr_type(value)) ) {    \
                CreateCompareOp(obj, value, op, Lloc(x));    \
            } else {    \
                throw std::runtime_error(cxx_operator_name + " is supported only for arrays and complex types, found "    \
                    + ASRUtils::type_to_str(ASRUtils::expr_type(obj)));    \
            }    \

        clang::Expr* callee = x->getCallee();
        TraverseStmt(callee);
        std::string cxx_operator_name = cxx_operator_name_obj.get();
        if( cxx_operator_name == "operator<<" ) {
            if( print_args == nullptr ) {
                print_args = al.make_new<Vec<ASR::expr_t*>>();
                print_args->reserve(al, 1);
            }
            clang::Expr** args = x->getArgs();
            TraverseStmt(args[1]);
            cxx_operator_name = cxx_operator_name_obj.get();
            if( cxx_operator_name.size() == 0 && print_args != nullptr && tmp != nullptr ) {
                ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
                print_args->push_back(al, arg);
            }
            TraverseStmt(args[0]);
            cxx_operator_name = cxx_operator_name_obj.get();
            if( cxx_operator_name == "cout" ) {
                Vec<ASR::expr_t*> print_args_vec;
                print_args_vec.reserve(al, print_args->size());
                for( int i = print_args->size() - 1; i >= 0; i-- ) {
                    print_args_vec.push_back(al, print_args->p[i]);
                }
                ASR::expr_t* empty_string = ASRUtils::EXPR(
                    ASR::make_StringConstant_t(al, Lloc(x), s2c(al, ""),
                    ASRUtils::TYPE(ASR::make_Character_t(al, Lloc(x), 1, 1, nullptr))
                ));
                tmp = ASR::make_Print_t(al, Lloc(x), print_args_vec.p,
                    print_args_vec.size(), empty_string, empty_string);
                print_args = nullptr;
                is_stmt_created = true;
            }
        } else if( cxx_operator_name == "operator()" || cxx_operator_name == "operator[]" ) {
            clang::Expr** args = x->getArgs();
            if( x->getNumArgs() == 0 ) {
                throw std::runtime_error(cxx_operator_name + " needs at least the callee to be present.");
            }

            TraverseStmt(args[0]);
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ) {
                Vec<ASR::array_index_t> array_indices;
                array_indices.reserve(al, x->getNumArgs() - 1);
                for( size_t i = 1; i < x->getNumArgs(); i++ ) {
                    TraverseStmt(args[i]);
                    ASR::expr_t* index = ASRUtils::EXPR(tmp.get());
                    ASR::array_index_t array_index;
                    array_index.loc = index->base.loc;
                    array_index.m_left = nullptr;
                    array_index.m_right = index;
                    array_index.m_step = nullptr;
                    array_indices.push_back(al, array_index);
                }
                tmp = ASR::make_ArrayItem_t(al, Lloc(x), obj,
                    array_indices.p, array_indices.size(),
                    ASRUtils::extract_type(ASRUtils::expr_type(obj)),
                    ASR::arraystorageType::RowMajor, nullptr);
            } else if(ASR::is_a<ASR::List_t>(*ASRUtils::expr_type(obj))) {
                if( x->getNumArgs() > 2 ) {
                    throw std::runtime_error("std::vector needs only one integer for indexing.");
                }

                TraverseStmt(args[1]);
                ASR::expr_t* index = ASRUtils::EXPR(tmp.get());
                tmp = ASR::make_ListItem_t(al, Lloc(x), obj, index,
                    ASRUtils::get_contained_type(ASRUtils::expr_type(obj)), nullptr);
            } else if( ASR::is_a<ASR::IntrinsicArrayFunction_t>(*obj) ) {
                tmp = (ASR::asr_t*) obj;
            } else {
                throw std::runtime_error("Only indexing arrays is supported for now with " + cxx_operator_name + ".");
            }
        } else if( cxx_operator_name == "operator=" ) {
            if( x->getNumArgs() != 2 ) {
                throw std::runtime_error("operator= accepts two arguments, found " + std::to_string(x->getNumArgs()));
            }

            clang::Expr** args = x->getArgs();
            TraverseStmt(args[0]);
            ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());
            assignment_target = obj;
            if( ASRUtils::is_array(ASRUtils::expr_type(obj)) ) {
                TraverseStmt(args[1]);
                if( !is_stmt_created ) {
                    ASR::expr_t* value = ASRUtils::EXPR(tmp.get());
                    cast_helper(obj, value, true);
                    ASRUtils::make_ArrayBroadcast_t_util(al, Lloc(x), obj, value);
                    tmp = ASR::make_Assignment_t(al, Lloc(x), obj, value, nullptr);
                    is_stmt_created = true;
                } else {
                    // This means that right hand side of operator=
                    // was a CXXConstructor and there was an allocation
                    // statement created for it. So skip and return should
                    // done in this case.
                    tmp = nullptr;
                    is_stmt_created = false;
                }
                assignment_target = nullptr;
            } else if( ASRUtils::is_complex(*ASRUtils::expr_type(obj)) ||
                       ASR::is_a<ASR::Struct_t>(*ASRUtils::extract_type(
                        ASRUtils::expr_type(obj))) ||
                       ASR::is_a<ASR::ArrayItem_t>(*obj) ) {
                TraverseStmt(args[1]);
                if( !is_stmt_created ) {
                    ASR::expr_t* value = ASRUtils::EXPR(tmp.get());
                    cast_helper(obj, value, true);
                    tmp = ASR::make_Assignment_t(al, Lloc(x), obj, value, nullptr);
                    is_stmt_created = true;
                }
                assignment_target = nullptr;
            } else if( ASR::is_a<ASR::List_t>(*ASRUtils::expr_type(obj)) ) {
                interpret_init_list_expr_as_list = true;
                TraverseStmt(args[1]);
                interpret_init_list_expr_as_list = false;
                if( !is_stmt_created ) {
                    ASR::expr_t* value = ASRUtils::EXPR(tmp.get());
                    tmp = ASR::make_Assignment_t(al, Lloc(x), obj, value, nullptr);
                    is_stmt_created = true;
                }
            } else {
                throw std::runtime_error("operator= is supported only for array, complex and list types.");
            }
        } else if( cxx_operator_name == "operator+" ) {
            generate_code_for_binop(ASR::binopType::Add);
        } else if( cxx_operator_name == "operator-" ) {
            if( x->getNumArgs() == 1 ) {
                clang::Expr** args = x->getArgs();    \
                TraverseStmt(args[0]);    \
                ASR::expr_t* obj = ASRUtils::EXPR(tmp.get());    \
                CreateUnaryMinus(obj, Lloc(x));
            } else {
                generate_code_for_binop(ASR::binopType::Sub);
            }
        } else if( cxx_operator_name == "operator/" ) {
            generate_code_for_binop(ASR::binopType::Div);
        } else if( cxx_operator_name == "operator*" ) {
            generate_code_for_binop(ASR::binopType::Mul);
        } else if( cxx_operator_name == "operator>" ) {
            generate_code_for_cmpop(ASR::cmpopType::Gt);
        } else if( cxx_operator_name == "operator<" ) {
            generate_code_for_cmpop(ASR::cmpopType::Lt);
        } else if( cxx_operator_name == "operator<=" ) {
            generate_code_for_cmpop(ASR::cmpopType::LtE);
        } else if( cxx_operator_name == "operator>=" ) {
            generate_code_for_cmpop(ASR::cmpopType::GtE);
        } else if( cxx_operator_name == "operator!=" ) {
            generate_code_for_cmpop(ASR::cmpopType::NotEq);
        } else {
            throw std::runtime_error("C++ operator is not supported yet, " + cxx_operator_name);
        }
        return true;
    }

    template <typename CallExprType>
    bool check_and_handle_special_function(
        CallExprType *x, ASR::expr_t* callee) {
        std::string func_name;
        std::string cxx_operator_name = cxx_operator_name_obj.get();
        std::string member_name = member_name_obj.get();
        if( cxx_operator_name.size() > 0 ) {
            func_name = cxx_operator_name;
        } else if( member_name.size() > 0 ) {
            if( callee == nullptr ) {
                throw std::runtime_error("Callee object not available.");
            }
            func_name = member_name;
        } else {
            ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
            func_name = std::string(ASRUtils::symbol_name(callee_Var->m_v));
        }
        if (special_function_map.find(func_name) == special_function_map.end()) {
            if( current_scope->resolve_symbol(func_name) == nullptr ) {
                throw std::runtime_error("ICE: " + func_name + " is not handled yet in LC.");
            }
            return false;
        }
        SpecialFunc sf = special_function_map[func_name];
        Vec<ASR::expr_t*> args;
        args.reserve(al, 1);
        if( sf != SpecialFunc::View ) {
            bool skip_format_str = true;
            ASR::expr_t* assignment_target_copy = assignment_target;
            assignment_target = nullptr;
            for (auto *p : x->arguments()) {
                TraverseStmt(p);
                if (sf == SpecialFunc::Printf && skip_format_str) {
                    skip_format_str = false;
                    continue;
                }
                if( (tmp == nullptr || p->getStmtClass() ==
                     clang::Stmt::StmtClass::CXXDefaultArgExprClass ) ) {
                    args.push_back(al, nullptr);
                } else {
                    ASR::asr_t* tmp_ = tmp.get();
                    args.push_back(al, ASRUtils::EXPR(tmp_));
                }
            }
            assignment_target = assignment_target_copy;
        }
        if (sf == SpecialFunc::Printf) {
            tmp = ASR::make_Print_t(al, Lloc(x), args.p, args.size(), nullptr, nullptr);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::View) {
            clang::Expr** view_args = x->getArgs();
            size_t view_nargs = x->getNumArgs();
            ASR::expr_t* assignment_target_copy = assignment_target;
            assignment_target = nullptr;
            TraverseStmt(view_args[0]);
            assignment_target = assignment_target_copy;
            ASR::expr_t* array = ASRUtils::EXPR(tmp.get());
            size_t rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(array));
            Vec<ASR::array_index_t> array_section_indices;
            array_section_indices.reserve(al, rank);
            size_t i, j, result_dims = 0;
            for( i = 0, j = 1; j < view_nargs; j++, i++ ) {
                ASR::expr_t* assignment_target_copy = assignment_target;
                assignment_target = nullptr;
                TraverseStmt(view_args[j]);
                assignment_target = assignment_target_copy;
                ASR::array_index_t index;
                if( (tmp == nullptr && (is_all_called || is_range_called)) ||
                    (tmp == nullptr || view_args[j]->getStmtClass() ==
                        clang::Stmt::StmtClass::CXXDefaultArgExprClass ) ) {
                    index.loc = array->base.loc;
                    if( is_range_called ) {
                        index.m_left = range_start.get();
                        ASR::expr_t* range_end_ = range_end.get();
                        CreateBinOp(range_end_,
                            ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc,
                            1, ASRUtils::expr_type(range_end_))),
                            ASR::binopType::Sub, index.loc);
                        index.m_right = ASRUtils::EXPR(tmp.get());
                        index.m_step = range_step.get();
                        array_section_indices.push_back(al, index);
                    } else if( is_all_called ) {
                        index.m_left = PassUtils::get_bound(array, i + 1, "lbound", al);
                        index.m_right = PassUtils::get_bound(array, i + 1, "ubound", al);
                        index.m_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc, 1,
                            ASRUtils::TYPE(ASR::make_Integer_t(al, index.loc, 4))));
                        array_section_indices.push_back(al, index);
                    } else {
                        throw std::runtime_error("Neither xt::range nor xt::all is being called for slicing array.");
                    }
                    is_range_called = false;
                    is_all_called = false;
                    result_dims += 1;
                } else {
                    ASR::expr_t* arg_ = ASRUtils::EXPR(tmp.get());
                    index.loc = arg_->base.loc;
                    index.m_left = nullptr;
                    index.m_right = arg_;
                    index.m_step = nullptr;
                    array_section_indices.push_back(al, index);
                }
            }
            for( ; i < rank; i++ ) {
                ASR::array_index_t index;
                index.loc = array->base.loc;
                index.m_left = PassUtils::get_bound(array, i + 1, "lbound", al);
                index.m_right = PassUtils::get_bound(array, i + 1, "ubound", al);
                index.m_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, index.loc, 1,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, index.loc, 4))));
                array_section_indices.push_back(al, index);
                result_dims += 1;
            }
            ASR::ttype_t* element_type = ASRUtils::extract_type(ASRUtils::expr_type(array));
            Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, result_dims);
            for( size_t i = 0; i < result_dims; i++ ) {
                ASR::dimension_t dim;
                dim.loc = Lloc(x);
                dim.m_length = nullptr;
                dim.m_start = nullptr;
                empty_dims.push_back(al, dim);
            }
            ASR::ttype_t* array_section_type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
                element_type, empty_dims.p, empty_dims.size(), ASR::array_physical_typeType::DescriptorArray));
            tmp = ASR::make_ArraySection_t(al, Lloc(x), array, array_section_indices.p,
                array_section_indices.size(), array_section_type, nullptr);
        } else if (sf == SpecialFunc::Shape) {
            if( args.size() == 0 ) {
                throw std::runtime_error("Calling xt::xtensor::shape without dimension is not supported yet.");
            }

            ASR::expr_t* dim = args.p[0];
            int dim_value = -1;
            if( ASRUtils::extract_value(dim, dim_value) ) {
                dim = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, dim->base.loc,
                    dim_value + 1, ASRUtils::expr_type(dim)));
            } else {
                dim = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(al, dim->base.loc,
                    dim, ASR::binopType::Add, ASRUtils::get_constant_one_with_given_type(
                        al, ASRUtils::expr_type(dim)), ASRUtils::expr_type(dim), nullptr));
            }

            tmp = ASR::make_ArraySize_t(al, Lloc(x), callee, dim,
                ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)),
                nullptr);
        } else if (sf == SpecialFunc::Reshape) {
            size_t args_size = 0;
            for( size_t r = 0; r < args.size(); r++ ) {
                if( args.p[r] != nullptr ) {
                    args_size += 1;
                }
            }
            if( args_size != 1 ) {
                throw std::runtime_error("Calling xt::xtensor::reshape should be called only with target shape.");
            }

            ASR::expr_t* target_shape = args.p[0];
            size_t target_shape_rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(target_shape));
            if( target_shape_rank != 1 ) {
                throw std::runtime_error("Target shape must be an array of one dimension only.");
            }
            size_t target_rank = ASRUtils::get_fixed_size_of_array(ASRUtils::expr_type(target_shape));
            size_t callee_rank = ASRUtils::extract_n_dims_from_ttype(ASRUtils::expr_type(callee));
            if( target_rank <= 0 ) {
                throw std::runtime_error("Target shape must be a constant sized array of one dimension.");
            }
            if( target_rank != callee_rank ) {
                throw std::runtime_error("Ranks must be same before and after reshaping.");
            }
            Vec<ASR::dimension_t> empty_dims;
            empty_dims.reserve(al, target_rank);
            for( size_t r = 0; r < target_rank; r++ ) {
                ASR::dimension_t dim;
                dim.loc = Lloc(x);
                dim.m_length = nullptr;
                dim.m_start = nullptr;
                empty_dims.push_back(al, dim);
            }

            ASR::array_physical_typeType target_physical_type = ASR::array_physical_typeType::DescriptorArray;
            bool override_physical_type = false;
            if( ASRUtils::extract_physical_type(ASRUtils::expr_type(callee)) ==
                ASR::array_physical_typeType::FixedSizeArray ) {
                target_physical_type = ASR::array_physical_typeType::FixedSizeArray;
                override_physical_type = true;
            }
            ASR::ttype_t* target_type = ASRUtils::make_Array_t_util(al, Lloc(x),
                ASRUtils::extract_type(ASRUtils::expr_type(callee)), empty_dims.p, empty_dims.size(),
                ASR::abiType::Source, false, target_physical_type, override_physical_type, false);
            ASR::expr_t* new_shape = ASRUtils::cast_to_descriptor(al, args.p[0]);
            tmp = ASR::make_ArrayReshape_t(al, Lloc(x), callee, new_shape, target_type, nullptr);
            tmp = ASR::make_Assignment_t(al, Lloc(x), callee, ASRUtils::EXPR(tmp.get()), nullptr);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Size) {
            if( args.size() != 0 ) {
                throw std::runtime_error("xt::xtensor::size/std::vector::size should be called with only one argument.");
            }

            if( ASRUtils::is_array(ASRUtils::expr_type(callee)) ) {
                tmp = ASR::make_ArraySize_t(al, Lloc(x), callee, nullptr,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)),
                    nullptr);
            } else if( ASR::is_a<ASR::List_t>(*ASRUtils::expr_type(callee)) ) {
                tmp = ASR::make_ListLen_t(al, Lloc(x), callee,
                    ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)), nullptr);
            } else {
                throw std::runtime_error("Only xt::xtensor::size and std::vector::size are supported.");
            }
        } else if (sf == SpecialFunc::Fill) {
            if( args.size() != 1 ) {
                throw std::runtime_error("xt::xtensor::fill should be called with only one argument.");
            }
            if( ASRUtils::is_array(ASRUtils::expr_type(args.p[0])) ) {
                throw std::runtime_error("Argument of xt::xtensor::fill should be a scalar.");
            }

            ASR::expr_t* arg = args.p[0];
            ASRUtils::make_ArrayBroadcast_t_util(al, Lloc(x), callee, arg);
            tmp = ASR::make_Assignment_t(al, Lloc(x), callee, arg, nullptr);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::TorchTensorItem) {
            if( args.size() != 0 ) {
                throw std::runtime_error("torch::Tensor::item shouldn't be called with any argument.");
            }
            if( ASRUtils::is_array(ASRUtils::expr_type(callee)) ) {
                throw std::runtime_error("Callee of torch::Tensor::item should be a scalar.");
            }

            tmp = reinterpret_cast<ASR::asr_t*>(callee);
            is_stmt_created = false;
        } else if (sf == SpecialFunc::TorchOnes) {
            if( args.size() != 2 ) { // second one is TorchOptions, to be ignored
                throw std::runtime_error("torch::ones should be called with only one argument.");
            }

            ASR::expr_t* shape_arg = args.p[0];
            ASR::expr_t* one = ASRUtils::get_constant_one_with_given_type(
                al, ASRUtils::TYPE(ASR::make_Real_t(al, Lloc(x), 8)));
            Vec<ASR::dimension_t> expr_dims; expr_dims.reserve(al, 1);
            if( ASR::is_a<ASR::IntegerConstant_t>(*shape_arg) ) {
                ASR::dimension_t expr_dim;
                expr_dim.loc = Lloc(x);
                expr_dim.m_start = ASRUtils::get_constant_zero_with_given_type(
                    al, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)));
                expr_dim.m_length = shape_arg;
                expr_dims.push_back(al, expr_dim);
                ASR::ttype_t* type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
                    ASRUtils::TYPE(ASR::make_Real_t(al, Lloc(x), 8)), expr_dims.p,
                    expr_dims.size(), ASR::array_physical_typeType::FixedSizeArray));
                int num_ones = ASR::down_cast<ASR::IntegerConstant_t>(shape_arg)->m_n;
                Vec<ASR::expr_t*> ones_vec; ones_vec.reserve(al, num_ones);
                for( size_t onei = 0; onei < num_ones; onei++ ) {
                    ones_vec.push_back(al, one);
                }
                tmp = ASRUtils::make_ArrayConstructor_t_util(al, Lloc(x), ones_vec.p, ones_vec.size(),
                    type, ASR::arraystorageType::RowMajor);
                is_stmt_created = false;
            } else if( ASR::is_a<ASR::ArrayConstant_t>(*shape_arg) ) {
                throw std::runtime_error("{...} not yet supported in torch::ones");
            }
            is_stmt_created = false;
        } else if( sf == SpecialFunc::All ) {
            // Handles xt::all() - no arguments
            // Handle with argument case later.
            is_all_called = true;
            tmp = nullptr;
        } else if( sf == SpecialFunc::Range ) {
            if( args.size() < 2 ) {
                throw std::runtime_error("xt::range accepts at least 2 arguments.");
            }
            range_start = args.p[0];
            range_end = args.p[1];
            if( args.size() > 2 ) {
                range_step = args.p[2];
            } else {
                range_step = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                    al, args.p[0]->base.loc, 1, ASRUtils::expr_type(args.p[0])));
            }
            is_range_called = true;
            tmp = nullptr;
        } else if( sf == SpecialFunc::Any ) {
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::Any),
                args.p, args.size(), 0, ASRUtils::TYPE(ASR::make_Logical_t(
                    al, Lloc(x), 4)), nullptr);
        } else if( sf == SpecialFunc::Exp ) {
            tmp = ASRUtils::make_IntrinsicElementalFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::Exp),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::Pow ) {
            if( args.size() != 2 ) {
                throw std::runtime_error("Pow accepts exactly two arguments.");
            }

            CreateBinOp(args.p[0], args.p[1], ASR::binopType::Pow, Lloc(x));
        } else if( sf == SpecialFunc::Sqrt ) {
            tmp = ASRUtils::make_IntrinsicElementalFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::Sqrt),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::Abs ) {
            tmp = ASRUtils::make_IntrinsicElementalFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::Abs),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::Sin ) {
            tmp = ASRUtils::make_IntrinsicElementalFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::Sin),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::Cos ) {
            tmp = ASRUtils::make_IntrinsicElementalFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::Cos),
                args.p, args.size(), 0, ASRUtils::expr_type(args.p[0]), nullptr);
        } else if( sf == SpecialFunc::AMax ) {
            if( args.size() > 1 && args.p[1] != nullptr ) {
                throw std::runtime_error("dim argument not yet supported with " + func_name);
            }
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::MaxVal),
                args.p, 1, 0, ASRUtils::extract_type(ASRUtils::expr_type(args.p[0])),
                nullptr);
        } else if( sf == SpecialFunc::AMin ) {
            if( args.size() > 1 && args.p[1] != nullptr ) {
                throw std::runtime_error("dim argument not yet supported with " + func_name);
            }
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::MinVal),
                args.p, 1, 0, ASRUtils::extract_type(ASRUtils::expr_type(args.p[0])),
                nullptr);
        } else if( sf == SpecialFunc::Sum ) {
            if( args.size() > 1 && args.p[1] != nullptr ) {
                throw std::runtime_error("dim argument not yet supported with " + func_name);
            }
            tmp = ASRUtils::make_IntrinsicArrayFunction_t_util(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicArrayFunctions::Sum),
                args.p, 1, 0, ASRUtils::extract_type(ASRUtils::expr_type(args.p[0])),
                nullptr);
        } else if( sf == SpecialFunc::NotEqual ) {
            ASR::expr_t* arg1 = args.p[0];
            ASR::expr_t* arg2 = args.p[1];
            CreateCompareOp(arg1, arg2, ASR::cmpopType::NotEq, Lloc(x));
        } else if( sf == SpecialFunc::Equal ) {
            ASR::expr_t* arg1 = args.p[0];
            ASR::expr_t* arg2 = args.p[1];
            CreateCompareOp(arg1, arg2, ASR::cmpopType::Eq, Lloc(x));
        } else if (sf == SpecialFunc::Exit) {
            LCOMPILERS_ASSERT(args.size() == 1);
            int code = 0;
            ASRUtils::extract_value(args[0], code);
            if( code != 0 ) {
                tmp = ASR::make_ErrorStop_t(al, Lloc(x), args[0]);
            } else {
                tmp = ASR::make_Stop_t(al, Lloc(x), args[0]);
            }
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Empty) {
            if( args.size() != 1 ) {
                throw std::runtime_error("xt::empty must be provided with shape.");
            }
            if( assignment_target == nullptr ) {
                throw std::runtime_error("xt::empty should be used only in assignment statement.");
            }
            if( !ASRUtils::is_allocatable(assignment_target) ) {
                throw std::runtime_error("Assignment target must be an alloctable");
            }

            ASR::expr_t** a_m_args = nullptr; size_t n_m_args = 0;
            if( ASR::is_a<ASR::ArrayConstant_t>(*args.p[0]) ) {
                ASR::ArrayConstant_t* array_constant = ASR::down_cast<ASR::ArrayConstant_t>(args.p[0]);
                a_m_args = array_constant->m_args;
                n_m_args = array_constant->n_args;
            } else if( ASR::is_a<ASR::ArrayConstructor_t>(*args.p[0]) ) {
                ASR::ArrayConstructor_t* array_constructor = ASR::down_cast<ASR::ArrayConstructor_t>(args.p[0]);
                a_m_args = array_constructor->m_args;
                n_m_args = array_constructor->n_args;
            } else {
                throw std::runtime_error("Only {...} is allowed for supplying shape to xt::empty.");
            }

            size_t target_rank = ASRUtils::extract_n_dims_from_ttype(
                ASRUtils::expr_type(assignment_target));
            if( n_m_args != target_rank ) {
                throw std::runtime_error("Assignment target must be of same rank as the size of the shape array.");
            }

            Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
            ASR::alloc_arg_t alloc_arg; alloc_arg.loc = Lloc(x);
            alloc_arg.m_a = assignment_target;
            alloc_arg.m_len_expr = nullptr; alloc_arg.m_type = nullptr;
            Vec<ASR::dimension_t> alloc_dims; alloc_dims.reserve(al, target_rank);
            for( size_t i = 0; i < target_rank; i++ ) {
                ASR::dimension_t alloc_dim;
                alloc_dim.loc = Lloc(x);
                alloc_dim.m_length = a_m_args[i];
                alloc_dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                    al, Lloc(x), 0, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
                alloc_dims.push_back(al, alloc_dim);
            }
            alloc_arg.m_dims = alloc_dims.p; alloc_arg.n_dims = alloc_dims.size();
            alloc_args.push_back(al, alloc_arg);
            current_body->push_back(al, ASRUtils::STMT(ASR::make_Allocate_t(al, Lloc(x),
                alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr)));
            tmp = nullptr;
            is_stmt_created = true;
        } else if (sf == SpecialFunc::TorchEmpty) {
            if( args.size() < 1 ) { // Ignore the last two
                throw std::runtime_error("torch::empty must be provided with shape.");
            }
            if( assignment_target != nullptr && ASRUtils::expr_intent(assignment_target) == ASR::intentType::Local) {
                throw std::runtime_error("torch::empty isn't handled in assignment statement yet.");
            }

            ASR::expr_t** a_m_args = nullptr; size_t n_m_args = 0;
            if( ASR::is_a<ASR::ArrayConstant_t>(*args.p[0]) ) {
                ASR::ArrayConstant_t* array_constant = ASR::down_cast<ASR::ArrayConstant_t>(args.p[0]);
                a_m_args = array_constant->m_args;
                n_m_args = array_constant->n_args;
            } else if( ASR::is_a<ASR::ArrayConstructor_t>(*args.p[0]) ) {
                ASR::ArrayConstructor_t* array_constructor = ASR::down_cast<ASR::ArrayConstructor_t>(args.p[0]);
                a_m_args = array_constructor->m_args;
                n_m_args = array_constructor->n_args;
            } else {
                throw std::runtime_error("Only {...} is allowed for supplying shape to xt::empty.");
            }

            Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, n_m_args);
            for( size_t idim = 0; idim < n_m_args; idim++ ) {
                ASR::dimension_t empty_dim;
                empty_dim.loc = Lloc(x);
                empty_dim.m_start = ASRUtils::get_constant_zero_with_given_type(
                    al, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)));
                empty_dim.m_length = nullptr;
                empty_dims.push_back(al, empty_dim);
            }
            ASR::ttype_t* type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
                ASRUtils::extract_type(ASRUtils::expr_type(assignment_target)),
                empty_dims.p, empty_dims.size(), ASR::array_physical_typeType::DescriptorArray));
            type = ASRUtils::TYPE(ASR::make_Allocatable_t(al, Lloc(x), type));
            ASR::down_cast<ASR::Variable_t>(
                ASR::down_cast<ASR::Var_t>(assignment_target)->m_v)->m_type = type;
            tmp = nullptr;
            is_stmt_created = false;
        } else if (sf == SpecialFunc::TorchFromBlob) {
            if( args.size() < 2 ) { // Ignore the last one
                throw std::runtime_error("torch::from must be provided with C array and its shape.");
            }

            ASR::dimension_t* m_dims = nullptr;
            size_t n_dims = ASRUtils::extract_dimensions_from_ttype(ASRUtils::expr_type(args.p[0]), m_dims);
            if( ASR::is_a<ASR::ArrayConstant_t>(*args.p[1]) ) {
                ASR::ArrayConstant_t* array_constant = ASR::down_cast<ASR::ArrayConstant_t>(args.p[1]);

                Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, array_constant->n_args);
                for( size_t idim = 0; idim < array_constant->n_args; idim++ ) {
                    if( !ASRUtils::is_value_equal(array_constant->m_args[idim], m_dims[idim].m_length) ) {
                        throw std::runtime_error("ICE: Could not decipher the equality of the shape "
                            "and shape of the array provided in torch::from_blob");
                    }
                }
                tmp = reinterpret_cast<ASR::asr_t*>(args.p[0]);
                is_stmt_created = false;
            } else {
                throw std::runtime_error("Only {...} is allowed for supplying shape to torch::from_blob.");
            }
        } else if (sf == SpecialFunc::Iota) {
            tmp = ASR::make_ComplexConstant_t(al, Lloc(x), 0.0, 1.0,
                ASRUtils::TYPE(ASR::make_Complex_t(al, Lloc(x), 8)));
        } else if (sf == SpecialFunc::PushBack) {
            if( args.size() != 1 ) {
                throw std::runtime_error("std::vector::push_back should be called with only one argument.");
            }

            if( !ASRUtils::check_equal_type(
                    ASRUtils::get_contained_type(ASRUtils::expr_type(callee)),
                    ASRUtils::expr_type(args[0])) ) {
                throw std::runtime_error(std::string("std::vector::push_back argument must have same type as element ") +
                    "type of list, found " + ASRUtils::type_to_str(ASRUtils::expr_type(args[0])));
            }

            tmp = ASR::make_ListAppend_t(al, Lloc(x), callee, args[0]);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Clear) {
            if( args.size() > 1 ) {
                throw std::runtime_error("std::vector::clear should be called with only one argument.");
            }

            tmp = ASR::make_ListClear_t(al, Lloc(x), callee);
            is_stmt_created = true;
        } else if (sf == SpecialFunc::Data) {
            if( args.size() > 0 ) {
                throw std::runtime_error("std::vector::data should be called with only one argument.");
            }

            Vec<ASR::dimension_t> dims; dims.reserve(al, 1);
            ASR::dimension_t dim;
            dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                al, Lloc(x), 0, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
            dim.m_length = nullptr;
            dims.push_back(al, dim);
            tmp = ASRUtils::make_Cast_t_value(al, Lloc(x), callee, ASR::cast_kindType::ListToArray,
                ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x), ASRUtils::get_contained_type(ASRUtils::expr_type(callee)),
                dims.p, dims.size(), ASR::array_physical_typeType::UnboundedPointerToDataArray)));
        } else if (sf == SpecialFunc::Reserve) {
            if( args.size() > 1 ) {
                throw std::runtime_error("std::vector::reserve should be called with only one argument.");
            }

            Vec<ASR::expr_t*> args_with_list; args_with_list.reserve(al, 2);
            args_with_list.push_back(al, callee);
            args_with_list.push_back(al, args[0]);
            tmp = ASR::make_Expr_t(al, Lloc(x), ASRUtils::EXPR(ASR::make_IntrinsicElementalFunction_t(al, Lloc(x),
                static_cast<int64_t>(ASRUtils::IntrinsicElementalFunctions::ListReserve), args_with_list.p,
                args_with_list.size(), 0, nullptr, nullptr)));
            is_stmt_created = true;
        } else {
            throw std::runtime_error("Only printf and exit special functions supported");
        }
        return true;
    }

    bool TraverseCallExpr(clang::CallExpr *x) {
        std::string member_name = member_name_obj.get();
        TraverseStmt(x->getCallee());
        ASR::expr_t* callee = nullptr;
        if( tmp != nullptr ) {
            callee = ASRUtils::EXPR(tmp.get());
        }
        if( check_and_handle_special_function(x, callee) ) {
            member_name_obj.set(member_name);
            return true;
        }

        clang::Expr** args = x->getArgs();
        size_t n_args = x->getNumArgs();
        Vec<ASR::call_arg_t> call_args;
        call_args.reserve(al, n_args);
        for( size_t i = 0; i < n_args; i++ ) {
            TraverseStmt(args[i]);
            ASR::expr_t* arg = ASRUtils::EXPR(tmp.get());
            ASR::call_arg_t call_arg;
            call_arg.loc = arg->base.loc;
            call_arg.m_value = arg;
            call_args.push_back(al, call_arg);
        }

        ASR::Var_t* callee_Var = ASR::down_cast<ASR::Var_t>(callee);
        ASR::symbol_t* callee_sym = callee_Var->m_v;
        const clang::QualType& qual_type = x->getCallReturnType(*Context);
        ASR::ttype_t* return_type = ClangTypeToASRType(qual_type);
        if( return_type == nullptr ) {
            tmp = ASRUtils::make_SubroutineCall_t_util(al, Lloc(x), callee_sym,
                callee_sym, call_args.p, call_args.size(), nullptr,
                nullptr, false, false);
            is_stmt_created = true;
        } else {
            tmp = ASRUtils::make_FunctionCall_t_util(al, Lloc(x), callee_sym,
                callee_sym, call_args.p, call_args.size(), return_type,
                nullptr, nullptr);
        }
        member_name_obj.set(member_name);
        return true;
    }

    bool TraverseFunctionDecl(clang::FunctionDecl *x) {
        SymbolTable* parent_scope = current_scope;
        std::string name = x->getName().str();
        ASR::symbol_t* current_function_symbol = parent_scope->resolve_symbol(name);
        ASR::Function_t* current_function = nullptr;

        if( current_function_symbol == nullptr ) {
            current_scope = al.make_new<SymbolTable>(parent_scope);
            Vec<ASR::expr_t*> args;
            args.reserve(al, 1);
            for (auto &p : x->parameters()) {
                TraverseDecl(p);
                args.push_back(al, ASRUtils::EXPR(tmp.get()));
            }

            ASR::ttype_t* return_type = ClangTypeToASRType(x->getReturnType());
            ASR::symbol_t* return_sym = nullptr;
            ASR::expr_t* return_var = nullptr;
            if (return_type != nullptr) {
                return_sym = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
                    current_scope, s2c(al, "__return_var"), nullptr, 0, ASR::intentType::ReturnVar, nullptr, nullptr,
                    ASR::storage_typeType::Default, return_type, nullptr, ASR::abiType::Source, ASR::accessType::Public,
                    ASR::presenceType::Required, false));
                current_scope->add_symbol("__return_var", return_sym);
            }

            if (return_type != nullptr) {
                return_var = ASRUtils::EXPR(ASR::make_Var_t(al, return_sym->base.loc, return_sym));
            }

            tmp = ASRUtils::make_Function_t_util(al, Lloc(x), current_scope, s2c(al, name),
                nullptr, 0, args.p, args.size(), nullptr, 0, return_var, ASR::abiType::Source,
                ASR::accessType::Public, ASR::deftypeType::Implementation, nullptr, false, false,
                false, false, false, nullptr, 0, false, false, false);
            current_function_symbol = ASR::down_cast<ASR::symbol_t>(tmp.get());
            current_function = ASR::down_cast<ASR::Function_t>(current_function_symbol);
            current_scope = current_function->m_symtab;
            parent_scope->add_symbol(name, current_function_symbol);
        } else {
            current_function = ASR::down_cast<ASR::Function_t>(current_function_symbol);
            current_scope = current_function->m_symtab;
            for( size_t i = 0; i < current_function->n_args; i++ ) {
                ASR::Var_t* argi = ASR::down_cast<ASR::Var_t>(current_function->m_args[i]);
                if( current_scope->get_symbol(ASRUtils::symbol_name(argi->m_v)) == argi->m_v ) {
                    current_scope->erase_symbol(ASRUtils::symbol_name(argi->m_v));
                }
            }

            int i = 0;
            for (auto &p : x->parameters()) {
                TraverseDecl(p);
                current_function->m_args[i] = ASRUtils::EXPR(tmp.get());
                i++;
            }
        }

        Vec<ASR::stmt_t*>* current_body_copy = current_body;
        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        current_body = &body;
        if( x->doesThisDeclarationHaveABody() ) {
            TraverseStmt(x->getBody());
        }
        current_body = current_body_copy;
        current_function->m_body = body.p;
        current_function->n_body = body.size();
        current_scope = parent_scope;
        is_stmt_created = false;
        return true;
    }

    bool TraverseDeclStmt(clang::DeclStmt* x) {
        if( x->isSingleDecl() ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseDeclStmt(x);
        }

        clang::DeclGroup& decl_group = x->getDeclGroup().getDeclGroup();
        for( size_t i = 0; i < decl_group.size(); i++ ) {
            TraverseDecl(decl_group[i]);
            if( is_stmt_created ) {
                current_body->push_back(al, ASRUtils::STMT(tmp.get()));
            }
        }
        is_stmt_created = false;
        return true;
    }

    bool TraverseCXXConstructExpr(clang::CXXConstructExpr* x) {
        ThirdPartyCPPArrayTypes third_party_array_type;
        bool is_third_party_array_type = false;
        Vec<ASR::dimension_t> shape_result; shape_result.reserve(al, 1);
        ASR::ttype_t* constructor_type = ClangTypeToASRType(x->getType(), &shape_result,
            &third_party_array_type, &is_third_party_array_type);
        if( is_third_party_array_type && third_party_array_type == ThirdPartyCPPArrayTypes::MDSpanArray ) {
            if( x->getNumArgs() == 0 ) {
                tmp = nullptr;
                is_stmt_created = false;
                return true;
            }
            if( x->getNumArgs() == 1 ) {
                if( shape_result.size() != ASRUtils::extract_n_dims_from_ttype(constructor_type) ) {
                    throw std::runtime_error("Allocation sizes for all dimensions not provided.");
                }
                if( !ASRUtils::is_fixed_size_array(shape_result.p, shape_result.size()) ) {
                    throw std::runtime_error("Allocation size of Kokkos::mdspan array isn't specified correctly.");
                }

                Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
                ASR::alloc_arg_t alloc_arg;
                alloc_arg.loc = Lloc(x);
                LCOMPILERS_ASSERT(assignment_target != nullptr);
                alloc_arg.m_a = assignment_target;
                alloc_arg.m_dims = shape_result.p; alloc_arg.n_dims = shape_result.size();
                alloc_arg.m_len_expr = nullptr; alloc_arg.m_type = nullptr;
                alloc_args.push_back(al, alloc_arg);
                Vec<ASR::expr_t*> dealloc_args; dealloc_args.reserve(al, 1);
                dealloc_args.push_back(al, assignment_target);
                current_body->push_back(al, ASRUtils::STMT(ASR::make_ExplicitDeallocate_t(
                    al, Lloc(x), dealloc_args.p, dealloc_args.size())));
                current_body->push_back(al, ASRUtils::STMT(ASR::make_Allocate_t(al, Lloc(x),
                    alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr)));
                tmp = nullptr;
                is_stmt_created = true;
                return true;
            } else if( x->getNumArgs() >= 1 ) {
                clang::Expr* _data = x->getArg(0);
                TraverseStmt(_data);
                ASR::expr_t* list_to_array = ASRUtils::EXPR(tmp.get());
                if( !ASR::is_a<ASR::Cast_t>(*list_to_array) ||
                    ASR::down_cast<ASR::Cast_t>(list_to_array)->m_kind !=
                    ASR::cast_kindType::ListToArray) {
                    throw std::runtime_error("First argument of Kokkos::mdspan "
                        "constructor should be a call to std::vector::data() function.");
                } else {
                    mark_as_read_only(list_to_array);
                }
                if( ASRUtils::extract_n_dims_from_ttype(constructor_type) != x->getNumArgs() - 1 ) {
                    throw std::runtime_error("Shape provided in the constructor "
                        "doesn't match with the rank of the array.");
                }
                Vec<ASR::dimension_t> alloc_dims; alloc_dims.reserve(al, x->getNumArgs() - 1);
                Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
                for( int i = 1; i < x->getNumArgs(); i++ ) {
                    clang::Expr* argi = x->getArg(i);
                    TraverseStmt(argi);
                    ASR::dimension_t alloc_dim;
                    alloc_dim.loc = Lloc(x);
                    alloc_dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x), 0,
                        ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
                    alloc_dim.m_length = ASRUtils::EXPR(tmp.get());
                    alloc_dims.push_back(al, alloc_dim);
                }
                ASR::alloc_arg_t alloc_arg;
                alloc_arg.loc = Lloc(x);
                LCOMPILERS_ASSERT(assignment_target != nullptr);
                alloc_arg.m_a = assignment_target;
                alloc_arg.m_dims = alloc_dims.p; alloc_arg.n_dims = alloc_dims.size();
                alloc_arg.m_len_expr = nullptr; alloc_arg.m_type = nullptr;
                alloc_args.push_back(al, alloc_arg);
                Vec<ASR::expr_t*> dealloc_args; dealloc_args.reserve(al, 1);
                dealloc_args.push_back(al, assignment_target);
                current_body->push_back(al, ASRUtils::STMT(ASR::make_ExplicitDeallocate_t(
                    al, Lloc(x), dealloc_args.p, dealloc_args.size())));
                current_body->push_back(al, ASRUtils::STMT(ASR::make_Allocate_t(al, Lloc(x),
                    alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr)));
                tmp = reinterpret_cast<ASR::asr_t*>(list_to_array);
                is_stmt_created = false;
                return true;
            } else {
                throw std::runtime_error("Kokkos::mdspan constructor is called "
                    "with incorrect number of arguments, expected " + std::to_string(
                        ASRUtils::extract_n_dims_from_ttype(constructor_type) + 1) +
                        " and found, " + std::to_string(x->getNumArgs()));
            }
        } else if( constructor_type == nullptr ) {
            clang::Expr* x_ = x->getArg(0);
            return TraverseStmt(x_);
        } else if( x->getNumArgs() >= 0 ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseCXXConstructExpr(x);
        }
        tmp = nullptr;
        return true;
    }

    void add_reshape_if_needed(ASR::expr_t*& expr, ASR::expr_t* target_expr) {
        if( ASR::is_a<ASR::List_t>(*ASRUtils::expr_type(target_expr)) ||
            ((ASR::is_a<ASR::Cast_t>(*expr) &&
             ASR::down_cast<ASR::Cast_t>(expr)->m_kind == ASR::cast_kindType::ListToArray)) ) {
            return ;
        }
        ASR::ttype_t* expr_type = ASRUtils::expr_type(expr);
        ASR::ttype_t* target_expr_type = ASRUtils::expr_type(target_expr);
        ASR::dimension_t *expr_dims = nullptr, *target_expr_dims = nullptr;
        size_t expr_rank = ASRUtils::extract_dimensions_from_ttype(expr_type, expr_dims);
        size_t target_expr_rank = ASRUtils::extract_dimensions_from_ttype(target_expr_type, target_expr_dims);
        if( expr_rank == target_expr_rank ) {
            return ;
        }

        const Location& loc = expr->base.loc;
        Vec<ASR::expr_t*> new_shape_; new_shape_.reserve(al, target_expr_rank);
        for( size_t i = 0; i < target_expr_rank; i++ ) {
            new_shape_.push_back(al, ASRUtils::get_size(target_expr, i + 1, al));
        }

        Vec<ASR::dimension_t> new_shape_dims; new_shape_dims.reserve(al, 1);
        ASR::dimension_t new_shape_dim; new_shape_dim.loc = loc;
        new_shape_dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            target_expr_rank, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_shape_dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_shape_dims.push_back(al, new_shape_dim);
        ASR::ttype_t* new_shape_type = ASRUtils::TYPE(ASR::make_Array_t(al, loc,
            ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4)), new_shape_dims.p,
            new_shape_dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        ASR::expr_t* new_shape = ASRUtils::EXPR(ASRUtils::make_ArrayConstructor_t_util(al, loc,
            new_shape_.p, new_shape_.size(), new_shape_type, ASR::arraystorageType::RowMajor));
        new_shape = ASRUtils::cast_to_descriptor(al, new_shape);

        ASR::ttype_t* reshaped_expr_type = target_expr_type;
        if( ASRUtils::is_fixed_size_array(expr_type) ) {
            reshaped_expr_type = ASRUtils::duplicate_type_with_empty_dims(al,
                ASRUtils::type_get_past_allocatable(
                    ASRUtils::type_get_past_pointer(target_expr_type)),
                ASR::array_physical_typeType::FixedSizeArray, true);
        }
        ASR::expr_t* reshaped_expr = ASRUtils::EXPR(ASR::make_ArrayReshape_t(al, loc, expr,
            new_shape, reshaped_expr_type, nullptr));
        expr = reshaped_expr;
    }

    bool TraverseCXXTemporaryObjectExpr(clang::CXXTemporaryObjectExpr *x) {
        if( !x->getConstructor()->isConstexpr() ) {
            throw std::runtime_error("Constructors for user-define types "
                                     "must be defined with constexpr.");
        }
        if( static_cast<clang::CompoundStmt*>(x->getConstructor()->getBody())->size() > 0 ) {
            throw std::runtime_error("Constructor for user-defined must have empty body.");
        }
        std::string type_name = x->getConstructor()->getNameAsString();
        ASR::symbol_t* s = current_scope->resolve_symbol(type_name);
        if( s == nullptr ) {
            throw std::runtime_error(type_name + " not found in current scope.");
        }

        ASR::StructType_t* struct_type_t = ASR::down_cast<ASR::StructType_t>(s);
        Vec<ASR::call_arg_t> struct_constructor_args;
        struct_constructor_args.reserve(al, x->getNumArgs());
        for( size_t i = 0; i < x->getNumArgs(); i++ ) {
            TraverseStmt(x->getArg(i));
            ASR::call_arg_t call_arg;
            call_arg.loc = Lloc(x);
            call_arg.m_value = ASRUtils::EXPR(tmp.get());
            if( !ASRUtils::is_value_constant(ASRUtils::expr_value(call_arg.m_value)) ) {
                throw std::runtime_error("Constructor for user-defined types "
                    "must be initialised with constant values, " + std::to_string(i) +
                    "-th argument is not a constant.");
            }
            ASR::ttype_t* orig_type = ASRUtils::symbol_type(
                struct_type_t->m_symtab->resolve_symbol(struct_type_t->m_members[i]));
            ASR::ttype_t* arg_type = ASRUtils::expr_type(call_arg.m_value);
            if( !ASRUtils::types_equal(orig_type, arg_type, true) ) {
                throw std::runtime_error(type_name + "'s " + std::to_string(i) +
                    "-th constructor argument's type, " + ASRUtils::type_to_str(arg_type) +
                    ", is not compatible with " + std::string(struct_type_t->m_members[i]) +
                    " member's type, " + ASRUtils::type_to_str(orig_type));
            }
            struct_constructor_args.push_back(al, call_arg);
        }
        tmp = ASR::make_StructTypeConstructor_t(al, Lloc(x), s, struct_constructor_args.p,
            struct_constructor_args.size(), ClangTypeToASRType(x->getType()), nullptr);
        return true;
    }

    void TraverseAPValue(clang::APValue& field) {
        Location loc;
        loc.first = 1; loc.last = 1;
        switch( field.getKind() ) {
            case clang::APValue::Int: {
                tmp = ASR::make_IntegerConstant_t(al, loc, field.getInt().getLimitedValue(),
                    ASRUtils::TYPE(ASR::make_Integer_t(al, loc, field.getInt().getBitWidth()/8)));
                break;
            }
            case clang::APValue::Float: {
                tmp = ASR::make_RealConstant_t(al, loc, field.getFloat().convertToDouble(),
                    ASRUtils::TYPE(ASR::make_Real_t(al, loc, 8)));
                break;
            }
            default: {
                throw std::runtime_error("APValue not supported for clang::APValue::" +
                                         std::to_string(field.getKind()));
            }
        }
    }

    void evaluate_compile_time_value_for_Var(clang::APValue* ap_value, ASR::symbol_t* v) {
        switch( ap_value->getKind() ) {
            case clang::APValue::Struct: {
                ASR::ttype_t* v_type = ASRUtils::type_get_past_const(ASRUtils::symbol_type(v));
                if( !ASR::is_a<ASR::Struct_t>(*v_type) ) {
                    // throw std::runtime_error("Expected ASR::Struct_t type found, " +
                    //     ASRUtils::type_to_str(v_type));
                    // No error, just return, clang marking something as Struct
                    // doesn't mean that it necessarily maps to ASR Struct, it can
                    // map to ASR Array type as well
                    return ;
                }
                ASR::Struct_t* struct_t = ASR::down_cast<ASR::Struct_t>(v_type);
                ASR::StructType_t* struct_type_t = ASR::down_cast<ASR::StructType_t>(
                    ASRUtils::symbol_get_past_external(struct_t->m_derived_type));
                for( size_t i = 0; i < ap_value->getStructNumFields(); i++ ) {
                    clang::APValue& field = ap_value->getStructField(i);
                    TraverseAPValue(field);
                    struct2member_inits[v][struct_type_t->m_members[i]] = ASRUtils::EXPR(tmp.get());
                }
                break;
            }
            default: {
                break;
            }
        }
    }

    bool TraverseVarDecl(clang::VarDecl *x) {
        std::string name = x->getName().str();
        if( scopes.size() > 0 ) {
            if( scopes.back().find(name) != scopes.back().end() ) {
                throw std::runtime_error(name + std::string(" is already defined."));
            } else {
                std::string aliased_name = current_scope->get_unique_name(name);
                scopes.back()[name] = aliased_name;
                name = aliased_name;
            }
        } else {
            if( current_scope->resolve_symbol(name) ) {
                throw std::runtime_error(name + std::string(" is already defined."));
            }
        }
        Vec<ASR::dimension_t> xshape_result; xshape_result.reserve(al, 0);
        ThirdPartyCPPArrayTypes array_type; bool is_third_party_array_type = false;
        ASR::ttype_t *asr_type = ClangTypeToASRType(x->getType(), &xshape_result,
            &array_type, &is_third_party_array_type);
        if( is_third_party_array_type &&
            array_type == ThirdPartyCPPArrayTypes::PyTorchArray ) {
            if( !x->hasInit() ) {
                throw std::runtime_error("torch::Tensor variables must have initialiser value.");
            }
        }
        ASR::symbol_t *v = ASR::down_cast<ASR::symbol_t>(ASR::make_Variable_t(al, Lloc(x),
            current_scope, s2c(al, name), nullptr, 0, ASR::intentType::Local, nullptr, nullptr,
            ASR::storage_typeType::Default, asr_type, nullptr, ASR::abiType::Source,
            ASR::accessType::Public, ASR::presenceType::Required, false));
        current_scope->add_symbol(name, v);
        is_stmt_created = false;
        if (x->hasInit()) {
            tmp = nullptr;
            ASR::expr_t* var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), v));
            if( ASR::is_a<ASR::List_t>(*asr_type) ) {
                interpret_init_list_expr_as_list = true;
            }
            assignment_target = var;
            TraverseStmt(x->getInit());
            interpret_init_list_expr_as_list = false;
            assignment_target = nullptr;
            if( tmp != nullptr && !is_stmt_created ) {
                ASR::expr_t* init_val = ASRUtils::EXPR(tmp.get());
                if( ASR::is_a<ASR::Const_t>(*asr_type) ) {
                    if( !ASRUtils::is_value_constant(ASRUtils::expr_value(init_val)) &&
                        !ASR::is_a<ASR::StructTypeConstructor_t>(*init_val) ) {
                        throw std::runtime_error("Initialisation expression of "
                            "constant variables must reduce to a constant value.");
                    }

                    ASR::Variable_t* variable_t = ASR::down_cast<ASR::Variable_t>(v);
                    variable_t->m_symbolic_value = init_val;
                    variable_t->m_value = ASRUtils::expr_value(init_val);
                    variable_t->m_storage = ASR::storage_typeType::Parameter;
                } else {
                    if( is_third_party_array_type ) {
                        if( array_type == ThirdPartyCPPArrayTypes::PyTorchArray ) {
                            ASR::dimension_t* dims = nullptr;
                            size_t n_dims = ASRUtils::extract_dimensions_from_ttype(
                                ASRUtils::expr_type(init_val), dims);

                            Vec<ASR::dimension_t> empty_dims; empty_dims.reserve(al, n_dims);
                            for( size_t dimi = 0; dimi < n_dims; dimi++ ) {
                                ASR::dimension_t empty_dim;
                                empty_dim.loc = Lloc(x);
                                empty_dim.m_start = ASRUtils::get_constant_zero_with_given_type(
                                    al, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)));
                                empty_dim.m_length = nullptr;
                                empty_dims.push_back(al, empty_dim);
                            }
                            ASR::Variable_t* variable_t = ASR::down_cast<ASR::Variable_t>(v);
                            variable_t->m_type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
                                ASRUtils::extract_type(variable_t->m_type), empty_dims.p, empty_dims.size(),
                                ASR::array_physical_typeType::DescriptorArray));
                            variable_t->m_type = ASRUtils::TYPE(ASR::make_Allocatable_t(
                                al, Lloc(x), variable_t->m_type));

                            Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
                            ASR::alloc_arg_t alloc_arg; alloc_arg.loc = Lloc(x);
                            alloc_arg.m_a = var;
                            alloc_arg.m_dims = dims; alloc_arg.n_dims = n_dims;
                            alloc_arg.m_len_expr = nullptr; alloc_arg.m_type = nullptr;
                            alloc_args.push_back(al, alloc_arg);
                            current_body->push_back(al, ASRUtils::STMT(ASR::make_Allocate_t(
                                al, Lloc(x), alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr)));
                        }
                    }
                    add_reshape_if_needed(init_val, var);
                    tmp = ASR::make_Assignment_t(al, Lloc(x), var, init_val, nullptr);
                    is_stmt_created = true;
                }
            } else {
                is_stmt_created = false;
                tmp = nullptr;
            }

            if( x->getEvaluatedValue() ) {
                clang::APValue* ap_value = x->getEvaluatedValue();
                evaluate_compile_time_value_for_Var(ap_value, v);
            }
        }
        return true;
    }

    template <typename T>
    void flatten_ArrayConstant(ASR::expr_t* array_constant) {
        if( !ASRUtils::is_array(ASRUtils::expr_type(array_constant)) ) {
            return ;
        }

        LCOMPILERS_ASSERT(ASR::is_a<T>(*array_constant));
        T* array_constant_t = ASR::down_cast<T>(array_constant);
        Vec<ASR::expr_t*> new_elements; new_elements.reserve(al, array_constant_t->n_args);
        for( size_t i = 0; i < array_constant_t->n_args; i++ ) {
            if( ASR::is_a<ASR::ArrayConstructor_t>(*array_constant_t->m_args[i]) ) {
                flatten_ArrayConstant<ASR::ArrayConstructor_t>(array_constant_t->m_args[i]);
            } else {
                flatten_ArrayConstant<ASR::ArrayConstant_t>(array_constant_t->m_args[i]);
            }
            if( ASR::is_a<T>(*array_constant_t->m_args[i]) ) {
                T* aci = ASR::down_cast<T>(array_constant_t->m_args[i]);
                for( size_t j = 0; j < aci->n_args; j++ ) {
                    new_elements.push_back(al, aci->m_args[j]);
                }
            } else {
                new_elements.push_back(al, array_constant_t->m_args[i]);
            }
        }
        array_constant_t->m_args = new_elements.p;
        array_constant_t->n_args = new_elements.size();
        Vec<ASR::dimension_t> new_dims; new_dims.reserve(al, 1);
        const Location& loc = array_constant->base.loc;
        ASR::dimension_t dim; dim.loc = loc;
        dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            new_elements.size(), ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, loc,
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, loc, 4))));
        new_dims.push_back(al, dim);
        ASR::ttype_t* new_type = ASRUtils::TYPE(ASR::make_Array_t(al, loc,
            ASRUtils::type_get_past_array(flatten_Array(array_constant_t->m_type)),
            new_dims.p, new_dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        array_constant_t->m_type = new_type;
    }

    bool extract_dimensions_from_array_type(ASR::ttype_t* array_type,
        Vec<ASR::dimension_t>& alloc_dims) {
        if( !ASRUtils::is_array(array_type) ) {
            return false;
        }

        ASR::Array_t* array_t = ASR::down_cast<ASR::Array_t>(
            ASRUtils::type_get_past_allocatable(
                ASRUtils::type_get_past_pointer(
                    ASRUtils::type_get_past_const(array_type))));
        for( int i = 0; i < array_t->n_dims; i++ ) {
            alloc_dims.push_back(al, array_t->m_dims[i]);
        }

        extract_dimensions_from_array_type(array_t->m_type, alloc_dims);
        return true;
    }

    void create_allocate_stmt(ASR::expr_t* var, ASR::ttype_t* array_type) {
        if( var == nullptr || !ASRUtils::is_allocatable(var) ) {
            return ;
        }

        const Location& loc = var->base.loc;
        Vec<ASR::dimension_t> alloc_dims; alloc_dims.reserve(al, 1);
        if( extract_dimensions_from_array_type(array_type, alloc_dims) ) {
            Vec<ASR::alloc_arg_t> alloc_args; alloc_args.reserve(al, 1);
            ASR::alloc_arg_t alloc_arg; alloc_arg.loc = loc;
            alloc_arg.m_a = var;
            alloc_arg.m_dims = alloc_dims.p; alloc_arg.n_dims = alloc_dims.size();
            alloc_arg.m_type = nullptr; alloc_arg.m_len_expr = nullptr;
            alloc_args.push_back(al, alloc_arg);
            ASR::stmt_t* allocate_stmt = ASRUtils::STMT(ASR::make_Allocate_t(al, loc,
                alloc_args.p, alloc_args.size(), nullptr, nullptr, nullptr));
            current_body->push_back(al, allocate_stmt);
        }
    }

    bool TraverseInitListExpr(clang::InitListExpr* x) {
        if( interpret_init_list_expr_as_list ) {
            Vec<ASR::expr_t*> list_elements;
            list_elements.reserve(al, x->getNumInits());
            clang::Expr** clang_inits = x->getInits();
            for( size_t i = 0; i < x->getNumInits(); i++ ) {
                TraverseStmt(clang_inits[i]);
                list_elements.push_back(al, ASRUtils::EXPR(tmp.get()));
            }
            ASR::ttype_t* type = ASRUtils::expr_type(list_elements[list_elements.size() - 1]);
            tmp = ASR::make_ListConstant_t(al, Lloc(x), list_elements.p, list_elements.size(),
                ASRUtils::TYPE(ASR::make_List_t(al, Lloc(x), type)));
            return true;
        }
        Vec<ASR::expr_t*> init_exprs;
        init_exprs.reserve(al, x->getNumInits());
        clang::Expr** clang_inits = x->getInits();
        ASR::expr_t* assignment_target_copy = assignment_target;
        assignment_target = nullptr;
        for( size_t i = 0; i < x->getNumInits(); i++ ) {
            TraverseStmt(clang_inits[i]);
            init_exprs.push_back(al, ASRUtils::EXPR(tmp.get()));
        }
        assignment_target = assignment_target_copy;
        ASR::ttype_t* type = ASRUtils::expr_type(init_exprs[init_exprs.size() - 1]);
        Vec<ASR::dimension_t> dims; dims.reserve(al, 1);
        ASR::dimension_t dim; dim.loc = Lloc(x);
        dim.m_length = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x),
            x->getNumInits(), ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
        dim.m_start = ASRUtils::EXPR(ASR::make_IntegerConstant_t(al, Lloc(x),
            0, ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4))));
        dims.push_back(al, dim);
        type = ASRUtils::TYPE(ASR::make_Array_t(al, Lloc(x),
            type, dims.p, dims.size(), ASR::array_physical_typeType::FixedSizeArray));
        ASR::expr_t* array_constant = ASRUtils::EXPR(ASRUtils::make_ArrayConstructor_t_util(al, Lloc(x),
            init_exprs.p, init_exprs.size(), type, ASR::arraystorageType::RowMajor));
        create_allocate_stmt(assignment_target, type);
        if( ASR::is_a<ASR::ArrayConstant_t>(*array_constant) ) {
            flatten_ArrayConstant<ASR::ArrayConstant_t>(array_constant);
        } else if( ASR::is_a<ASR::ArrayConstructor_t>(*array_constant) ) {
            flatten_ArrayConstant<ASR::ArrayConstructor_t>(array_constant);
        } else {
            throw std::runtime_error("Only ArrayConstant and ArrayConstructor can be flattened.");
        }
        tmp = (ASR::asr_t*) array_constant;
        return true;
    }

    void CreateCompareOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::cmpopType cmpop_type, const Location& loc) {
        ASR::ttype_t* left_type = ASRUtils::expr_type(lhs);
        ASR::ttype_t* right_type = ASRUtils::expr_type(rhs);
        if( ASR::is_a<ASR::Enum_t>(*left_type) ) {
            left_type = ASR::down_cast<ASR::EnumType_t>(
                ASR::down_cast<ASR::Enum_t>(left_type)->m_enum_type)->m_type;
        }
        if( ASR::is_a<ASR::Enum_t>(*right_type) ) {
            right_type = ASR::down_cast<ASR::EnumType_t>(
                ASR::down_cast<ASR::Enum_t>(right_type)->m_enum_type)->m_type;
        }
        cast_helper(lhs, rhs, false);
        ASRUtils::make_ArrayBroadcast_t_util(al, loc, lhs, rhs);
        ASR::dimension_t* m_dims;
        size_t n_dims = ASRUtils::extract_dimensions_from_ttype(left_type, m_dims);
        ASR::ttype_t* result_type = ASRUtils::TYPE(ASR::make_Logical_t(al, loc, 4));
        if( n_dims > 0 ) {
            result_type = ASRUtils::make_Array_t_util(al, loc, result_type, m_dims, n_dims);
        }
        if( ASRUtils::is_integer(*left_type) && ASRUtils::is_integer(*right_type) ) {
            tmp = ASR::make_IntegerCompare_t(al, loc, lhs, cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_real(*left_type) && ASRUtils::is_real(*right_type) ) {
            tmp = ASR::make_RealCompare_t(al, loc, lhs, cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_logical(*left_type) && ASRUtils::is_logical(*right_type) ) {
            tmp = ASR::make_LogicalCompare_t(al, loc, lhs, cmpop_type, rhs, result_type, nullptr);
        } else if( ASRUtils::is_complex(*left_type) && ASRUtils::is_complex(*right_type) ) {
            tmp = ASR::make_ComplexCompare_t(al, loc, lhs, cmpop_type, rhs, result_type, nullptr);
        } else {
            throw std::runtime_error("Only integer, real and complex types are supported so "
                "far for comparison operator, found: " + ASRUtils::type_to_str(left_type)
                + " and " + ASRUtils::type_to_str(right_type));
        }
    }

    void CreateLogicalOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::logicalbinopType binop_type, const Location& loc) {
        cast_helper(lhs, rhs, false, true);
        ASRUtils::make_ArrayBroadcast_t_util(al, loc, lhs, rhs);
        if( ASRUtils::is_logical(*ASRUtils::expr_type(lhs)) &&
            ASRUtils::is_logical(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_LogicalBinOp_t(al, loc, lhs,
                binop_type, rhs, ASRUtils::expr_type(lhs), nullptr);
        } else {
            throw std::runtime_error("Only integer types are supported so "
                "far for logical binary operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(lhs))
                + " and " + ASRUtils::type_to_str(ASRUtils::expr_type(rhs)));
        }
    }

    template <typename T>
    void evaluate_compile_time_value_for_BinOp(T& left_value, T& right_value,
        T& result_value, ASR::binopType binop_type) {
        switch (binop_type) {
            case ASR::binopType::Add: {
                result_value = left_value + right_value;
                break;
            }
            case ASR::binopType::Mul: {
                result_value = left_value * right_value;
                break;
            }
            case ASR::binopType::Div: {
                result_value = left_value / right_value;
                break;
            }
            case ASR::binopType::Sub: {
                result_value = left_value - right_value;
                break;
            }
            case ASR::binopType::Pow: {
                result_value = pow(left_value, right_value);
                break;
            }
            default: {
                throw std::runtime_error("Evaluation for ASR::binopType::" +
                    std::to_string(binop_type) + " is not supported yet.");
            }
        }
    }

    ASR::expr_t* evaluate_compile_time_value_for_BinOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::binopType binop_type, const Location& loc, ASR::ttypeType type) {
        if( ASRUtils::is_array(ASRUtils::expr_type(lhs)) ) {
            return nullptr;
        }

        ASR::ttype_t* result_type = ASRUtils::type_get_past_const(ASRUtils::expr_type(lhs));

        #define EVALUATE_COMPILE_TIME_VALUE_FOR_BINOP_CASE(Name, Constructor, Ctype) \
            case ASR::ttypeType::Name: { \
                Ctype lhs_value, rhs_value, result_value; \
                if( !ASRUtils::extract_value(ASRUtils::expr_value(lhs), lhs_value) || \
                    !ASRUtils::extract_value(ASRUtils::expr_value(rhs), rhs_value)) { \
                    return nullptr; \
                } \
                evaluate_compile_time_value_for_BinOp( \
                    lhs_value, rhs_value, result_value, binop_type); \
                return ASRUtils::EXPR(ASR::Constructor(al, loc, result_value, result_type)); \
            } \

        switch (type) {
            EVALUATE_COMPILE_TIME_VALUE_FOR_BINOP_CASE(Real, make_RealConstant_t, double);
            EVALUATE_COMPILE_TIME_VALUE_FOR_BINOP_CASE(Integer, make_IntegerConstant_t, int64_t);
            default: {
                throw std::runtime_error(std::string("Compile time evaluation of binary operator for type, ") +
                    ASRUtils::type_to_str(result_type) + " isn't handled yet.");
            }
        }

        return nullptr;
    }

    void CreateBinOp(ASR::expr_t* lhs, ASR::expr_t* rhs,
        ASR::binopType binop_type, const Location& loc) {
        cast_helper(lhs, rhs, false);
        ASRUtils::make_ArrayBroadcast_t_util(al, loc, lhs, rhs);
        ASR::ttype_t* result_type = ASRUtils::type_get_past_const(
            ASRUtils::type_get_past_allocatable(
                ASRUtils::type_get_past_pointer(ASRUtils::expr_type(lhs))));
        if( ASRUtils::is_integer(*ASRUtils::expr_type(lhs)) &&
            ASRUtils::is_integer(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_IntegerBinOp_t(al, loc, lhs,
                binop_type, rhs, result_type,
                evaluate_compile_time_value_for_BinOp(
                    lhs, rhs, binop_type, loc, ASR::ttypeType::Integer));
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_real(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_RealBinOp_t(al, loc, lhs,
                binop_type, rhs, result_type,
                evaluate_compile_time_value_for_BinOp(
                    lhs, rhs, binop_type, loc, ASR::ttypeType::Real));
        } else if( ASRUtils::is_complex(*ASRUtils::expr_type(lhs)) &&
                   ASRUtils::is_complex(*ASRUtils::expr_type(rhs)) ) {
            tmp = ASR::make_ComplexBinOp_t(al, loc, lhs,
                binop_type, rhs, result_type, nullptr);
        } else {
            throw std::runtime_error("Only integer, real and complex types are supported so "
                "far for binary operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(lhs))
                + " and " + ASRUtils::type_to_str(ASRUtils::expr_type(rhs)));
        }
    }

    void CreateLogicalNot(ASR::expr_t* op, const Location& loc) {
        if( ASRUtils::is_logical(*ASRUtils::expr_type(op)) ) {
            tmp = ASR::make_LogicalNot_t(al, loc, op,
                ASRUtils::expr_type(op), nullptr);
        } else {
            throw std::runtime_error("Only logical types are supported so "
                "far for logical not operator, found: " +
                ASRUtils::type_to_str(ASRUtils::expr_type(op)));
        }
    }

    ASR::expr_t* evaluate_compile_time_value_for_UnaryMinus(
        ASR::expr_t* operand, const Location& loc, ASR::ttypeType type) {
        if( ASRUtils::is_array(ASRUtils::expr_type(operand)) ) {
            return nullptr;
        }

        ASR::ttype_t* result_type = ASRUtils::expr_type(operand);

        #define EVALUATE_COMPILE_TIME_VALUE_FOR_UNARYOP_CASE(Name, Constructor, Ctype) \
            case ASR::ttypeType::Name: { \
                Ctype operand_value; \
                if( !ASRUtils::extract_value(ASRUtils::expr_value(operand), operand_value) ) { \
                    return nullptr; \
                } \
                return ASRUtils::EXPR(ASR::Constructor(al, loc, -operand_value, result_type)); \
            } \

        switch (type) {
            EVALUATE_COMPILE_TIME_VALUE_FOR_UNARYOP_CASE(Real, make_RealConstant_t, double);
            EVALUATE_COMPILE_TIME_VALUE_FOR_UNARYOP_CASE(Integer, make_IntegerConstant_t, int64_t);
            default: {
                throw std::runtime_error(std::string("Compile time evaluation of unary minus for type, ") +
                    ASRUtils::type_to_str(result_type) + " isn't handled yet.");
            }
        }

        return nullptr;
    }

    void CreateUnaryMinus(ASR::expr_t* op, const Location& loc) {
        if( ASRUtils::is_integer(*ASRUtils::expr_type(op)) ) {
            tmp = ASR::make_IntegerUnaryMinus_t(
                al, loc, op, ASRUtils::expr_type(op),
                evaluate_compile_time_value_for_UnaryMinus(
                op, loc, ASR::ttypeType::Integer));
        } else if( ASRUtils::is_real(*ASRUtils::expr_type(op)) ) {
            tmp = ASR::make_RealUnaryMinus_t(al, loc, op, ASRUtils::expr_type(op),
                evaluate_compile_time_value_for_UnaryMinus(
                op, loc, ASR::ttypeType::Real));
        } else {
            throw std::runtime_error("Only integer and real types are supported so "
                "far for unary operator, found: " + ASRUtils::type_to_str(ASRUtils::expr_type(op)));
        }
    }

    void mark_as_read_only(ASR::symbol_t* symbol) {
        read_only_symbols.insert(ASRUtils::symbol_get_past_external(symbol));
    }

    void mark_as_read_only(ASR::expr_t* list_to_array, bool root=true) {
        ASR::expr_t* expr = list_to_array;
        if( root ) {
            if( !ASR::is_a<ASR::Cast_t>(*list_to_array) ) {
                return ;
            }
            ASR::Cast_t* cast_t = ASR::down_cast<ASR::Cast_t>(list_to_array);
            if( cast_t->m_kind != ASR::cast_kindType::ListToArray ) {
                return ;
            }

            expr = cast_t->m_arg;
        }

        switch( expr->type ) {
            case ASR::exprType::Var: {
                ASR::Var_t* var_t = ASR::down_cast<ASR::Var_t>(expr);
                return mark_as_read_only(var_t->m_v);
            }
            case ASR::exprType::ListItem: {
                ASR::ListItem_t* list_item_t = ASR::down_cast<ASR::ListItem_t>(expr);
                return mark_as_read_only(list_item_t->m_a, false);
            }
            case ASR::exprType::ListSection: {
                ASR::ListSection_t* list_section_t = ASR::down_cast<ASR::ListSection_t>(expr);
                return mark_as_read_only(list_section_t->m_a, false);
            }
            case ASR::exprType::StructInstanceMember: {
                ASR::StructInstanceMember_t* struct_instance_member_t = ASR::down_cast<ASR::StructInstanceMember_t>(expr);
                return mark_as_read_only(struct_instance_member_t->m_m);
            }
            case ASR::exprType::StructStaticMember: {
                ASR::StructStaticMember_t* struct_static_member_t = ASR::down_cast<ASR::StructStaticMember_t>(expr);
                return mark_as_read_only(struct_static_member_t->m_m);
            }
            case ASR::exprType::EnumStaticMember: {
                ASR::EnumStaticMember_t* enum_static_member_t = ASR::down_cast<ASR::EnumStaticMember_t>(expr);
                return mark_as_read_only(enum_static_member_t->m_m);
            }
            case ASR::exprType::UnionInstanceMember: {
                ASR::UnionInstanceMember_t* union_instance_member_t = ASR::down_cast<ASR::UnionInstanceMember_t>(expr);
                return mark_as_read_only(union_instance_member_t->m_m);
            }
            default: {
                throw std::runtime_error("ASRUtils::exprType::" + std::to_string(expr->type) +
                    " is not handled yet in is_read_only method.");
            }
        }
    }

    bool is_read_only(ASR::symbol_t* symbol, std::string& symbol_name) {
        if( read_only_symbols.find(ASRUtils::symbol_get_past_external(symbol)) !=
            read_only_symbols.end() ) {
            symbol_name = ASRUtils::symbol_name(ASRUtils::symbol_get_past_external(symbol));
            return true;
        }
        return false;
    }

    bool is_read_only(ASR::expr_t* expr, std::string& symbol_name) {
        switch( expr->type ) {
            case ASR::exprType::Var: {
                ASR::Var_t* var_t = ASR::down_cast<ASR::Var_t>(expr);
                return is_read_only(var_t->m_v, symbol_name);
            }
            case ASR::exprType::ListItem: {
                ASR::ListItem_t* list_item_t = ASR::down_cast<ASR::ListItem_t>(expr);
                return is_read_only(list_item_t->m_a, symbol_name);
            }
            case ASR::exprType::ArrayItem: {
                ASR::ArrayItem_t* array_item_t = ASR::down_cast<ASR::ArrayItem_t>(expr);
                return is_read_only(array_item_t->m_v, symbol_name);
            }
            case ASR::exprType::ListSection: {
                ASR::ListSection_t* list_section_t = ASR::down_cast<ASR::ListSection_t>(expr);
                return is_read_only(list_section_t->m_a, symbol_name);
            }
            case ASR::exprType::StructInstanceMember: {
                ASR::StructInstanceMember_t* struct_instance_member_t = ASR::down_cast<ASR::StructInstanceMember_t>(expr);
                return is_read_only(struct_instance_member_t->m_m, symbol_name);
            }
            case ASR::exprType::StructStaticMember: {
                ASR::StructStaticMember_t* struct_static_member_t = ASR::down_cast<ASR::StructStaticMember_t>(expr);
                return is_read_only(struct_static_member_t->m_m, symbol_name);
            }
            case ASR::exprType::EnumStaticMember: {
                ASR::EnumStaticMember_t* enum_static_member_t = ASR::down_cast<ASR::EnumStaticMember_t>(expr);
                return is_read_only(enum_static_member_t->m_m, symbol_name);
            }
            case ASR::exprType::UnionInstanceMember: {
                ASR::UnionInstanceMember_t* union_instance_member_t = ASR::down_cast<ASR::UnionInstanceMember_t>(expr);
                return is_read_only(union_instance_member_t->m_m, symbol_name);
            }
            default: {
                throw std::runtime_error("ASRUtils::exprType::" + std::to_string(expr->type) +
                    " is not handled yet in is_read_only method.");
            }
        }
        return false;
    }

    bool TraverseBinaryOperator(clang::BinaryOperator *x) {
        clang::BinaryOperatorKind op = x->getOpcode();
        TraverseStmt(x->getLHS());
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp.get());
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp.get());
        if( op == clang::BO_Assign ) {
            std::string symbol_name = "";
            if( is_read_only(x_lhs, symbol_name) ) {
                std::cerr << symbol_name + " is marked as read only." << std::endl;
                exit(EXIT_FAILURE);
            }
            cast_helper(x_lhs, x_rhs, true);
            ASRUtils::make_ArrayBroadcast_t_util(al, Lloc(x), x_lhs, x_rhs);
            tmp = ASR::make_Assignment_t(al, Lloc(x), x_lhs, x_rhs, nullptr);
            is_stmt_created = true;
        } else {
            bool is_binop = false, is_cmpop = false;
            bool is_logicalbinop = false;
            ASR::binopType binop_type;
            ASR::cmpopType cmpop_type;
            ASR::logicalbinopType logicalbinop_type;
            switch (op) {
                case clang::BO_Add: {
                    binop_type = ASR::binopType::Add;
                    is_binop = true;
                    break;
                }
                case clang::BO_Sub: {
                    binop_type = ASR::binopType::Sub;
                    is_binop = true;
                    break;
                }
                case clang::BO_Mul: {
                    binop_type = ASR::binopType::Mul;
                    is_binop = true;
                    break;
                }
                case clang::BO_Div: {
                    binop_type = ASR::binopType::Div;
                    is_binop = true;
                    break;
                }
                case clang::BO_EQ: {
                    cmpop_type = ASR::cmpopType::Eq;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_NE: {
                    cmpop_type = ASR::cmpopType::NotEq;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_LT: {
                    cmpop_type = ASR::cmpopType::Lt;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_LE: {
                    cmpop_type = ASR::cmpopType::LtE;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_GT: {
                    cmpop_type = ASR::cmpopType::Gt;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_GE: {
                    cmpop_type = ASR::cmpopType::GtE;
                    is_cmpop = true;
                    break;
                }
                case clang::BO_LAnd: {
                    logicalbinop_type = ASR::logicalbinopType::And;
                    is_logicalbinop = true;
                    break;
                }
                case clang::BO_LOr: {
                    logicalbinop_type = ASR::logicalbinopType::Or;
                    is_logicalbinop = true;
                    break;
                }
                default: {
                    throw std::runtime_error("BinaryOperator not supported " + std::to_string(op));
                    break;
                }
            }
            if( is_binop ) {
                CreateBinOp(x_lhs, x_rhs, binop_type, Lloc(x));
            } else if( is_cmpop ) {
                CreateCompareOp(x_lhs, x_rhs, cmpop_type, Lloc(x));
            } else if( is_logicalbinop ) {
                CreateLogicalOp(x_lhs, x_rhs, logicalbinop_type, Lloc(x));
            } else {
                throw std::runtime_error("Only binary operators supported so far");
            }
            is_stmt_created = false;
        }
        return true;
    }

    ASR::symbol_t* check_aliases(std::string name) {
        for( auto itr = scopes.rbegin(); itr != scopes.rend(); itr++ ) {
            std::map<std::string, std::string>& alias = *itr;
            if( alias.find(name) != alias.end() ) {
                return current_scope->resolve_symbol(alias[name]);
            }
        }

        return nullptr;
    }

    ASR::symbol_t* resolve_symbol(std::string name) {
        ASR::symbol_t* sym = check_aliases(name);
        if( sym ) {
            return sym;
        }

        return current_scope->resolve_symbol(name);
    }

    bool TraverseDeclRefExpr(clang::DeclRefExpr* x) {
        std::string name = x->getNameInfo().getAsString();
        std::string namespace_name = "";
        if( x->getQualifier() ) {
            namespace_name = x->getQualifier()->getAsNamespace()->getNameAsString();
        }
        ASR::symbol_t* sym = resolve_symbol(name);
        if( name == "operator<<" || name == "cout" || name == "endl" ||
            name == "operator()" || name == "operator+" || name == "operator=" ||
            name == "operator*" || name == "view" || name == "submdspan" || name == "empty" ||
            name == "all" || name == "full_extent" || name == "any" || name == "not_equal" ||
            name == "exit" || name == "printf" || name == "exp" ||
            name == "sum" || name == "amax" || name == "abs" ||
            name == "operator-" || name == "operator/" || name == "operator>" ||
            name == "range" || name == "pow" || name == "equal" ||
            name == "operator<" || name == "operator<=" || name == "operator>=" ||
            name == "operator!=" || name == "operator\"\"i" || name == "sin" ||
            name == "cos" || name == "amin" || name == "operator[]" || name == "sqrt" ||
            name == "ones" || name == "from_blob" ) {
            if( sym != nullptr && ASR::is_a<ASR::Function_t>(
                    *ASRUtils::symbol_get_past_external(sym)) ) {
                throw std::runtime_error("Special function " + name + " cannot be overshadowed yet.");
            }
            if( sym == nullptr ) {
                if( name == "full_extent" ) {
                    is_all_called = true;
                } else if(namespace_name == "torch") {
                    cxx_operator_name_obj.set(namespace_name + "::" + name);
                } else {
                    cxx_operator_name_obj.set(name);
                }
                tmp = nullptr;
                return true;
            }
        }
        if( sym == nullptr ) {
            if( x->getType()->isEnumeralType() ) {
                SymbolTable* scope = current_scope;
                while( scope ) {
                    for( auto itr = scope2enums[scope].begin(); itr != scope2enums[scope].end(); itr++ ) {
                        std::string mangled_name = current_scope->get_unique_name(
                            name + "@" + ASRUtils::symbol_name(*itr));
                        ASR::EnumType_t* enumtype_t = ASR::down_cast<ASR::EnumType_t>(*itr);
                        ASR::symbol_t* enum_member_orig = enumtype_t->m_symtab->resolve_symbol(name);
                        if( enum_member_orig == nullptr ) {
                            continue ;
                        }
                        ASR::symbol_t* enum_member = ASR::down_cast<ASR::symbol_t>(ASR::make_ExternalSymbol_t(
                            al, Lloc(x), current_scope, s2c(al, mangled_name), enum_member_orig,
                            ASRUtils::symbol_name(*itr), nullptr, 0, s2c(al, name), ASR::accessType::Public));
                        current_scope->add_symbol(mangled_name, enum_member);
                        tmp = ASR::make_EnumValue_t(al, Lloc(x), ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), enum_member)),
                            ASRUtils::TYPE(ASR::make_Enum_t(al, Lloc(x), *itr)), enumtype_t->m_type,
                            ASRUtils::expr_value(ASR::down_cast<ASR::Variable_t>(enum_member_orig)->m_symbolic_value));
                        return true;
                    }
                    scope = scope->parent;
                }
            }
            throw std::runtime_error("Symbol " + name + " not found in current scope.");
        }
        tmp = ASR::make_Var_t(al, Lloc(x), sym);
        is_stmt_created = false;
        return true;
    }

    bool TraverseIntegerLiteral(clang::IntegerLiteral* x) {
        int64_t i = x->getValue().getLimitedValue();
        tmp = ASR::make_IntegerConstant_t(al, Lloc(x), i,
            ASRUtils::TYPE(ASR::make_Integer_t(al, Lloc(x), 4)));
        is_stmt_created = false;
        return true;
    }

    void cast_helper(ASR::expr_t*& left, ASR::expr_t*& right, bool is_assign, bool logical_bin_op=false) {
        bool no_cast = ((ASR::is_a<ASR::Pointer_t>(*ASRUtils::expr_type(left)) &&
                        ASR::is_a<ASR::Var_t>(*left)) ||
                        (ASR::is_a<ASR::Pointer_t>(*ASRUtils::expr_type(right)) &&
                        ASR::is_a<ASR::Var_t>(*right)));
        ASR::ttype_t *right_type = ASRUtils::expr_type(right);
        ASR::ttype_t *left_type = ASRUtils::expr_type(left);
        left_type = ASRUtils::extract_type(left_type);
        right_type = ASRUtils::extract_type(right_type);
        if( no_cast ) {
            int lkind = ASRUtils::extract_kind_from_ttype_t(left_type);
            int rkind = ASRUtils::extract_kind_from_ttype_t(right_type);
            if( left_type->type != right_type->type || lkind != rkind ) {
                throw std::runtime_error("Casting for mismatching pointer types not supported yet.");
            }
        }

        // Handle presence of logical types in binary operations
        // by converting them into 32-bit integers.
        // See integration_tests/test_bool_binop.py for its significance.
        if(!is_assign && !logical_bin_op && ASRUtils::is_logical(*left_type) && ASRUtils::is_logical(*right_type) ) {
            ASR::ttype_t* dest_type = ASRUtils::TYPE(ASR::make_Integer_t(al, left_type->base.loc, 4));
            ASR::ttype_t* dest_type_left = dest_type;
            if( ASRUtils::is_array(left_type) ) {
                ASR::dimension_t* m_dims = nullptr;
                size_t n_dims = ASRUtils::extract_dimensions_from_ttype(left_type, m_dims);
                dest_type_left = ASRUtils::make_Array_t_util(al, dest_type->base.loc,
                    dest_type, m_dims, n_dims);
            }
            ASR::ttype_t* dest_type_right = dest_type;
            if( ASRUtils::is_array(right_type) ) {
                ASR::dimension_t* m_dims = nullptr;
                size_t n_dims = ASRUtils::extract_dimensions_from_ttype(right_type, m_dims);
                dest_type_right = ASRUtils::make_Array_t_util(al, dest_type->base.loc,
                    dest_type, m_dims, n_dims);
            }
            left = CastingUtil::perform_casting(left, dest_type_left, al, left->base.loc);
            right = CastingUtil::perform_casting(right, dest_type_right, al, right->base.loc);
            return ;
        }

        ASR::ttype_t *src_type = nullptr, *dest_type = nullptr;
        ASR::expr_t *src_expr = nullptr, *dest_expr = nullptr;
        int casted_expression_signal = CastingUtil::get_src_dest(
            left, right, src_expr, dest_expr, src_type, dest_type,
            is_assign, true);
        if( casted_expression_signal == 2 ) {
            return ;
        }
        src_expr = CastingUtil::perform_casting(
            src_expr, dest_type, al, src_expr->base.loc);
        if( casted_expression_signal == 0 ) {
            left = src_expr;
            right = dest_expr;
        } else if( casted_expression_signal == 1 ) {
            left = dest_expr;
            right = src_expr;
        }
    }

    bool TraverseUserDefinedLiteral(clang::UserDefinedLiteral* x) {
        clang::Expr* clang_cooked_literal = x->getCookedLiteral();
        TraverseStmt(clang_cooked_literal);
        ASR::expr_t* cooked_literal = ASRUtils::EXPR(tmp.get());

        clang::Expr* clang_literal = x->getCallee();
        TraverseStmt(clang_literal);
        if( tmp != nullptr ) {
            throw std::runtime_error("User defined literals only work for operator\"\"i.");
        }
        if( !check_and_handle_special_function(x, nullptr) ) {
            throw std::runtime_error("User defined literals only work for operator\"\"i.");
        }
        ASR::expr_t* literal = ASRUtils::EXPR(tmp.get());
        cast_helper(literal, cooked_literal, false);
        tmp = ASR::make_ComplexBinOp_t(al, Lloc(x), literal, ASR::binopType::Mul,
            cooked_literal, ASRUtils::expr_type(literal), nullptr);
        return true;
    }

    bool TraverseCXXBoolLiteralExpr(clang::CXXBoolLiteralExpr* x) {
        bool b = x->getValue();
        tmp = ASR::make_LogicalConstant_t(al, Lloc(x), b,
            ASRUtils::TYPE(ASR::make_Logical_t(al, Lloc(x), 4)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseFloatingLiteral(clang::FloatingLiteral* x) {
        double d = x->getValue().convertToDouble();
        tmp = ASR::make_RealConstant_t(al, Lloc(x), d,
            ASRUtils::TYPE(ASR::make_Real_t(al, Lloc(x), 8)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseStringLiteral(clang::StringLiteral *x) {
        std::string s = x->getString().str();
        tmp = ASR::make_StringConstant_t(al, Lloc(x), s2c(al, s),
            ASRUtils::TYPE(ASR::make_Character_t(al, Lloc(x), 1, s.size(), nullptr)));
        is_stmt_created = false;
        return true;
    }

    bool TraverseSwitchStmt(clang::SwitchStmt* x) {
        Vec<ASR::case_stmt_t*>* current_switch_case_copy = current_switch_case;
        Vec<ASR::stmt_t*>* default_stmt_copy = default_stmt;
        Vec<ASR::case_stmt_t*> current_switch_case_; current_switch_case_.reserve(al, 1);
        Vec<ASR::stmt_t*> default_stmt_; default_stmt_.reserve(al, 1);
        current_switch_case = &current_switch_case_;
        default_stmt = &default_stmt_;
        bool enable_fall_through_copy = enable_fall_through;
        enable_fall_through = false;

        clang::Expr* clang_cond = x->getCond();
        TraverseStmt(clang_cond);
        ASR::expr_t* cond = ASRUtils::EXPR(tmp.get());

        TraverseStmt(x->getBody());

        tmp = ASR::make_Select_t(al, Lloc(x), cond,
            current_switch_case->p, current_switch_case->size(),
            default_stmt->p, default_stmt->size(), enable_fall_through);
        enable_fall_through = false;
        current_switch_case = current_switch_case_copy;
        default_stmt = default_stmt_copy;
        is_stmt_created = true;
        enable_fall_through = enable_fall_through_copy;
        return true;
    }

    bool TraverseBreakStmt(clang::BreakStmt* x) {
        clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseBreakStmt(x);
        if( inside_loop ) {
            tmp = ASR::make_Exit_t(al, Lloc(x), nullptr);
            is_stmt_created = true;
        }
        is_break_stmt_present.set(true);
        return true;
    }

    bool TraverseContinueStmt(clang::ContinueStmt* x) {
        if( for_loop != nullptr ) {
            clang::Stmt* inc_stmt = for_loop->getInc();
            TraverseStmt(inc_stmt);
            LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
            current_body->push_back(al, ASRUtils::STMT(tmp.get()));
        }
        tmp = ASR::make_Cycle_t(al, Lloc(x), nullptr);
        is_stmt_created = true;
        return true;
    }

    bool TraverseCaseStmt(clang::CaseStmt* x) {
        if ( x->caseStmtIsGNURange() ) {
            throw std::runtime_error("Ranges not supported in case.");
        }

        clang::Expr* lhs = x->getLHS();
        TraverseStmt(lhs);
        ASR::expr_t* cond = ASRUtils::EXPR(tmp.get());

        Vec<ASR::expr_t*> a_test; a_test.reserve(al, 1);
        a_test.push_back(al, cond);

        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &body;
        is_break_stmt_present.set(false);
        TraverseStmt(x->getSubStmt());
        current_body = current_body_copy;
        bool case_fall_through = true;
        if( !is_break_stmt_present.get() ) {
            enable_fall_through = true;
        } else {
            case_fall_through = false;
        }
        ASR::case_stmt_t* case_stmt = ASR::down_cast<ASR::case_stmt_t>(
            ASR::make_CaseStmt_t(al, Lloc(x), a_test.p, a_test.size(),
            body.p, body.size(), case_fall_through));
        current_switch_case->push_back(al, case_stmt);
        is_stmt_created = false;
        return true;
    }

    bool TraverseDefaultStmt(clang::DefaultStmt* x) {
        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = default_stmt;
        TraverseStmt(x->getSubStmt());
        current_body = current_body_copy;
        is_stmt_created = false;
        return true;
    }

    bool TraverseCompoundStmt(clang::CompoundStmt *x) {
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);
        for (auto &s : x->body()) {
            bool is_stmt_created_ = is_stmt_created;
            is_stmt_created = false;
            TraverseStmt(s);
            if( is_stmt_created ) {
                current_body->push_back(al, ASRUtils::STMT(tmp.get()));
                is_stmt_created = false;
            }
            is_stmt_created = is_stmt_created_;
        }
        scopes.pop_back();
        return true;
    }

    bool TraverseReturnStmt(clang::ReturnStmt *x) {
        ASR::symbol_t* return_sym = current_scope->resolve_symbol("__return_var");
        ASR::expr_t* return_var = ASRUtils::EXPR(ASR::make_Var_t(al, Lloc(x), return_sym));
        TraverseStmt(x->getRetValue());
        tmp = ASR::make_Assignment_t(al, Lloc(x), return_var, ASRUtils::EXPR(tmp.get()), nullptr);
        current_body->push_back(al, ASRUtils::STMT(tmp.get()));
        tmp = ASR::make_Return_t(al, Lloc(x));
        is_stmt_created = true;
        return true;
    }

    ASR::expr_t* flatten_ArrayItem(ASR::expr_t* expr) {
        if( !ASR::is_a<ASR::ArrayItem_t>(*expr) ) {
            return expr;
        }

        ASR::ArrayItem_t* array_item_t = ASR::down_cast<ASR::ArrayItem_t>(expr);
        if( !ASR::is_a<ASR::ArrayItem_t>(*array_item_t->m_v) ) {
            return expr;
        }

        ASR::expr_t* flattened_array_item_expr = flatten_ArrayItem(array_item_t->m_v);
        ASR::ArrayItem_t* flattened_array_item = ASR::down_cast<ASR::ArrayItem_t>(flattened_array_item_expr);
        array_item_t->m_v = flattened_array_item->m_v;
        Vec<ASR::array_index_t> indices; indices.from_pointer_n_copy(
            al, flattened_array_item->m_args, flattened_array_item->n_args);
        indices.push_back(al, array_item_t->m_args[0]);
        array_item_t->m_args = indices.p;
        array_item_t->n_args = indices.size();
        return expr;
    }

    bool TraverseArraySubscriptExpr(clang::ArraySubscriptExpr* x) {
        clang::Expr* clang_array = x->getBase();
        TraverseStmt(clang_array);
        ASR::expr_t* array = flatten_ArrayItem(ASRUtils::EXPR(tmp.get()));
        clang::Expr* clang_index = x->getIdx();
        TraverseStmt(clang_index);
        ASR::expr_t* index = ASRUtils::EXPR(tmp.get());
        Vec<ASR::array_index_t> indices; indices.reserve(al, 1);
        ASR::array_index_t array_index; array_index.loc = index->base.loc;
        array_index.m_left = nullptr; array_index.m_right = index; array_index.m_step = nullptr;
        indices.push_back(al, array_index);
        ASR::expr_t* array_item = ASRUtils::EXPR(ASR::make_ArrayItem_t(al, Lloc(x), array, indices.p, indices.size(),
            ASRUtils::extract_type(ASRUtils::expr_type(array)), ASR::arraystorageType::RowMajor, nullptr));
        array_item = flatten_ArrayItem(array_item);
        tmp = (ASR::asr_t*) array_item;
        return true;
    }

    bool TraverseCompoundAssignOperator(clang::CompoundAssignOperator* x) {
        TraverseStmt(x->getLHS());
        ASR::expr_t* x_lhs = ASRUtils::EXPR(tmp.get());
        TraverseStmt(x->getRHS());
        ASR::expr_t* x_rhs = ASRUtils::EXPR(tmp.get());
        if( x->getOpcode() == clang::BinaryOperatorKind::BO_AddAssign ) {
            CreateBinOp(x_lhs, x_rhs, ASR::binopType::Add, Lloc(x));
        } else if( x->getOpcode() == clang::BinaryOperatorKind::BO_SubAssign ) {
            CreateBinOp(x_lhs, x_rhs, ASR::binopType::Sub, Lloc(x));
        } else if( x->getOpcode() == clang::BinaryOperatorKind::BO_MulAssign ) {
            CreateBinOp(x_lhs, x_rhs, ASR::binopType::Mul, Lloc(x));
        } else {
            throw std::runtime_error(x->getOpcodeStr().str() + " is not yet supported in CompoundAssignOperator.");
        }
        ASR::expr_t* sum_expr = ASRUtils::EXPR(tmp.get());
        tmp = ASR::make_Assignment_t(al, Lloc(x), x_lhs, sum_expr, nullptr);
        is_stmt_created = true;
        return true;
    }

    bool TraverseUnaryOperator(clang::UnaryOperator* x) {
        clang::UnaryOperatorKind op = x->getOpcode();
        clang::Expr* expr = x->getSubExpr();
        TraverseStmt(expr);
        ASR::expr_t* var = ASRUtils::EXPR(tmp.get());
        switch( op ) {
            case clang::UnaryOperatorKind::UO_PostInc: {
                ASR::expr_t* incbyone = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(
                    al, Lloc(x), var, ASR::binopType::Add,
                    ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                        al, Lloc(x), 1, ASRUtils::expr_type(var))),
                    ASRUtils::expr_type(var), nullptr));
                tmp = ASR::make_Assignment_t(al, Lloc(x), var, incbyone, nullptr);
                is_stmt_created = true;
                break;
            }
            case clang::UnaryOperatorKind::UO_PostDec: {
                ASR::expr_t* decbyone = ASRUtils::EXPR(ASR::make_IntegerBinOp_t(
                    al, Lloc(x), var, ASR::binopType::Sub,
                    ASRUtils::EXPR(ASR::make_IntegerConstant_t(
                        al, Lloc(x), 1, ASRUtils::expr_type(var))),
                    ASRUtils::expr_type(var), nullptr));
                tmp = ASR::make_Assignment_t(al, Lloc(x), var, decbyone, nullptr);
                is_stmt_created = true;
                break;
            }
            case clang::UnaryOperatorKind::UO_Minus: {
                CreateUnaryMinus(var, Lloc(x));
                break;
            }
            case clang::UnaryOperatorKind::UO_LNot: {
                CreateLogicalNot(var, Lloc(x));
                break;
            }
            default: {
                throw std::runtime_error("Only postfix increment and minus are supported so far.");
            }
        }
        return true;
    }

    bool TraverseWhileStmt(clang::WhileStmt* x) {
        bool inside_loop_copy = inside_loop;
        inside_loop = true;
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);

        clang::Expr* loop_cond = x->getCond();
        TraverseStmt(loop_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp.get());

        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &body;
        clang::Stmt* loop_body = x->getBody();
        TraverseStmt(loop_body);
        current_body = current_body_copy;

        tmp = ASR::make_WhileLoop_t(al, Lloc(x), nullptr, test, body.p, body.size(), nullptr, 0);
        is_stmt_created = true;
        scopes.pop_back();
        inside_loop = inside_loop_copy;
        return true;
    }

    bool TraverseForStmt(clang::ForStmt* x) {
        bool inside_loop_copy = inside_loop;
        inside_loop = true;
        clang::ForStmt* for_loop_copy = for_loop;
        for_loop = x;
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);
        clang::Stmt* init_stmt = x->getInit();
        TraverseStmt(init_stmt);
        LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
        current_body->push_back(al, ASRUtils::STMT(tmp.get()));

        clang::Expr* loop_cond = x->getCond();
        TraverseStmt(loop_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp.get());

        Vec<ASR::stmt_t*> body; body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &body;
        clang::Stmt* loop_body = x->getBody();
        TraverseStmt(loop_body);
        clang::Stmt* inc_stmt = x->getInc();
        TraverseStmt(inc_stmt);
        LCOMPILERS_ASSERT(tmp != nullptr && is_stmt_created);
        body.push_back(al, ASRUtils::STMT(tmp.get()));
        current_body = current_body_copy;

        tmp = ASR::make_WhileLoop_t(al, Lloc(x), nullptr, test, body.p, body.size(), nullptr, 0);
        is_stmt_created = true;
        scopes.pop_back();
        for_loop = for_loop_copy;
        inside_loop = inside_loop_copy;
        return true;
    }

    bool TraverseIfStmt(clang::IfStmt* x) {
        std::map<std::string, std::string> alias;
        scopes.push_back(alias);

        clang::Expr* if_cond = x->getCond();
        TraverseStmt(if_cond);
        ASR::expr_t* test = ASRUtils::EXPR(tmp.get());
        if( !ASRUtils::is_logical(*ASRUtils::expr_type(test)) ) {
            test = CastingUtil::perform_casting(test,
                ASRUtils::TYPE(ASR::make_Logical_t(al, Lloc(x), 1)), al, Lloc(x));
        }

        Vec<ASR::stmt_t*> then_body; then_body.reserve(al, 1);
        Vec<ASR::stmt_t*>*current_body_copy = current_body;
        current_body = &then_body;
        clang::Stmt* clang_then_body = x->getThen();
        TraverseStmt(clang_then_body);
        current_body = current_body_copy;

        Vec<ASR::stmt_t*> else_body; else_body.reserve(al, 1);
        if( x->hasElseStorage() ) {
            Vec<ASR::stmt_t*>*current_body_copy = current_body;
            current_body = &else_body;
            clang::Stmt* clang_else_body = x->getElse();
            TraverseStmt(clang_else_body);
            current_body = current_body_copy;
        }

        tmp = ASR::make_If_t(al, Lloc(x), test, then_body.p,
            then_body.size(), else_body.p, else_body.size());
        is_stmt_created = true;
        scopes.pop_back();
        return true;
    }

    template <typename T>
    bool process_AST_node(T* x) {
        std::string file_path = get_file_name(x);
        if( file_path.size() == 0 ) {
            return false;
        }

        std::vector<std::string> include_paths = {
            "include/c++/v1",
            "include/stddef.h",
            "include/__stddef_max_align_t.h",
            "usr/include",
            "mambaforge/envs",
            "lib/gcc",
            "lib/clang",
            "micromamba-root/envs",
            "micromamba/envs",
            "conda_root/envs"
        };
        for( std::string& path: include_paths ) {
            if( file_path.find(path) != std::string::npos ) {
                return false;
            }
        }

        return true;
    }

    bool TraverseDecl(clang::Decl* x) {
        if( x->isImplicit() ) {
            return true;
        }

        if( process_AST_node(x) || x->getKind() == clang::Decl::Kind::TranslationUnit ) {
            return clang::RecursiveASTVisitor<ClangASTtoASRVisitor>::TraverseDecl(x);
        } else {
            return true;
        }

        return false;
    }

private:
    clang::ASTContext *Context;
};

class FindNamedClassConsumer: public clang::ASTConsumer {

public:

    Allocator& al;

    explicit FindNamedClassConsumer(clang::ASTContext *Context,
        Allocator& al_, ASR::asr_t*& tu_): al(al_), Visitor(Context, al_, tu_) {}

    virtual void HandleTranslationUnit(clang::ASTContext &Context) {
        Visitor.TraverseDecl(Context.getTranslationUnitDecl());
        PassUtils::UpdateDependenciesVisitor dependency_visitor(al);
        dependency_visitor.visit_TranslationUnit(*((ASR::TranslationUnit_t*) Visitor.tu));
    }

private:
    LCompilers::ClangASTtoASRVisitor Visitor;
};

class FindNamedClassAction: public clang::ASTFrontendAction {

    public:

        Allocator& al;
        ASR::asr_t*& tu;

        FindNamedClassAction(Allocator& al_, ASR::asr_t*& tu_): al{al_}, tu{tu_} {

        }

        virtual std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
            clang::CompilerInstance &Compiler, llvm::StringRef /*InFile*/) {
            return std::make_unique<FindNamedClassConsumer>(
                &Compiler.getASTContext(), al, tu);
        }
};

class ClangCheckActionFactory {

public:

    std::string infile, ast_dump_file, ast_dump_filter;
    bool ast_list, ast_print, show_clang_ast;

    ClangCheckActionFactory(std::string infile_, std::string ast_dump_file,
        std::string ast_dump_filter, bool ast_list,
        bool ast_print, bool show_clang_ast): infile(infile_),
        ast_dump_file(ast_dump_file), ast_dump_filter(ast_dump_filter),
        ast_list(ast_list), ast_print(ast_print), show_clang_ast(show_clang_ast) {}

    std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
        if (ast_list) {
            return clang::CreateASTDeclNodeLister();
        } else if ( show_clang_ast ) {
            llvm::raw_fd_ostream* llvm_fd_ostream = nullptr;
            if ( ast_dump_file.size() > 0 ) {
                std::error_code errorCode;
                llvm_fd_ostream = new llvm::raw_fd_ostream(ast_dump_file, errorCode);
            }
            std::unique_ptr<llvm::raw_ostream> llvm_ostream(llvm_fd_ostream);
            return clang::CreateASTDumper(std::move(llvm_ostream), ast_dump_filter,
                                          /*DumpDecls=*/true,
                                          /*Deserialize=*/false,
                                          /*DumpLookups=*/false,
                                          /*DumpDeclTypes=*/false,
                                          clang::ADOF_Default);
        } else if (ast_print) {
            return clang::CreateASTPrinter(nullptr, ast_dump_filter);
        }
        return std::make_unique<clang::ASTConsumer>();
    }
};

template <typename T>
class LCompilersFrontendActionFactory : public clang::tooling::FrontendActionFactory {

public:

    Allocator& al;
    ASR::asr_t*& tu;

    LCompilersFrontendActionFactory(Allocator& al_, ASR::asr_t*& tu_):
        al(al_), tu(tu_) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<T>(al, tu);
    }
};

template <typename T>
clang::tooling::FrontendActionFactory* newFrontendActionLCompilersFactory(Allocator& al_, ASR::asr_t*& tu_) {
    return new LCompilersFrontendActionFactory<T>(al_, tu_);
}

namespace LC {

static llvm::Expected<clang::tooling::CommonOptionsParser> get_parser(std::string infile, bool parse_all_comments) {
    static llvm::cl::OptionCategory ClangCheckCategory("clang-check options");
    static const llvm::opt::OptTable &Options = clang::driver::getDriverOptTable();
    if( parse_all_comments ) {
        int clang_argc = 4;
        const char *clang_argv[] = {"lc", infile.c_str(), "--extra-arg=-fparse-all-comments", "--"};
        return clang::tooling::CommonOptionsParser::create(clang_argc, clang_argv, ClangCheckCategory);
    } else {
        int clang_argc = 3;
        const char *clang_argv[] = {"lc", infile.c_str(), "--"};
        return clang::tooling::CommonOptionsParser::create(clang_argc, clang_argv, ClangCheckCategory);
    }
}

int dump_clang_ast(std::string infile, std::string ast_dump_file,
    std::string ast_dump_filter, bool ast_list, bool ast_print,
    bool show_clang_ast, bool parse_all_comments) {
    auto ExpectedParser = get_parser(infile, parse_all_comments);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
    std::vector<std::string> sourcePaths = OptionsParser.getSourcePathList();
    clang::tooling::ClangTool Tool(OptionsParser.getCompilations(), sourcePaths);

    ClangCheckActionFactory CheckFactory(infile, ast_dump_file,
        ast_dump_filter, ast_list, ast_print, show_clang_ast);
    return Tool.run(clang::tooling::newFrontendActionFactory(&CheckFactory).get());
}

int clang_ast_to_asr(Allocator &al, std::string infile, ASR::asr_t*& tu, bool parse_all_comments) {
    auto ExpectedParser = get_parser(infile, parse_all_comments);
    if (!ExpectedParser) {
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
    std::vector<std::string> sourcePaths = OptionsParser.getSourcePathList();
    clang::tooling::ClangTool Tool(OptionsParser.getCompilations(), sourcePaths);

    clang::tooling::FrontendActionFactory* FrontendFactory;
    FrontendFactory = newFrontendActionLCompilersFactory<FindNamedClassAction>(al, tu);
    return Tool.run(FrontendFactory);
}

} // namespace LC

} // namespace LCompilers
