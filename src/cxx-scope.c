#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gc.h>
#include "cxx-scope.h"
#include "cxx-buildscope.h"
#include "cxx-driver.h"
#include "cxx-typeutils.h"
#include "cxx-utils.h"
#include "cxx-solvetemplate.h"
#include "hash.h"

static scope_t* new_scope(void)
{
	scope_t* sc = GC_CALLOC(1, sizeof(*sc));
	sc->hash = hash_create(HASH_SIZE, HASHFUNC(prime_hash), KEYCMPFUNC(strcmp));

	return sc;
}

// Creates a new namespace scope, a new global scope is created by just
// passing a NULL enclosing namespace
scope_t* new_namespace_scope(scope_t* enclosing_scope)
{
	scope_t* result = new_scope();

	result->kind = NAMESPACE_SCOPE;
	result->contained_in = enclosing_scope;

	// Template scope gets inherited automatically
	if (enclosing_scope != NULL)
	{
		result->template_scope = enclosing_scope->template_scope;
	}

	return result;
}

// Creates a new prototype scope
scope_t* new_prototype_scope(scope_t* enclosing_scope)
{
	scope_t* result = new_scope();
	result->kind = PROTOTYPE_SCOPE;

	result->contained_in = enclosing_scope;

	result->template_scope = enclosing_scope->template_scope;

	return result;
}

// Creates a new block scope
scope_t* new_block_scope(scope_t* enclosing_scope, scope_t* prototype_scope, scope_t* function_scope)
{
	scope_t* result = new_scope();

	result->kind = BLOCK_SCOPE;
	result->prototype_scope = prototype_scope;
	result->function_scope = function_scope;
	result->contained_in = enclosing_scope;
	
	// Template scope gets inherited automatically
	result->template_scope = enclosing_scope->template_scope;

	// Create artificial entry for the block scope
	static int scope_number = 1000;
	char* c = GC_CALLOC(256, sizeof(char));
	sprintf(c, "(block scope #%05d)", scope_number);
	scope_number++;

	scope_entry_t* new_block_scope_entry = new_symbol(enclosing_scope, c);
	new_block_scope_entry->kind = SK_SCOPE;

	new_block_scope_entry->related_scope = result;
	
	return result;
}

// Creates a new function scope
scope_t* new_function_scope(scope_t* enclosing_scope, scope_t* prototype_scope)
{
	scope_t* result = new_scope();
	
	result->kind = FUNCTION_SCOPE;
	result->prototype_scope = prototype_scope;
	result->contained_in = enclosing_scope;
	
	// Template scope gets inherited automatically
	result->template_scope = enclosing_scope->template_scope;

	return result;
}

// Creates a new class scope
scope_t* new_class_scope(scope_t* enclosing_scope)
{
	scope_t* result = new_scope();

	result->kind = CLASS_SCOPE;

	result->contained_in = enclosing_scope;
	
	// Template scope gets inherited automatically
	result->template_scope = enclosing_scope->template_scope;

	return result;
}

scope_t* new_template_scope(scope_t* enclosing_scope)
{
	scope_t* result = new_scope();

	result->kind = TEMPLATE_SCOPE;
	result->contained_in = enclosing_scope;

	// This might sound odd but will enable the inheritance
	// result->template_scope = result;

	return result;
}

scope_entry_t* new_symbol(scope_t* sc, char* name)
{
	scope_entry_list_t* result_set = (scope_entry_list_t*) hash_get(sc->hash, name);

	scope_entry_t* result;

	result = GC_CALLOC(1, sizeof(*result));
	result->symbol_name = GC_STRDUP(name);
	result->scope = sc;

	if (result_set != NULL)
	{
		scope_entry_list_t* new_set = (scope_entry_list_t*) GC_CALLOC(1, sizeof(*new_set));

		// Put the new entry in front of the previous
		*new_set = *result_set;

		result_set->next = new_set;
		result_set->entry = result;
	}
	else
	{
		result_set = (scope_entry_list_t*) GC_CALLOC(1, sizeof(*result_set));
		result_set->entry = result;
		result_set->next = NULL; // redundant, though

		hash_put(sc->hash, name, result_set);
	}

	return result;
}

