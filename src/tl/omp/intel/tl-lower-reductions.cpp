/*--------------------------------------------------------------------
  (C) Copyright 2006-2013 Barcelona Supercomputing Center
                          Centro Nacional de Supercomputacion
  
  This file is part of Mercurium C/C++ source-to-source compiler.
  
  See AUTHORS file in the top level directory for information
  regarding developers and contributors.
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 3 of the License, or (at your option) any later version.
  
  Mercurium C/C++ source-to-source compiler is distributed in the hope
  that it will be useful, but WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
  PURPOSE.  See the GNU Lesser General Public License for more
  details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with Mercurium C/C++ source-to-source compiler; if
  not, write to the Free Software Foundation, Inc., 675 Mass Ave,
  Cambridge, MA 02139, USA.
--------------------------------------------------------------------*/


#include "tl-lower-reductions.hpp"
#include "tl-lowering-utils.hpp"
#include "tl-symbol-utils.hpp"
#include "tl-nodecl-utils.hpp"
#include "tl-counters.hpp"
#include "cxx-diagnostic.h"
#include "cxx-cexpr.h"

namespace TL
{
    Intel::ReplaceInOutMaster::ReplaceInOutMaster(
            TL::Symbol field,
            TL::Symbol orig_omp_in,  TL::Symbol reduction_pack_symbol,
            TL::Symbol orig_omp_out, TL::Symbol reduced_symbol)
        : _field(field),
        _orig_omp_in(orig_omp_in), _reduction_pack_symbol(reduction_pack_symbol),
        _orig_omp_out(orig_omp_out), _reduced_symbol(reduced_symbol)
    { }

    void Intel::ReplaceInOutMaster::visit(const Nodecl::Symbol& node)
    {
        TL::Symbol sym = node.get_symbol();

        if (sym == _orig_omp_in)
        {
            Nodecl::NodeclBase class_member = 
                Nodecl::ClassMemberAccess::make(
                        _reduction_pack_symbol.make_nodecl(/* set_ref_type */ true, node.get_locus()),
                        Nodecl::Symbol::make(_field, node.get_locus()),
                        Nodecl::NodeclBase::null(),
                        node.get_type(),
                        node.get_locus());
            node.replace(class_member);
        }
        else if (sym == _orig_omp_out)
        {
            Nodecl::NodeclBase reduced_sym_tree =
                _reduced_symbol.make_nodecl(/* set_ref_type */ true, node.get_locus());
            node.replace(reduced_sym_tree);
        }
    }

    struct ReplaceInOut : Nodecl::ExhaustiveVisitor<void>
    {
        TL::Symbol _field;
        TL::Symbol _orig_omp_in, _new_omp_in;
        TL::Symbol _orig_omp_out, _new_omp_out;

        ReplaceInOut(
                TL::Symbol field,
                TL::Symbol orig_omp_in,  TL::Symbol new_omp_in,
                TL::Symbol orig_omp_out, TL::Symbol new_omp_out)
            : _field(field),
            _orig_omp_in(orig_omp_in), _new_omp_in(new_omp_in),
            _orig_omp_out(orig_omp_out), _new_omp_out(new_omp_out)
        { }

        virtual void visit(const Nodecl::Symbol& node)
        {
            TL::Symbol sym = node.get_symbol();

            TL::Symbol *new_symbol = NULL;

            if (sym == _orig_omp_in)
                new_symbol = &_new_omp_in;
            else if (sym == _orig_omp_out)
                new_symbol = &_new_omp_out;
            else
                return;

            Nodecl::NodeclBase class_member = 
                Nodecl::ClassMemberAccess::make(
                        new_symbol->make_nodecl(/* set_ref_type */ true, node.get_locus()),
                        Nodecl::Symbol::make(_field, node.get_locus()),
                        Nodecl::NodeclBase::null(),
                        node.get_type(),
                        node.get_locus());

            node.replace(class_member);
        }
    };

    typedef Nodecl::ExhaustiveVisitor<void> ReplaceInOutSIMD;

    struct ReplaceInOutSIMDKNC : ReplaceInOutSIMD
    {
        TL::Symbol _orig_omp_in, _new_omp_in;
        TL::Symbol _orig_omp_out, _new_omp_out;

