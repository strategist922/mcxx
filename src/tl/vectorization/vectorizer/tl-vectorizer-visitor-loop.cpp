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

#include "tl-vectorizer-visitor-loop.hpp"

#include "tl-vectorization-utils.hpp"
#include "tl-vectorization-analysis-interface.hpp"

#include "tl-vectorizer.hpp"
#include "tl-vectorizer-visitor-local-symbol.hpp"
#include "tl-vectorizer-visitor-statement.hpp"
#include "tl-vectorizer-visitor-expression.hpp"

#include "cxx-cexpr.h"
#include "tl-nodecl-utils.hpp"


namespace TL
{
namespace Vectorization
{
    VectorizerVisitorLoop::VectorizerVisitorLoop(VectorizerEnvironment& environment) :
        _environment(environment)
    {
    }


    void VectorizerVisitorLoop::visit(const Nodecl::ForStatement& for_statement)
    {
        // CACHE: Before vectorizing!
//        Nodecl::List cache_it_update_pre = _environment._vectorizer_cache.get_iteration_update_pre(_environment);
//        Nodecl::List cache_it_update_post = _environment._vectorizer_cache.get_iteration_update_post(_environment);

        // Vectorize Local Symbols
        VectorizerVisitorLocalSymbol visitor_local_symbol(_environment);
        visitor_local_symbol.walk(for_statement);

        // Vectorize Loop Header
        VectorizerVisitorLoopHeader visitor_loop_header(_environment);
        visitor_loop_header.walk(for_statement.get_loop_header().as<Nodecl::LoopControl>());

        // LOOP BODY
        VectorizerVisitorStatement visitor_stmt(_environment);
        visitor_stmt.walk(for_statement.get_statement());
    }

    void VectorizerVisitorLoop::visit(const Nodecl::WhileStatement& while_statement)
    {
        // Vectorize Loop Header
        VectorizerVisitorLoopCond visitor_loop_cond(_environment);
        visitor_loop_cond.walk(while_statement.get_condition());

        // LOOP BODY
        VectorizerVisitorStatement visitor_stmt(_environment);
        visitor_stmt.walk(while_statement.get_statement());
    }


    Nodecl::NodeclVisitor<void>::Ret VectorizerVisitorLoop::unhandled_node(const Nodecl::NodeclBase& n)
    {
        std::cerr << "For Visitor: Unknown node "
            << ast_print_node_type(n.get_kind())
            << " at " << n.get_locus_str()
            << std::endl;

        return Ret();
    }

    VectorizerVisitorLoopHeader::VectorizerVisitorLoopHeader(VectorizerEnvironment& environment)
        : _environment(environment)
    {
    }

    void VectorizerVisitorLoopHeader::visit(const Nodecl::LoopControl& loop_control)
    {
        // Init
        VectorizerVisitorLoopInit visitor_loop_init;
        visitor_loop_init.walk(loop_control.get_init());

        // Cond
        VectorizerVisitorLoopCond visitor_loop_cond(_environment);
        visitor_loop_cond.walk(loop_control.get_cond());

        // Next
        VectorizerVisitorLoopNext visitor_loop_next(_environment);
        visitor_loop_next.walk(loop_control.get_next());
    }

    Nodecl::NodeclVisitor<void>::Ret VectorizerVisitorLoopHeader::unhandled_node(const Nodecl::NodeclBase& n)
    {
        std::cerr << "Loop Header Visitor: Unknown node "
            << ast_print_node_type(n.get_kind())
            << " at " << n.get_locus_str()
            << std::endl;

        return Ret();
    }

    VectorizerVisitorLoopInit::VectorizerVisitorLoopInit(void)
    {
    }

    void VectorizerVisitorLoopInit::visit(const Nodecl::ObjectInit& node)
    {
        running_error("Vectorizer (%s): Declaration of new variables is not supported yet "\
                "in LoopControl. Please, declare them outside of the loop.",
                node.get_locus_str().c_str());
    }

    void VectorizerVisitorLoopInit::visit(const Nodecl::Assignment& node)
    {
    }

    void VectorizerVisitorLoopInit::visit(const Nodecl::Comma& node)
    {
        // TODO
        walk(node.get_lhs());
        walk(node.get_rhs());
    }