scope_entry_list_t* query_in_symbols_of_scope(scope_t* sc, char* name)
{
	fprintf(stderr, "Looking in symbols of scope %p -> '%s'...", sc, name);
	scope_entry_list_t* result = (scope_entry_list_t*) hash_get(sc->hash, name);

	if (result == NULL)
	{
		fprintf(stderr, "not found\n");
	}
	else
	{
		fprintf(stderr, "found\n");
	}

	return result;
}

/*
 * Insert entry in the scope
 */
void insert_entry(scope_t* sc, scope_entry_t* entry)
{
	if (entry->symbol_name == NULL)
	{
		internal_error("Inserting an symbol entry without name!", 0);
	}
	
	scope_entry_list_t* result_set = (scope_entry_list_t*) hash_get(sc->hash, entry->symbol_name);


	if (result_set != NULL)
	{
		scope_entry_list_t* new_set = (scope_entry_list_t*) GC_CALLOC(1, sizeof(*new_set));

		// Put the new entry in front of the previous
		*new_set = *result_set;

		result_set->next = new_set;
		result_set->entry = entry;
	}
	else
	{
		result_set = (scope_entry_list_t*) GC_CALLOC(1, sizeof(*result_set));
		result_set->entry = entry;
		result_set->next = NULL; // redundant, though

		hash_put(sc->hash, entry->symbol_name, result_set);
	}
}


/*
 * Returns the scope of this nested name specification
 */
scope_t* query_nested_name_spec(scope_t* sc, AST global_op, AST nested_name, scope_entry_list_t** result_entry_list)
{
	// We'll start the search in "sc"
	scope_t* lookup_scope = sc;
	scope_entry_list_t* entry_list = NULL;

	// unless we are told to start in the global scope
	if (global_op != NULL)
	{
		lookup_scope = compilation_options.global_scope;
	}

	// Traverse the qualification tree
	while (nested_name != NULL)
	{
		AST nested_name_spec = ASTSon0(nested_name);
		char seen_class = 0;

		switch (ASTType(nested_name_spec))
		{
			case AST_SYMBOL :
				{
					entry_list = query_unqualified_name(lookup_scope, ASTText(nested_name_spec));

					// If not found, null
					if (entry_list == NULL)
					{
						return NULL;
					}

					// Now filter for SK_CLASS or SK_NAMESPACE
					enum cxx_symbol_kind filter[3] = {SK_CLASS, SK_NAMESPACE, SK_TYPEDEF};
					entry_list = filter_symbol_kind_set(entry_list, 3, filter);

					if (entry_list == NULL)
					{
						return NULL;
					}

					scope_entry_t* entry = entry_list->entry;

					if (entry->kind == SK_TYPEDEF)
					{
						// Advance over typedefs
						type_t* aliased_type = entry->type_information;
						aliased_type = advance_over_typedefs(aliased_type);

						// Now get the entry_t*
						// If this is a typedef it can only be a typedef against a type
						// that should be a class
						if (aliased_type->kind != TK_DIRECT
								|| aliased_type->type->kind != STK_USER_DEFINED)
						{
							return NULL;
						}
						entry = aliased_type->type->user_defined_type;

						if (entry == NULL)
						{
							return NULL;
						}
					}

					// Classes do not have namespaces within them
					if (seen_class && entry->kind == SK_NAMESPACE)
					{
						return NULL;
					}

					// Once seen a class no more namespaces can appear
					if (entry->kind == SK_CLASS)
					{
						seen_class = 1;
					}

					// It looks fine, update the scope
					lookup_scope = entry->related_scope;
					break;
				}
			case AST_TEMPLATE_ID :
				 {
					 entry_list = query_template_id(nested_name_spec, sc, lookup_scope);
					 lookup_scope = entry_list->entry->related_scope;
					 seen_class = 1;
					 break;
				 }
			default:
				{
					internal_error("Unknown node '%s'\n", ast_print_node_type(ASTType(nested_name_spec)));
					break;
				}
		}

		nested_name = ASTSon1(nested_name);
	}

	if (result_entry_list != NULL)
	{
		*result_entry_list = entry_list;
	}

	return lookup_scope;
}

