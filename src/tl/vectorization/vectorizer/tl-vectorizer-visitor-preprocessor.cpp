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

#include "tl-vectorizer-visitor-preprocessor.hpp"

#include "cxx-cexpr.h"

namespace TL
{
namespace Vectorization
{
    VectorizerVisitorPreprocessor::VectorizerVisitorPreprocessor(
            const VectorizerEnvironment& environment)
        : _environment(environment)
    {
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::AddAssignment& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());

        const Nodecl::Assignment assignment =
            Nodecl::Assignment::make(
                    n.get_lhs().shallow_copy(),
                    Nodecl::Add::make(
                        n.get_lhs().shallow_copy(),
                        n.get_rhs().shallow_copy(),
                        n.get_type(),
                        n.get_locus()),
                    n.get_type(),
                    n.get_locus());

        n.replace(assignment);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::MinusAssignment& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());

        const Nodecl::Assignment assignment =
            Nodecl::Assignment::make(
                    n.get_lhs().shallow_copy(),
                    Nodecl::Minus::make(
                        n.get_lhs().shallow_copy(),
                        n.get_rhs().shallow_copy(),
                        n.get_type(),
                        n.get_locus()),
                    n.get_type(),
                    n.get_locus());

        n.replace(assignment);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::MulAssignment& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());

        const Nodecl::Assignment assignment =
            Nodecl::Assignment::make(
                    n.get_lhs().shallow_copy(),
                    Nodecl::Mul::make(
                        n.get_lhs().shallow_copy(),
                        n.get_rhs().shallow_copy(),
                        n.get_type(),
                        n.get_locus()),
                    n.get_type(),
                    n.get_locus());

        n.replace(assignment);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::DivAssignment& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());

        const Nodecl::Assignment assignment =
            Nodecl::Assignment::make(
                    n.get_lhs().shallow_copy(),
                    Nodecl::Div::make(
                        n.get_lhs().shallow_copy(),
                        n.get_rhs().shallow_copy(),
                        n.get_type(),
                        n.get_locus()),
                    n.get_type(),
                    n.get_locus());

        n.replace(assignment);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::ModAssignment& n)
    {
        walk(n.get_lhs());
        walk(n.get_rhs());

        const Nodecl::Assignment assignment =
            Nodecl::Assignment::make(
                    n.get_lhs().shallow_copy(),
                    Nodecl::Mod::make(
                        n.get_lhs().shallow_copy(),
                        n.get_rhs().shallow_copy(),
                        n.get_type(),
                        n.get_locus()),
                    n.get_type(),
                    n.get_locus());

        n.replace(assignment);
    }

    void VectorizerVisitorPreprocessor::visit_pre_post_increment(
            const Nodecl::Preincrement& n)
    {
        Nodecl::NodeclBase parent = n.get_parent();

        if(parent.is<Nodecl::ExpressionStatement>() || 
                 parent.get_parent().is<Nodecl::LoopControl>())
        {
            Nodecl::Assignment new_increment = 
                Nodecl::Assignment::make(
                        n.get_rhs().shallow_copy(),
                        Nodecl::Add::make(
                            n.get_rhs().shallow_copy(),
                            const_value_to_nodecl(const_value_get_one(
                                    n.get_type().get_size(), 1)),
                            n.get_type(),
                            n.get_locus()),
                        n.get_type(),
                        n.get_locus());

            n.replace(new_increment);
        }
    }

    void VectorizerVisitorPreprocessor::visit_pre_post_decrement(
            const Nodecl::Predecrement& n)
    {
        Nodecl::NodeclBase parent = n.get_parent();

        if(parent.is<Nodecl::ExpressionStatement>() || 
                parent.is<Nodecl::LoopControl>())
        {
            Nodecl::Assignment new_decrement = 
                Nodecl::Assignment::make(
                        n.get_rhs().shallow_copy(),
                        Nodecl::Minus::make(
                            n.get_rhs().shallow_copy(),
                            const_value_to_nodecl(const_value_get_one(
                                    n.get_type().get_size(), 1)),
                            n.get_type(),
                            n.get_locus()),
                        n.get_type(),
                        n.get_locus());

            n.replace(new_decrement);
        }
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::Preincrement& n)
    {
        walk(n.get_rhs());
        visit_pre_post_increment(n);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::Postincrement& n)
    {
        walk(n.get_rhs());
        visit_pre_post_increment(n.as<Nodecl::Preincrement>());
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::Predecrement& n)
    {
        walk(n.get_rhs());
        visit_pre_post_decrement(n);
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::Postdecrement& n)
    {
        walk(n.get_rhs());
        visit_pre_post_decrement(n.as<Nodecl::Predecrement>());
    }

    void VectorizerVisitorPreprocessor::visit(const Nodecl::ForStatement& n)
    {
        walk(n.get_loop_header());
        walk(n.get_statement());

        Nodecl::LoopControl loop_control = n.get_loop_header().
            as<Nodecl::LoopControl>();

        // OpenMP For
        // This works only for very simple conditions: IV < UB
        if (n == _environment._analysis_simd_scope)
        {
            // CONDITION
            // This works only for very simple conditions: IV < UB
            Nodecl::NodeclBase loop_condition = loop_control.get_cond();

            TL::ForStatement tl_for(n);
            Nodecl::NodeclBase ub = tl_for.get_upper_bound();

            Nodecl::NodeclBase result;

            // UB is normalized. < --> <=
            if (loop_condition.is<Nodecl::LowerThan>())
            {
                Nodecl::LowerOrEqualThan new_condition =
                    Nodecl::LowerOrEqualThan::make(
                            loop_condition.as<Nodecl::LowerThan>()
                            .get_lhs().shallow_copy(),
                            ub.shallow_copy(),
                            loop_condition.get_type(),
                            loop_condition.get_locus());

                loop_condition.replace(new_condition);

            }
            else if (loop_condition.is<Nodecl::LowerOrEqualThan>())
            {
                // Nothing to do. Already normalized
            }
            else
            {
                internal_error("Vectorizer: Unsupported condition at preprocessing OpenMP For", 0);
            }
        }
    }
}
}