        ReplaceInOutSIMDKNC(
                TL::Symbol orig_omp_in,  TL::Symbol new_omp_in,
                TL::Symbol orig_omp_out, TL::Symbol new_omp_out)
            : _orig_omp_in(orig_omp_in), _new_omp_in(new_omp_in),
            _orig_omp_out(orig_omp_out), _new_omp_out(new_omp_out)
        { }

        virtual void visit(const Nodecl::Symbol& node)
        {
            TL::Symbol sym = node.get_symbol();

            TL::Symbol *new_symbol = NULL;

            if (sym == _orig_omp_in)
                new_symbol = &_new_omp_in;
            else if (sym == _orig_omp_out)
                new_symbol = &_new_omp_out;
            else
                return;

            Nodecl::NodeclBase new_symbol_ref =
                new_symbol->make_nodecl(/* set_ref_type */ true, node.get_locus());

            node.replace(new_symbol_ref);
        }
    };

    TL::Symbol Intel::emit_callback_for_reduction_scalar(
            TL::ObjectList<Nodecl::OpenMP::ReductionItem> &reduction_items,
            TL::Type reduction_pack_type,
            Nodecl::NodeclBase location,
            TL::Symbol current_function)
    {
        TL::ObjectList<std::string> parameter_names;
        TL::ObjectList<TL::Type> parameter_types;

        parameter_names.append("red_omp_out");
        parameter_types.append(reduction_pack_type.get_lvalue_reference_to());

        parameter_names.append("red_omp_in");
        parameter_types.append(reduction_pack_type.get_lvalue_reference_to());

        TL::Counter &counters = TL::CounterManager::get_counter("intel-omp-reduction");
        std::stringstream ss;
        ss << "_red_" << (int)counters;
        counters++;

        TL::Symbol new_callback = SymbolUtils::new_function_symbol(
                current_function,
                ss.str(),
                get_void_type(),
                parameter_names,
                parameter_types);

        Nodecl::NodeclBase function_code, empty_stmt;

        SymbolUtils::build_empty_body_for_function(
                new_callback,
                function_code,
                empty_stmt);

        TL::Symbol red_omp_out = empty_stmt.retrieve_context().get_symbol_from_name("red_omp_out");
        TL::Symbol red_omp_in = empty_stmt.retrieve_context().get_symbol_from_name("red_omp_in");

        TL::Source combiner;
        TL::ObjectList<TL::Symbol> reduction_fields = reduction_pack_type.get_fields();
        TL::ObjectList<TL::Symbol>::iterator it_fields = reduction_fields.begin();
        for (TL::ObjectList<Nodecl::OpenMP::ReductionItem>::iterator it = reduction_items.begin();
                it != reduction_items.end();
                it++, it_fields++)
        {
            Nodecl::OpenMP::ReductionItem &current(*it);
            TL::Symbol reductor = current.get_reductor().get_symbol();
            OpenMP::Reduction* reduction = OpenMP::Reduction::get_reduction_info_from_symbol(reductor);

            Nodecl::NodeclBase combiner_expr = reduction->get_combiner().shallow_copy();

            ReplaceInOut replace_inout(
                    *it_fields,
                    reduction->get_omp_in(), red_omp_in,
                    reduction->get_omp_out(), red_omp_out);
            replace_inout.walk(combiner_expr);

            combiner << as_expression(combiner_expr) << ";"
                ;
        }

        Nodecl::NodeclBase new_body_tree = combiner.parse_statement(empty_stmt);
        empty_stmt.replace(new_body_tree);

        Nodecl::Utils::prepend_to_enclosing_top_level_location(location, function_code);

        return new_callback;
    }