// Similar to query_nested_name_spec but searches the name
scope_entry_list_t* query_nested_name(scope_t* sc, AST global_op, AST nested_name, AST name, unqualified_lookup_behaviour_t unqualified_lookup)
{
	scope_entry_list_t* result = NULL;
	scope_t* lookup_scope;

	if (global_op == NULL && nested_name == NULL)
	{
		// This is an unqualified identifier
		switch (ASTType(name))
		{
			case AST_SYMBOL :
                switch (unqualified_lookup)
				{
					case FULL_UNQUALIFIED_LOOKUP :
						{
							result = query_unqualified_name(sc, ASTText(name));
							break;
						}
					case NOFULL_UNQUALIFIED_LOOKUP :
						{
							result = query_in_symbols_of_scope(sc, ASTText(name));
							break;
						}
					default :
						{
							internal_error("Invalid lookup behaviour", 0);
						}
				}
				break;
			case AST_TEMPLATE_ID:
				// ??? There should be a query_unqualified_template_id ?
				result = query_unqualified_template_id(name, sc, sc);
				break;
			default :
				internal_error("Unexpected node type '%s'\n", ast_print_node_type(ASTType(name)));
		}
	}
	else
	{
		if ((lookup_scope = query_nested_name_spec(sc, global_op, nested_name, NULL)) != NULL)
		{
			switch (ASTType(name))
			{
				case AST_SYMBOL :
					result = query_in_symbols_of_scope(lookup_scope, ASTText(name));
					break;
				case AST_TEMPLATE_ID:
					result = query_template_id(name, sc, lookup_scope);
					break;
				case AST_CONVERSION_FUNCTION_ID :
					{
						char* conversion_function_name = get_conversion_function_name(name, lookup_scope, NULL);
						result = query_in_symbols_of_scope(lookup_scope, conversion_function_name);
						break;
					}
				default :
					internal_error("Unexpected node type '%s'\n", ast_print_node_type(ASTType(name)));
			}
		}
	}

	return result;
}


static scope_entry_list_t* query_template_id_internal(AST template_id, scope_t* sc, scope_t* lookup_scope, 
		unqualified_lookup_behaviour_t unqualified_lookup)
{
	AST symbol = ASTSon0(template_id);
	fprintf(stderr, "Trying to resolve template '%s'\n", ASTText(symbol));

	scope_entry_list_t* entry_list; 
	
	if (unqualified_lookup == FULL_UNQUALIFIED_LOOKUP)
	{
		entry_list = query_unqualified_name(lookup_scope, ASTText(symbol));
	}
	else
	{
		entry_list = query_in_symbols_of_scope(lookup_scope, ASTText(symbol));
	}

	scope_entry_list_t* iter = entry_list;

	if (iter == NULL)
	{
		internal_error("Template not found!\n", 0);
	}

	// Look for specializations
	char has_specializations = 0;
	while (iter != NULL)
	{
		if (iter->entry->kind != SK_TEMPLATE_SPECIALIZED_CLASS
				&& iter->entry->kind != SK_TEMPLATE_PRIMARY_CLASS
				&& iter->entry->kind != SK_TEMPLATE_TEMPLATE_PARAMETER
				&& iter->entry->kind != SK_TEMPLATE_FUNCTION)
		{
			internal_error("Expecting a template symbol but symbol kind %d found (line = %d)\n", 
					iter->entry->kind, ASTLine(template_id));
		}

		has_specializations |= (iter->entry->kind == SK_TEMPLATE_SPECIALIZED_CLASS);

		iter = iter->next;
	}

	if (!has_specializations)
	{
		fprintf(stderr, "This template was not specialized. Choosing primary template\n");
		return entry_list;
	}
	else
	{
		// Get the template_arguments
		template_argument_list_t* current_template_arguments = NULL;

		build_scope_template_arguments(template_id, sc, &current_template_arguments);
		scope_entry_t* matched_template = solve_template(entry_list, current_template_arguments, sc);

		fprintf(stderr, "Selected template '%p'\n", matched_template);
		return create_list_from_entry(matched_template);
	}
}