    Nodecl::NodeclVisitor<void>::Ret VectorizerVisitorLoopInit::unhandled_node(const Nodecl::NodeclBase& n)
    {
        std::cerr << "Loop Init Visitor: Unknown node "
            << ast_print_node_type(n.get_kind())
            << " at " << n.get_locus_str()
            << std::endl;

        return Ret();
    }

    VectorizerVisitorLoopCond::VectorizerVisitorLoopCond(const VectorizerEnvironment& environment) :
        _environment(environment)
    {
    }

    void VectorizerVisitorLoopCond::visit(const Nodecl::Equal& node)
    {
        visit_condition(node);
    }

    void VectorizerVisitorLoopCond::visit(const Nodecl::LowerThan& node)
    {
        visit_condition(node);
    }

    void VectorizerVisitorLoopCond::visit(const Nodecl::LowerOrEqualThan& node)
    {
        visit_condition(node);
    }

    void VectorizerVisitorLoopCond::visit(const Nodecl::GreaterThan& node)
    {
        visit_condition(node);
    }

    void VectorizerVisitorLoopCond::visit(const Nodecl::GreaterOrEqualThan& node)
    {
        visit_condition(node);
    }

    void VectorizerVisitorLoopCond::visit_condition(
            const Nodecl::NodeclBase& node)
    {
        Nodecl::Equal condition = node.as<Nodecl::Equal>();
        Nodecl::NodeclBase lhs = condition.get_lhs();
        Nodecl::NodeclBase rhs = condition.get_rhs();

        bool lhs_const_flag = VectorizationAnalysisInterface::
            _vectorizer_analysis->is_uniform(
                    _environment._analysis_simd_scope, lhs, lhs);
        bool rhs_const_flag = VectorizationAnalysisInterface::
            _vectorizer_analysis->is_uniform(
                    _environment._analysis_simd_scope, rhs, rhs);

        Nodecl::NodeclBase result = Nodecl::NodeclBase::null();

        if (!lhs_const_flag && rhs_const_flag)
        {
            TL::Type rhs_type = rhs.get_type();

            // Step
            // TODO: Not only the trivial case
            Nodecl::NodeclBase step;
            Nodecl::NodeclBase new_step;

            if (VectorizationAnalysisInterface::_vectorizer_analysis->
                    is_linear(_environment._analysis_simd_scope, lhs))
            {
                step = VectorizationAnalysisInterface::_vectorizer_analysis->
                    get_linear_step(_environment._analysis_scopes.back(), lhs);

                new_step = Vectorization::Utils::make_scalar_binary_node
                    <Nodecl::Mul>(
                            step.shallow_copy(),
                            const_value_to_nodecl(const_value_get_signed_int(
                                _environment._vectorization_factor)),
                            TL::Type::get_int_type(),
                            const_value_mul);
            }
            else
            {
                running_error("Vectorizer (%s): Induction variable cannot be found in LoopCondition.",
                        node.get_locus_str().c_str());
            }

            result = rhs.shallow_copy();

            // rhs => (rhs-(step-1))
            const Nodecl::NodeclBase new_node =
                Vectorization::Utils::make_scalar_binary_node<Nodecl::Minus>(
                        rhs.shallow_copy(),
                        Vectorization::Utils::make_scalar_binary_node
                        <Nodecl::Minus>(
                            new_step,
                            const_value_to_nodecl(const_value_get_one(4, 1)),
                            rhs_type,
                            const_value_sub),
                        rhs_type,
                        const_value_sub);

            rhs.replace(new_node);
        }
        else if (lhs_const_flag && !rhs_const_flag)
        {
            TL::Type lhs_type = lhs.get_type();

            // Step
            // TODO: Not only the trivial case
            Nodecl::NodeclBase step;
            Nodecl::Mul new_step;

            if (VectorizationAnalysisInterface::_vectorizer_analysis->
                    is_induction_variable(_environment._analysis_simd_scope,
                        rhs))
            {
                step = VectorizationAnalysisInterface::_vectorizer_analysis->
                    get_linear_step(_environment._analysis_scopes.back(), rhs);

                new_step = Vectorization::Utils::make_scalar_binary_node<Nodecl::Mul>(
                        step.shallow_copy(),
                        const_value_to_nodecl(
                            const_value_get_signed_int(_environment._vectorization_factor)),
                        step.get_type(),
                        const_value_mul);
            }
            else
            {
                running_error("Vectorizer (%s): Induction variable cannot be found in LoopCondition.",
                        node.get_locus_str().c_str());
            }

            result = lhs.shallow_copy();

            // lhs = (lhs-(step-1))
            const Nodecl::Minus new_node =
                Vectorization::Utils::make_scalar_binary_node<Nodecl::Minus>(
                        lhs.shallow_copy(),
                        Vectorization::Utils::make_scalar_binary_node<Nodecl::Minus>(
                            new_step,
                            const_value_to_nodecl(const_value_get_one(4, 1)),
                            lhs_type,
                            const_value_sub),
                        lhs_type,
                        const_value_sub);

            lhs.replace(new_node);
        }
        else if (lhs_const_flag && rhs_const_flag)
        {
            running_error("Vectorizer (%s): Loop is not vectorizable because of the loop "
                    "condition. BOTH expressions are CONSTANT.", locus_to_str(node.get_locus()));
        }
        else
        {
            running_error("Vectorizer (%s): Loop is not vectorizable because of the loop "
                    "condition. BOTH expressions are NOT CONSTANT.", locus_to_str(node.get_locus()));
        }
    }