    TL::Symbol Intel::emit_callback_for_reduction(
            CombinerISA isa,
            TL::ObjectList<Nodecl::OpenMP::ReductionItem> &reduction_items,
            TL::Type reduction_pack_type,
            Nodecl::NodeclBase location,
            TL::Symbol current_function)
    {
        switch (isa)
        {
            case COMBINER_AVX2:
            case COMBINER_KNC:
                {
                    // When a SIMD KNC reduction is requested, we do not return a function but
                    // an array of functions
                    TL::ObjectList<SIMDReductionPair> pairs;

                    for (TL::ObjectList<Nodecl::OpenMP::ReductionItem>::iterator it = reduction_items.begin();
                            it != reduction_items.end();
                            it++)
                    {
                        SIMDReductionPair p = emit_callback_for_reduction_simd(
                                isa,
                                *it,
                                location,
                                current_function);

                        pairs.append(p);
                    }

                    return emit_array_of_reduction_simd_functions(pairs, location, current_function);
                }
            case COMBINER_SCALAR:
                {
                    return emit_callback_for_reduction_scalar(
                            reduction_items,
                            reduction_pack_type,
                            location,
                            current_function);
                }
            default:
                internal_error("Code unreachable", 0);
        }

    }

    struct UpdateReductionUses : Nodecl::ExhaustiveVisitor<void>
    {
        TL::Symbol &_reduction_pack_symbol;
        TL::ObjectList<TL::Symbol> &_reduction_symbols;

        UpdateReductionUses(TL::Symbol &reduction_pack_symbol,
                TL::ObjectList<TL::Symbol>& reduction_symbols)
            : _reduction_pack_symbol(reduction_pack_symbol),
            _reduction_symbols(reduction_symbols)
        {
        }

        virtual void visit(const Nodecl::Symbol &n)
        {
            TL::Symbol sym = n.get_symbol();
            if (!_reduction_symbols.contains(sym))
                return;

            TL::ObjectList<TL::Symbol> fields = _reduction_pack_symbol
                .get_type()
                .get_fields()
                .find(sym, std::function<std::string(const TL::Symbol&)>(&TL::Symbol::get_name));

            ERROR_CONDITION(fields.empty(), "Field '%s' not found", sym.get_name().c_str());
            ERROR_CONDITION(fields.size() > 1, "Too many fields", 0);

            TL::Symbol member_in_class = fields[0];

            Nodecl::NodeclBase class_member_access =
                Nodecl::ClassMemberAccess::make(
                        _reduction_pack_symbol.make_nodecl(/* set_ref_type */ true, n.get_locus()),
                        Nodecl::Symbol::make(member_in_class, n.get_locus()),
                        Nodecl::NodeclBase::null(),
                        n.get_type(),
                        n.get_locus());

            n.replace(class_member_access);
        }
    };

    void Intel::update_reduction_uses(Nodecl::NodeclBase node,
            const TL::ObjectList<Nodecl::OpenMP::ReductionItem>& reduction_items,
            TL::Symbol reduction_pack_symbol)
    {
        TL::ObjectList<Symbol> reduction_symbols = reduction_items
            .map<Nodecl::NodeclBase>(&Nodecl::OpenMP::ReductionItem::get_reduced_symbol) // TL::ObjectList<Nodecl::NodeclBase>
            .map<TL::Symbol>(&Nodecl::NodeclBase::get_symbol); // TL::ObjectList<TL::Symbol>

        UpdateReductionUses update(reduction_pack_symbol, reduction_symbols);
        update.walk(node);
    }

    TL::Symbol Intel::declare_reduction_pack(const TL::ObjectList<TL::Symbol> &reduction_symbols,
            Nodecl::NodeclBase location)
    {
        TL::Counter &private_num = TL::CounterManager::get_counter("intel-omp-privates");
        std::stringstream struct_name;
        struct_name << "reduction_pack_" << (int)private_num;
        private_num++;

        Source struct_decl;
        struct_decl << "struct " << struct_name.str() << " {";
        for (TL::ObjectList<TL::Symbol>::const_iterator it = reduction_symbols.begin();
                it != reduction_symbols.end();
                it++)
        {
            struct_decl << as_type(it->get_type()) << " " << it->get_name() << ";";
        }

        struct_decl << "};";
        TL::Scope global_scope = CURRENT_COMPILED_FILE->global_decl_context;
        Nodecl::NodeclBase decl = struct_decl.parse_declaration(global_scope);

        TL::Symbol result;
        if (IS_CXX_LANGUAGE)
        {
            Nodecl::Utils::prepend_to_enclosing_top_level_location(location, decl);
            result = global_scope.get_symbol_from_name(struct_name.str());
        }
        else
        {
            result = global_scope.get_symbol_from_name("struct " + struct_name.str());
        }

        ERROR_CONDITION(!result.is_valid(), "Invalid symbol", 0);
        ERROR_CONDITION(!result.is_class(), "Should be a class-name", 0);

        return result;
    }