scope_entry_list_t* query_unqualified_template_id(AST template_id, scope_t* sc, scope_t* lookup_scope)
{
	return query_template_id_internal(template_id, sc, lookup_scope, FULL_UNQUALIFIED_LOOKUP);
}

scope_entry_list_t* query_template_id(AST template_id, scope_t* sc, scope_t* lookup_scope)
{
	return query_template_id_internal(template_id, sc, lookup_scope, NOFULL_UNQUALIFIED_LOOKUP);
}

scope_entry_list_t* query_id_expression(scope_t* sc, AST id_expr, unqualified_lookup_behaviour_t unqualified_lookup)
{
	switch (ASTType(id_expr))
	{
		// Unqualified ones
		case AST_SYMBOL :
			{
				scope_entry_list_t* result = NULL;
                switch (unqualified_lookup)
				{
					case FULL_UNQUALIFIED_LOOKUP :
						{
							result = query_unqualified_name(sc, ASTText(id_expr));
							break;
						}
					case NOFULL_UNQUALIFIED_LOOKUP :
						{
							result = query_in_symbols_of_scope(sc, ASTText(id_expr));
							break;
						}
					default :
						{
							internal_error("Invalid lookup behaviour", 0);
						}
				}
				return result;
				break;
			}
		case AST_DESTRUCTOR_ID :
			{
				// An unqualified destructor name "~name"
				// 'name' should be a class in this scope
				AST symbol = ASTSon0(id_expr);
				scope_entry_list_t* result = query_in_symbols_of_scope(sc, ASTText(symbol));

				return result;

				break;
			}
		case AST_TEMPLATE_ID :
			{
				// An unqualified template_id "identifier<stuff>"
				internal_error("Unsupported template id", 0);
				break;
			}
		case AST_OPERATOR_FUNCTION_ID :
			{
				// An unqualified operator_function_id "operator +"
				char* operator_function_name = get_operator_function_name(id_expr);

				scope_entry_list_t* result = query_in_symbols_of_scope(sc, operator_function_name);

				return result;
				break;
			}
		case AST_CONVERSION_FUNCTION_ID :
			{
				// An unqualified conversion_function_id "operator T"
				// Why this has no qualified equivalent ?
				internal_error("Unsupported conversion function id", 0);
				break;
			}
			// Qualified ones
		case AST_QUALIFIED_ID :
			{
				// A qualified id "a::b::c"
				AST global_op = ASTSon0(id_expr);
				AST nested_name = ASTSon1(id_expr);
				AST symbol = ASTSon2(id_expr);

				scope_entry_list_t* result = query_nested_name(sc, global_op, nested_name, 
                        symbol, FULL_UNQUALIFIED_LOOKUP);

				return result;
				break;
			}
		case AST_QUALIFIED_TEMPLATE :
			{
				// A qualified template "a::b::template c" [?]
				internal_error("Unsupported qualified template", 0);
				break;
			}
		case AST_QUALIFIED_TEMPLATE_ID :
			{
				// A qualified template_id "a::b::c<int>"
				internal_error("Unsupported qualified template id", 0);
				break;
			}
		case AST_QUALIFIED_OPERATOR_FUNCTION_ID :
			{
				// A qualified operator function_id "a::b::operator +"
				internal_error("Unsupported qualified operator id", 0);
				break;
			}
		default :
			{
				internal_error("Unknown node '%s'\n", ast_print_node_type(ASTType(id_expr)));
				break;
			}
	}

	return NULL;
}