    Nodecl::NodeclVisitor<void>::Ret VectorizerVisitorLoopCond::unhandled_node(const Nodecl::NodeclBase& n)
    {
        std::cerr << "Loop Cond Visitor: Unknown node "
            << ast_print_node_type(n.get_kind())
            << " at " << n.get_locus_str()
            << std::endl;
    }

    VectorizerVisitorLoopNext::VectorizerVisitorLoopNext(VectorizerEnvironment& environment) :
        _environment(environment)
    {
    }

    void VectorizerVisitorLoopNext::visit(const Nodecl::Assignment& node)
    {
        visit_increment(node, node.get_lhs());
    }

    void VectorizerVisitorLoopNext::visit(const Nodecl::Comma& node)
    {
        // TODO
        walk(node.get_lhs());
        walk(node.get_rhs());
    }

    void VectorizerVisitorLoopNext::visit_increment(const Nodecl::NodeclBase& node, const Nodecl::NodeclBase& lhs)
    {
        VectorizerVisitorExpression visitor_expression(_environment);

        visitor_expression.walk(node);
    }

    Nodecl::NodeclVisitor<void>::Ret VectorizerVisitorLoopNext::unhandled_node(
            const Nodecl::NodeclBase& n)
    {
        std::cerr << "Loop Next Visitor: Unknown node "
            << ast_print_node_type(n.get_kind())
            << " at " << n.get_locus_str()
            << std::endl;
    }


    VectorizerVisitorLoopEpilog::VectorizerVisitorLoopEpilog(VectorizerEnvironment& environment,
            int epilog_iterations, bool only_epilog, bool is_parallel_loop) :
        _environment(environment), _epilog_iterations(epilog_iterations),
        _only_epilog(only_epilog), _is_parallel_loop(is_parallel_loop)
    {
        if (_epilog_iterations == 0)
            internal_error("Vectorizer: Epilog with 0 iterations", 0);
    }

    void VectorizerVisitorLoopEpilog::visit(
            const Nodecl::NodeclBase& loop_statement,
            Nodecl::NodeclBase& net_epilog_node)
    {
        Nodecl::NodeclBase loop_cond;
       
        if (loop_statement.is<Nodecl::ForStatement>())
            loop_cond = loop_statement.as<Nodecl::ForStatement>().
                get_loop_header().as<Nodecl::LoopControl>().get_cond();
        else if (loop_statement.is<Nodecl::WhileStatement>())
            loop_cond = loop_statement.as<Nodecl::WhileStatement>().get_condition();
        else
            internal_error("Vectorizer: Epilog visit. Neither a ForStatement"\
                   " nor a WhileStatement", 0);

        if(_environment._support_masking)
        {
            visit_vector_epilog(loop_statement, loop_cond, net_epilog_node);
        }
        else
        {
            visit_scalar_epilog(loop_statement, loop_cond, net_epilog_node);
        }
    }