    namespace {

        struct SIMDizeCombiner : Nodecl::NodeclVisitor<void>
        {
            TL::Symbol _orig_omp_in, _new_omp_in;
            TL::Symbol _orig_omp_out, _new_omp_out;
            TL::Symbol _new_omp_mask;

            int _vector_width_bytes;

            ReplaceInOutSIMD * _replace_inout;

            SIMDizeCombiner(int vector_width_bytes) :
                _vector_width_bytes(vector_width_bytes), _replace_inout(NULL) 
            {
            }

            void init(
                    TL::Symbol orig_omp_in,  TL::Symbol new_omp_in,
                    TL::Symbol orig_omp_out, TL::Symbol new_omp_out,
                                             TL::Symbol new_omp_mask,
                    ReplaceInOutSIMD * replace_inout)
            {
                _orig_omp_in = orig_omp_in;
                _new_omp_in = new_omp_in;
                _orig_omp_out = orig_omp_out;
                _new_omp_out = new_omp_out;
                _new_omp_mask = new_omp_mask;

                _replace_inout = replace_inout;
            }

            virtual ~SIMDizeCombiner()
            {
            }

            TL::Type vector_type_of_scalar(TL::Type t)
            {
                t = t.no_ref().get_unqualified_type();
                return t.get_vector_of_bytes(_vector_width_bytes);
            }

            TL::Type vector_mask_type_of_scalar(TL::Type t)
            {
                return TL::Type::get_mask_type(
                        _vector_width_bytes/t.get_size());
            }



            private:
                // Do not copy
                SIMDizeCombiner(const SIMDizeCombiner&);
                // Do not assign
                SIMDizeCombiner& operator=(const SIMDizeCombiner&);
        };

        struct SIMDizeCombinerKNC : SIMDizeCombiner
        {
            SIMDizeCombinerKNC() : SIMDizeCombiner(64) {}
        };

        struct SIMDizeVerticalCombinerKNC : SIMDizeCombinerKNC
        {
            virtual void visit(const Nodecl::ExpressionStatement& node)
            {
                walk(node.get_nest());
            }

            virtual void visit(const Nodecl::AddAssignment& node)
            {
                _replace_inout->walk(node.get_lhs());
                _replace_inout->walk(node.get_rhs());

                Nodecl::NodeclBase vector_add = Nodecl::VectorAdd::make(
                        Nodecl::VectorLoad::make(
                            Nodecl::Reference::make(
                                node.get_lhs().shallow_copy(),
                                node.get_lhs().get_type().no_ref().get_pointer_to()),
                            _new_omp_mask.make_nodecl(),
                            Nodecl::List::make(
                                Nodecl::AlignedFlag::make()),
                            vector_type_of_scalar(node.get_lhs().get_type()).get_lvalue_reference_to()),
                        Nodecl::VectorLoad::make(
                            Nodecl::Reference::make(
                                node.get_rhs().shallow_copy(),
                                node.get_rhs().get_type().no_ref().get_pointer_to()),
                            _new_omp_mask.make_nodecl(),
                            Nodecl::List::make(
                                Nodecl::AlignedFlag::make()),
                            vector_type_of_scalar(node.get_rhs().get_type()).get_lvalue_reference_to()),
                        _new_omp_mask.make_nodecl(),
                        vector_type_of_scalar(node.get_type()));

                Nodecl::NodeclBase vector_store = Nodecl::VectorStore::make(
                        Nodecl::Reference::make(
                            node.get_lhs().shallow_copy(),
                            vector_type_of_scalar(node.get_type()).get_pointer_to()),
                        vector_add,
                        _new_omp_mask.make_nodecl(),
                        Nodecl::List::make(
                            Nodecl::AlignedFlag::make()),
                        vector_type_of_scalar(node.get_type()));

                node.replace(vector_store);
            }

            virtual void unhandled_node(const Nodecl::NodeclBase& n)
            {
                internal_error("Unhandled node '%s'\n", ast_print_node_type(n.get_kind()));
            }
        };