scope_entry_list_t* create_list_from_entry(scope_entry_t* entry)
{
	scope_entry_list_t* result = GC_CALLOC(1, sizeof(*result));
	result->entry = entry;
	result->next = NULL;

	return result;
}

static scope_entry_list_t* lookup_block_scope(scope_t* st, char* unqualified_name)
{
	// First check the scope
	scope_entry_list_t* result = NULL;
	fprintf(stderr, "Looking up '%s' in block...", unqualified_name);
	result = query_in_symbols_of_scope(st, unqualified_name);

	if (result != NULL)
	{
		return result;
	}

	// Search in the namespaces
	fprintf(stderr, "not found.\nLooking up '%s' in used namespaces...", unqualified_name);
	int i;
	for (i = 0; i < st->num_used_namespaces; i++)
	{
		result = query_in_symbols_of_scope(st->use_namespace[i], unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Search in the scope of labels
	if (st->function_scope != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in labels...", unqualified_name);
		result = query_in_symbols_of_scope(st->function_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}
	
	// Search in the scope of parameters
	if (st->prototype_scope != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->prototype_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise, if template scoping is available, check in the template scope
	if (st->template_scope != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in template parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->template_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

static scope_entry_list_t* lookup_prototype_scope(scope_t* st, char* unqualified_name)
{
	scope_entry_list_t* result = NULL;
	
	// Otherwise, if template scoping is available, check in the template scope
	if (st->template_scope != NULL)
	{
		fprintf(stderr, "Looking up '%s' in template parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->template_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

static scope_entry_list_t* lookup_namespace_scope(scope_t* st, char* unqualified_name)
{
	// First check the scope
	scope_entry_list_t* result = NULL;
	fprintf(stderr, "Looking up '%s' within namespace...", unqualified_name);
	result = query_in_symbols_of_scope(st, unqualified_name);

	if (result != NULL)
	{
		return result;
	}
	
	// Otherwise, if template scoping is available, check in the template scope
	if (st->template_scope != NULL)
	{
		fprintf(stderr, "Looking up '%s' in template parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->template_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Search in the namespaces
	fprintf(stderr, "not found.\nLooking up '%s' in used namespaces (%d)...", unqualified_name, st->num_used_namespaces);
	int i;
	for (i = 0; i < st->num_used_namespaces; i++)
	{
		result = query_in_symbols_of_scope(st->use_namespace[i], unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

static scope_entry_list_t* lookup_function_scope(scope_t* st, char* unqualified_name)
{
	// First check the scope
	scope_entry_list_t* result = NULL;
	fprintf(stderr, "Looking up '%s' in function scope...", unqualified_name);
	result = query_in_symbols_of_scope(st, unqualified_name);
	//
	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

static scope_entry_list_t* lookup_class_scope(scope_t* st, char* unqualified_name)
{
	// First check the scope
	scope_entry_list_t* result = NULL;
	fprintf(stderr, "Looking up '%s' in class...", unqualified_name);
	result = query_in_symbols_of_scope(st, unqualified_name);

	if (result != NULL)
	{
		return result;
	}

	// Search in the namespaces
	fprintf(stderr, "not found.\nLooking up '%s' in used namespaces...", unqualified_name);
	int i;
	for (i = 0; i < st->num_used_namespaces; i++)
	{
		result = query_in_symbols_of_scope(st->use_namespace[i], unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}
	
	// Search in the bases
	fprintf(stderr, "not found.\nLooking up '%s' in bases (%d)...", unqualified_name, st->num_base_scopes);
	for (i = 0; i < st->num_base_scopes; i++)
	{
		result = query_unqualified_name(st->base_scope[i], unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise, if template scoping is available, check in the template scope
	if (st->template_scope != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in template parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->template_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

static scope_entry_list_t* lookup_template_scope(scope_t* st, char* unqualified_name)
{
	// First check the scope
	scope_entry_list_t* result = NULL;
	fprintf(stderr, "Looking up '%s' in template scope...", unqualified_name);
	result = query_in_symbols_of_scope(st, unqualified_name);

	if (result != NULL)
	{
		return result;
	}

	// Otherwise, if template scoping is available, check in the template scope
	if (st->template_scope != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in template parameters...", unqualified_name);
		result = query_in_symbols_of_scope(st->template_scope, unqualified_name);
		if (result != NULL)
		{
			return result;
		}
	}

	// Otherwise try to find anything in the enclosing scope
	if (st->contained_in != NULL)
	{
		fprintf(stderr, "not found.\nLooking up '%s' in the enclosed scope...", unqualified_name);
		result = query_unqualified_name(st->contained_in, unqualified_name);
	}

	return result;
}

scope_entry_list_t* query_unqualified_name(scope_t* st, char* unqualified_name)
{
	static int nesting_level = 0;

	nesting_level++;

	scope_entry_list_t* result = NULL;

	switch (st->kind)
	{
		case PROTOTYPE_SCOPE :
			fprintf(stderr, "Starting lookup in prototype scope\n");
			result = lookup_prototype_scope(st, unqualified_name);
			break;
		case NAMESPACE_SCOPE :
			fprintf(stderr, "Starting lookup in namespace scope\n");
			result = lookup_namespace_scope(st, unqualified_name);
			break;
		case FUNCTION_SCOPE :
			fprintf(stderr, "Starting lookup in function scope\n");
			result = lookup_function_scope(st, unqualified_name);
			break;
		case BLOCK_SCOPE :
			fprintf(stderr, "Starting lookup in block scope\n");
			result = lookup_block_scope(st, unqualified_name);
			break;
		case CLASS_SCOPE :
			fprintf(stderr, "Starting lookup in class scope\n");
			result = lookup_class_scope(st, unqualified_name);
			break;
		case TEMPLATE_SCOPE :
			fprintf(stderr, "Starting lookup in template scope\n");
			result = lookup_template_scope(st, unqualified_name);
			break;
		case UNDEFINED_SCOPE :
		default :
			internal_error("Invalid scope kind=%d!\n", st->kind);
	}

	nesting_level--;

	if (nesting_level == 0)
	{
		if (result != NULL)
		{
			fprintf(stderr, "found\n");
		}
		else
		{
			fprintf(stderr, "not found.\n");
		}
	}

	return result;
}

// char incompatible_symbol_exists(scope_t* sc, AST id_expr, enum cxx_symbol_kind symbol_kind)
// {
// 	scope_entry_list_t* entry_list = query_id_expression(sc, id_expr);
// 	char found_incompatible = 0;
// 
// 	while (!found_incompatible && entry_list != NULL)
// 	{
// 		found_incompatible = (entry_list->entry->kind != symbol_kind);
// 
// 		entry_list = entry_list->next;
// 	}
// 
// 	return found_incompatible;
// }


scope_entry_list_t* filter_symbol_kind_set(scope_entry_list_t* entry_list, int num_kinds, enum cxx_symbol_kind* symbol_kind_set)
{
	scope_entry_list_t* result = NULL;
	scope_entry_list_t* iter = entry_list;
	
	while (iter != NULL)
	{
		int i;
		char found = 0;
		for (i = 0; (i < num_kinds) && !found; i++)
		{
			if (iter->entry->kind == symbol_kind_set[i])
			{
				scope_entry_list_t* new_item = GC_CALLOC(1, sizeof(*new_item));
				new_item->entry = iter->entry;
				new_item->next = result;
				result = new_item;
				found = 1;
			}
		}

		iter = iter->next;
	}

	return result;
}

scope_entry_list_t* filter_symbol_kind(scope_entry_list_t* entry_list, enum cxx_symbol_kind symbol_kind)
{
	scope_entry_list_t* result = NULL;

	result = filter_symbol_kind_set(entry_list, 1, &symbol_kind);

	return result;
}


scope_entry_list_t* filter_symbol_non_kind_set(scope_entry_list_t* entry_list, int num_kinds, enum cxx_symbol_kind* symbol_kind_set)
{
	scope_entry_list_t* result = NULL;
	scope_entry_list_t* iter = entry_list;
	
	while (iter != NULL)
	{
		int i;
		char found = 0;
		for (i = 0; (i < num_kinds) && !found; i++)
		{
			if (iter->entry->kind != symbol_kind_set[i])
			{
				scope_entry_list_t* new_item = GC_CALLOC(1, sizeof(*new_item));
				new_item->entry = iter->entry;
				new_item->next = result;
				result = new_item;
				found = 1;
			}
		}

		iter = iter->next;
	}

	return result;
}

scope_entry_list_t* filter_symbol_non_kind(scope_entry_list_t* entry_list, enum cxx_symbol_kind symbol_kind)
{
	scope_entry_list_t* result = NULL;

	result = filter_symbol_non_kind_set(entry_list, 1, &symbol_kind);

	return result;
}

/*
 * Returns a type if and only if this entry_list contains just one type
 * specifier. If another identifier is found it returns NULL
 */
scope_entry_t* filter_simple_type_specifier(scope_entry_list_t* entry_list)
{
	int non_type_name = 0;
	scope_entry_t* result = NULL;

	char seen_class_name = 0;
	char seen_function_name = 0;

	while (entry_list != NULL)
	{
		scope_entry_t* simple_type_entry = entry_list->entry;

		if (simple_type_entry->kind != SK_ENUM 
				&& simple_type_entry->kind != SK_CLASS 
				&& simple_type_entry->kind != SK_TYPEDEF 
				&& simple_type_entry->kind != SK_TEMPLATE_TYPE_PARAMETER
				&& simple_type_entry->kind != SK_TEMPLATE_TEMPLATE_PARAMETER
				&& simple_type_entry->kind != SK_TEMPLATE_PRIMARY_CLASS
				&& simple_type_entry->kind != SK_TEMPLATE_SPECIALIZED_CLASS
				&& simple_type_entry->kind != SK_GCC_BUILTIN_TYPE)
		{
			// Functions with name of a class are constructors and do not hide
			// the class symbol
			seen_function_name |= (simple_type_entry->kind == SK_FUNCTION);
			fprintf(stderr, "Found a '%d' that is non type\n", simple_type_entry->kind);
			non_type_name++;
		}
		else
		{
			seen_class_name |= simple_type_entry->kind == SK_CLASS;
			result = simple_type_entry;
		}

		entry_list = entry_list->next;
	}

	// There is something that is not a type name here and hides this simple type spec
	if (non_type_name != 0 
			&& !seen_class_name 
			&& !seen_function_name)
	{
		return NULL;
	}
	else
	{
		return result;
	}
}

scope_entry_list_t* append_scope_entry_lists(scope_entry_list_t* a, scope_entry_list_t* b)
{
	scope_entry_list_t* result = NULL;
	scope_entry_list_t* iter;

	iter = a;
	while (iter != NULL)
	{
		scope_entry_list_t* new_entry = GC_CALLOC(1, sizeof(*new_entry));

		new_entry->entry = iter->entry;
		new_entry->next = result;

		result = new_entry;

		iter = iter->next;
	}

	iter = b;
	while (iter != NULL)
	{
		scope_entry_list_t* new_entry = GC_CALLOC(1, sizeof(*new_entry));

		new_entry->entry = iter->entry;
		new_entry->next = result;

		result = new_entry;

		iter = iter->next;
	}

	return result;
}