    void VectorizerVisitorLoopEpilog::visit_vector_epilog(
            const Nodecl::NodeclBase& loop_statement,
            const Nodecl::NodeclBase& loop_cond,
            Nodecl::NodeclBase& net_epilog_node)
    {
        Nodecl::CompoundStatement comp_statement =
            Nodecl::Utils::skip_contexts_and_lists(loop_statement.as<Nodecl::ForStatement>().
            get_statement()).as<Nodecl::CompoundStatement>();

        // Vectorize Local Symbols
        VectorizerVisitorLocalSymbol visitor_local_symbol(_environment);
        visitor_local_symbol.walk(loop_statement);

        Nodecl::NodeclBase mask_nodecl_sym = Utils::get_new_mask_symbol(
                _environment._analysis_simd_scope,
                _environment._vectorization_factor, true);


        Nodecl::List result_stmt_list;

        // Set new IV init
        Nodecl::ExpressionStatement new_iv_init;
        Nodecl::NodeclBase iv;
        Nodecl::NodeclBase iv_init;
        if (_is_parallel_loop || _only_epilog)
        {
            // TODO:: get_updated_iv_init_for_epilog does not support WhileStatement
            get_updated_iv_init_for_epilog(loop_statement.as<Nodecl::ForStatement>(), iv, iv_init);

            new_iv_init = Nodecl::ExpressionStatement::make(
                    Nodecl::Assignment::make(
                        iv.shallow_copy(),
                        iv_init.shallow_copy(),
                        iv.get_type(),
                        iv.get_locus()));

            result_stmt_list.append(new_iv_init);
        }

        // Compute epilog mask expression and add it after vectorization
        Nodecl::ExpressionStatement mask_exp;
        if (_epilog_iterations != 1)
        {
            // Get mask for epilog instructions
            Nodecl::NodeclBase mask_value;
            if (_epilog_iterations > 0 || _only_epilog) // Constant value
            {
                mask_value = Vectorization::Utils::get_contiguous_mask_literal(
                        _environment._vectorization_factor,
                        _epilog_iterations);
            }
            else // Unknown number of iterations
            {
                // We are not shallow_copying.
                // It's not possible because we need to use the analysis
                mask_value = loop_cond;

                // Add all-one MaskLiteral to mask_list in order to vectorize the mask_value
                Nodecl::MaskLiteral all_one_mask =
                    Vectorization::Utils::get_contiguous_mask_literal(
                            _environment._vectorization_factor,
                            _environment._vectorization_factor);
                _environment._mask_list.push_back(all_one_mask);

                // Vectorising mask
                VectorizerVisitorExpression visitor_mask(_environment);
                visitor_mask.walk(mask_value);

                _environment._mask_list.pop_back();
            }


            if (mask_nodecl_sym.get_type().no_ref().is_same_type(mask_value.get_type().no_ref()))
            {
                //    std::cerr << "Masks have the same type" << std::endl;
                mask_exp = Nodecl::ExpressionStatement::make(
                        Nodecl::VectorMaskAssignment::make(mask_nodecl_sym,
                            mask_value.shallow_copy(),
                            mask_nodecl_sym.get_type(),
                            loop_statement.get_locus()));
            }
            else
            {
                //    std::cerr << "Masks don't have the same type" << std::endl;

                mask_exp = Nodecl::ExpressionStatement::make(
                        Nodecl::VectorMaskAssignment::make(mask_nodecl_sym,
                            Nodecl::VectorMaskConversion::make(
                                mask_value.shallow_copy(),
                                mask_nodecl_sym.get_type(),
                                loop_statement.get_locus()),
                            mask_nodecl_sym.get_type(),
                            loop_statement.get_locus()));
            }

            result_stmt_list.append(mask_exp);
        }

        // Vectorize Loop Body if iterations > 1
        if (_epilog_iterations != 1)
        {
            _environment._mask_list.push_back(mask_nodecl_sym);

            VectorizerVisitorStatement visitor_stmt(_environment);
            visitor_stmt.walk(comp_statement);

            _environment._mask_list.pop_back();
        }

        // Same as comp_statement
        Nodecl::NodeclBase loop_inner_statement = loop_statement.as<Nodecl::ForStatement>().
            get_statement().as<Nodecl::List>().front().shallow_copy();


        // Add IF check to skip epilog if mask is not zero
        Nodecl::NodeclBase if_mask_is_not_zero;
        if (_epilog_iterations == -1)
        {
            if_mask_is_not_zero =
                Vectorization::Utils::get_if_mask_is_not_zero_nodecl(
                        mask_nodecl_sym.shallow_copy(),
                        loop_inner_statement);

            result_stmt_list.append(if_mask_is_not_zero);
        }
        else
        {
            result_stmt_list.append(loop_inner_statement);
        }

        // Replace for by list of statements
        Nodecl::CompoundStatement result_compound_statement =
            Nodecl::CompoundStatement::make(result_stmt_list,
                    Nodecl::NodeclBase::null());

        loop_statement.replace(result_compound_statement);

        // Output reference to just the epilog code
        net_epilog_node = loop_inner_statement;
    }