        struct SIMDizeHorizontalCombinerKNC : SIMDizeCombinerKNC
        {
            virtual void visit(const Nodecl::ExpressionStatement& node)
            {
                walk(node.get_nest());
            }

            virtual void visit(const Nodecl::AddAssignment& node)
            {
                _replace_inout->walk(node.get_lhs());
                _replace_inout->walk(node.get_rhs());

                // Do not emit AddAssigment: old value is already in lane 0
                Nodecl::NodeclBase assignment =
                    Nodecl::Assignment::make(
                            node.get_lhs().shallow_copy(),
                            Nodecl::VectorReductionAdd::make(
                                Nodecl::VectorLoad::make(
                                    Nodecl::Reference::make(
                                        node.get_rhs().shallow_copy(),
                                        node.get_rhs().get_type().no_ref().get_pointer_to()),
                                    _new_omp_mask.make_nodecl(),
                                    Nodecl::List::make(
                                        Nodecl::AlignedFlag::make()),
                                    vector_type_of_scalar(node.get_rhs().get_type()).get_lvalue_reference_to()),
                                _new_omp_mask.make_nodecl(),
                                node.get_type()),
                            node.get_type());

                node.replace(assignment);
            }

            virtual void unhandled_node(const Nodecl::NodeclBase& n)
            {
                internal_error("Unhandled node '%s'\n", ast_print_node_type(n.get_kind()));
            }
        };

        struct SIMDizeCombinerAVX2 : SIMDizeCombiner
        {
            SIMDizeCombinerAVX2() : SIMDizeCombiner(32) {}
        };

        struct SIMDizeVerticalCombinerAVX2 : SIMDizeCombinerAVX2
        {
            virtual void visit(const Nodecl::ExpressionStatement& node)
            {
                walk(node.get_nest());
            }

            virtual void visit(const Nodecl::AddAssignment& node)
            {
                _replace_inout->walk(node.get_lhs());
                _replace_inout->walk(node.get_rhs());

                Nodecl::NodeclBase vector_add = Nodecl::VectorAdd::make(
                        Nodecl::VectorLoad::make(
                            Nodecl::Reference::make(
                                node.get_lhs().shallow_copy(),
                                node.get_lhs().get_type().no_ref().get_pointer_to()),
                            _new_omp_mask.make_nodecl(),
                            Nodecl::List::make(
                                Nodecl::AlignedFlag::make()),
                            vector_type_of_scalar(node.get_lhs().get_type()).get_lvalue_reference_to()),
                        Nodecl::VectorLoad::make(
                            Nodecl::Reference::make(
                                node.get_rhs().shallow_copy(),
                                node.get_rhs().get_type().no_ref().get_pointer_to()),
                            _new_omp_mask.make_nodecl(),
                            Nodecl::List::make(
                                Nodecl::AlignedFlag::make()),
                            vector_type_of_scalar(node.get_rhs().get_type()).get_lvalue_reference_to()),
                        _new_omp_mask.make_nodecl(),
                        vector_type_of_scalar(node.get_type()));

                Nodecl::NodeclBase vector_store = Nodecl::VectorStore::make(
                        Nodecl::Reference::make(
                            node.get_lhs().shallow_copy(),
                            vector_type_of_scalar(node.get_type()).get_pointer_to()),
                        vector_add,
                        _new_omp_mask.make_nodecl(),
                        Nodecl::List::make(
                            Nodecl::AlignedFlag::make()),
                        vector_type_of_scalar(node.get_type()));

                node.replace(vector_store);
            }

            virtual void unhandled_node(const Nodecl::NodeclBase& n)
            {
                internal_error("Unhandled node '%s'\n", ast_print_node_type(n.get_kind()));
            }
        };

        struct SIMDizeHorizontalCombinerAVX2 : SIMDizeCombinerAVX2
        {
            virtual void visit(const Nodecl::ExpressionStatement& node)
            {
                walk(node.get_nest());
            }

            virtual void visit(const Nodecl::AddAssignment& node)
            {
                _replace_inout->walk(node.get_lhs());
                _replace_inout->walk(node.get_rhs());

                TL::Type vector_type =
                    vector_type_of_scalar(node.get_rhs().get_type()).no_ref();

                // Do not emit AddAssigment: old value is already in lane 0
                Nodecl::NodeclBase assignment =
                    Nodecl::Assignment::make(
                            node.get_lhs().shallow_copy(),
                            Nodecl::VectorReductionAdd::make(
                                Nodecl::VectorLoad::make(
                                    Nodecl::Reference::make(
                                        node.get_rhs().shallow_copy(),
                                        node.get_rhs().get_type().no_ref().get_pointer_to()),
                                    _new_omp_mask.make_nodecl(),
                                    Nodecl::List::make(
                                        Nodecl::AlignedFlag::make()),
                                    vector_type),
                                _new_omp_mask.make_nodecl(),
                                node.get_type()),
                            node.get_type());

                node.replace(assignment);
            }

            virtual void unhandled_node(const Nodecl::NodeclBase& n)
            {
                internal_error("Unhandled node '%s'\n", ast_print_node_type(n.get_kind()));
            }
        };


        TL::Symbol generate_simd_combiner(
                TL::Intel::CombinerISA isa,
                SIMDizeCombiner &simdizer,
                Nodecl::OpenMP::ReductionItem &reduction_item,
                Nodecl::NodeclBase location,
                TL::Symbol current_function)
        {
            TL::ObjectList<std::string> parameter_names;
            TL::ObjectList<TL::Type> parameter_types;

            parameter_names.append("red_omp_out");
            parameter_types.append(reduction_item.get_reduction_type().get_type().get_lvalue_reference_to());

            parameter_names.append("red_omp_in");
            parameter_types.append(reduction_item.get_reduction_type().get_type().get_lvalue_reference_to());

//            TL::Symbol mmask_16_typedef = current_function.get_scope().get_symbol_from_name("__mmask16");
//            ERROR_CONDITION(!mmask_16_typedef.is_valid(), "__mmask16 not found in the scope", 0);

            parameter_names.append("red_omp_mask");
            parameter_types.append(simdizer.vector_mask_type_of_scalar(
                        reduction_item.get_reduction_type().get_type()));
            //mmask_16_typedef.get_user_defined_type());

            TL::Counter &counters = TL::CounterManager::get_counter("intel-omp-reduction");
            std::stringstream ss;
            ss << "_red_" << (int)counters;
            counters++;

            TL::Symbol new_callback = SymbolUtils::new_function_symbol(
                    current_function,
                    ss.str(),
                    get_void_type(),
                    parameter_names,
                    parameter_types);

            Nodecl::NodeclBase function_code, empty_stmt;

            SymbolUtils::build_empty_body_for_function(
                    new_callback,
                    function_code,
                    empty_stmt);

            TL::Symbol red_omp_out = empty_stmt.retrieve_context().get_symbol_from_name("red_omp_out");
            TL::Symbol red_omp_in = empty_stmt.retrieve_context().get_symbol_from_name("red_omp_in");
            TL::Symbol red_omp_mask = empty_stmt.retrieve_context().get_symbol_from_name("red_omp_mask");

            TL::Source combiner;

            TL::Symbol reductor = reduction_item.get_reductor().get_symbol();
            OpenMP::Reduction* reduction = OpenMP::Reduction::get_reduction_info_from_symbol(reductor);

            Nodecl::NodeclBase combiner_expr = reduction->get_combiner().shallow_copy();

            ReplaceInOutSIMD *replace_inout_simd;

            switch (isa)
            {
                case TL::Intel::COMBINER_AVX2:
                    //TODO
                case TL::Intel::COMBINER_KNC:
                    replace_inout_simd = new ReplaceInOutSIMDKNC(
                            reduction->get_omp_in(), red_omp_in,
                            reduction->get_omp_out(), red_omp_out);
                    break;
                default:
                    internal_error("Code unreachable", 0);
            }

            simdizer.init(
                    reduction->get_omp_in(), red_omp_in,
                    reduction->get_omp_out(), red_omp_out,
                    red_omp_mask,
                    replace_inout_simd);
            simdizer.walk(combiner_expr);

            delete replace_inout_simd;

            combiner << as_expression(combiner_expr) << ";"
                ;

            Nodecl::NodeclBase new_body_tree = combiner.parse_statement(empty_stmt);
            empty_stmt.replace(new_body_tree);

            Nodecl::Utils::prepend_to_enclosing_top_level_location(location, function_code);

            return new_callback;
        }
    }