    void VectorizerVisitorLoopEpilog::visit_scalar_epilog(
            const Nodecl::NodeclBase& loop_statement,
            const Nodecl::NodeclBase& loop_cond,
            Nodecl::NodeclBase& net_epilog_node)
    {
        // Set new IV init
        Nodecl::NodeclBase new_iv_init;
        Nodecl::NodeclBase iv;
        Nodecl::NodeclBase iv_init;
        if (_is_parallel_loop || _only_epilog)
        {
            // TODO:: get_updated_iv_init_for_epilog doesn't support WhileStatement
            get_updated_iv_init_for_epilog(loop_statement.as<Nodecl::ForStatement>(), iv, iv_init);

            new_iv_init = Nodecl::List::make(
                    Nodecl::Assignment::make(
                        iv.shallow_copy(),
                        iv_init.shallow_copy(),
                        iv.get_type(),
                        iv.get_locus()));
        }
        else
        {
            new_iv_init = Nodecl::NodeclBase::null();
        }

        // TODO:: This doesn't work with Whiles
        Nodecl::LoopControl loop_control =
            loop_statement.as<Nodecl::ForStatement>().get_loop_header().as<Nodecl::LoopControl>();

        loop_control.set_init(new_iv_init);

        net_epilog_node = loop_statement;
    }

    void VectorizerVisitorLoopEpilog::get_updated_iv_init_for_epilog(
            const Nodecl::ForStatement& for_statement,
            Nodecl::NodeclBase &induction_variable,
            Nodecl::NodeclBase &iv_init)
    {
        TL::ForStatementHelper<TL::NoNewNodePolicy> tl_for_statement(for_statement);

        if(!tl_for_statement.is_omp_valid_loop())
        {
            running_error("Epilog loop is not an OpenMP valid loop");
        }

        // i = (upper_bound) - ((upper_bound) % UnrollFactor) + (lower_bound)

        Nodecl::NodeclBase upper_bound = tl_for_statement.get_upper_bound();
        Nodecl::NodeclBase upper_bound_plus_one = 
            Vectorization::Utils::make_scalar_binary_node<Nodecl::Add>(
                upper_bound.shallow_copy(),
                const_value_to_nodecl(const_value_get_one(1, 4)),
                upper_bound.get_type(),
                const_value_add);
        Nodecl::NodeclBase lower_bound = tl_for_statement.get_lower_bound();

        TL::Symbol iv = tl_for_statement.get_induction_variable();
        TL::Type iv_type = iv.get_type();

        // Induction Variable
        induction_variable = iv.make_nodecl(true);

        // Induction Variable Init
        iv_init = Vectorization::Utils::make_scalar_binary_node<Nodecl::Minus>(
                upper_bound_plus_one.shallow_copy(),
                Vectorization::Utils::make_scalar_binary_node<Nodecl::Mod>(
                    Vectorization::Utils::make_scalar_binary_node<Nodecl::Minus>(
                        upper_bound_plus_one.shallow_copy(),
                        lower_bound.shallow_copy(),
                        iv_type,
                        const_value_sub),
                    const_value_to_nodecl(
                        const_value_get_signed_int(_environment._vectorization_factor)),
                    iv_type,
                    const_value_mod),
                iv_type,
                const_value_sub);
    }
}
}