    TL::Symbol Intel::emit_array_of_reduction_simd_functions(
            const TL::ObjectList<SIMDReductionPair>& pairs,
            Nodecl::NodeclBase location,
            TL::Symbol current_function)
    {
        TL::Counter &counters = TL::CounterManager::get_counter("intel-omp-reduction");
        std::stringstream ss;
        ss << "_red_arr_" << (int)counters;
        counters++;

        TL::ObjectList<TL::Type> void_ptr_void_ptr;
        void_ptr_void_ptr.append(TL::Type::get_void_type().get_pointer_to());
        void_ptr_void_ptr.append(TL::Type::get_void_type().get_pointer_to());

        TL::Type fun_type = TL::Type::get_void_type().
            get_function_returning(void_ptr_void_ptr);

        TL::Type ptr_fun_type = fun_type.get_pointer_to();
        TL::Type array_2_ptr_fun_type = ptr_fun_type.get_array_to(
                const_value_to_nodecl(const_value_get_signed_int(2 * pairs.size())),
                current_function.get_scope());

        TL::Symbol array_of_pf = current_function.get_scope().new_symbol(ss.str());
        array_of_pf.get_internal_symbol()->kind = SK_VARIABLE;
        symbol_entity_specs_set_is_user_declared(array_of_pf.get_internal_symbol(), 1);
        array_of_pf.set_type(array_2_ptr_fun_type);


        TL::ObjectList<Nodecl::NodeclBase> initializers;
        for (TL::ObjectList<SIMDReductionPair>::const_iterator it = pairs.begin();
                it != pairs.end();
                it++)
        {
            Nodecl::NodeclBase cast;
            initializers.append(
                    cast = Nodecl::Conversion::make(
                        it->vertical_combiner.make_nodecl(),
                        ptr_fun_type));
            cast.set_text("C");

            initializers.append(
                    cast = Nodecl::Conversion::make(
                        it->horizontal_combiner.make_nodecl(),
                        ptr_fun_type));
            cast.set_text("C");
        }

        Nodecl::NodeclBase array_initializer =
            Nodecl::StructuredValue::make(
                    Nodecl::List::make(initializers),
                    Nodecl::StructuredValueBracedImplicit::make(),
                    array_of_pf.get_type());
        array_of_pf.set_value(array_initializer);

        Nodecl::NodeclBase object_init = Nodecl::ObjectInit::make(array_of_pf);
        Nodecl::Utils::prepend_to_enclosing_top_level_location(location, object_init);
        return array_of_pf;
    }

    template <typename VerticalSimdizer, typename HorizontalSimdizer>
    Intel::SIMDReductionPair
    generate_simd_combiners(
            Intel::CombinerISA isa,
            Nodecl::OpenMP::ReductionItem &reduction_item,
            Nodecl::NodeclBase location,
            TL::Symbol current_function)
    {
        VerticalSimdizer vertical_simdizer;
        HorizontalSimdizer horizontal_simdizer;

        Intel::SIMDReductionPair p;
        p.vertical_combiner = generate_simd_combiner(
                isa,
                vertical_simdizer,
                reduction_item,
                location,
                current_function);

        p.horizontal_combiner = generate_simd_combiner(
                isa,
                horizontal_simdizer,
                reduction_item,
                location,
                current_function);
        return p;
    }

    // SIMD - KNC
    Intel::SIMDReductionPair Intel::emit_callback_for_reduction_simd(
            CombinerISA isa,
            Nodecl::OpenMP::ReductionItem &reduction_item,
            Nodecl::NodeclBase location,
            TL::Symbol current_function)
    {
        switch (isa)
        {
            case TL::Intel::COMBINER_AVX2:
                return generate_simd_combiners<SIMDizeVerticalCombinerAVX2, SIMDizeHorizontalCombinerAVX2>(
                        isa,
                        reduction_item,
                        location,
                        current_function);
            case TL::Intel::COMBINER_KNC:
                return generate_simd_combiners<SIMDizeVerticalCombinerKNC, SIMDizeHorizontalCombinerKNC>(
                        isa,
                        reduction_item,
                        location,
                        current_function);
            default:
                internal_error("Code unreachable", 0);
        }
    }
}
