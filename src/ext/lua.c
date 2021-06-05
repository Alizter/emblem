#include "lua.h"

#include "logs/logs.h"
#include "lua-ast-io.h"
#include "style.h"
#include <lauxlib.h>
#include <lualib.h>
#include <stdbool.h>
#include <string.h>

static bool is_callable(ExtensionState* s, int idx);

void inc_iter_num(Doc* doc)
{
	doc->ext->iter_num++;
	lua_pushinteger(doc->ext->state, doc->ext->iter_num);
	lua_setglobal(doc->ext->state, EM_ITER_NUM_VAR_NAME);
}

int exec_lua_pass(Doc* doc) { return exec_lua_pass_on_node(doc->ext->state, doc->root); }

#define HANDLE_LUA_LIST_PASS(s, list)                                                                                  \
	{                                                                                                                  \
		ListIter li;                                                                                                   \
		make_list_iter(&li, list);                                                                                     \
		DocTreeNode* subNode;                                                                                          \
		int rc = 0;                                                                                                    \
		while (iter_list((void**)&subNode, &li))                                                                       \
		{                                                                                                              \
			rc |= exec_lua_pass_on_node(s, subNode);                                                                   \
			if (rc)                                                                                                    \
				return rc;                                                                                             \
		}                                                                                                              \
		dest_list_iter(&li);                                                                                           \
		return rc;                                                                                                     \
	}

#include "debug.h"
int exec_lua_pass_on_node(ExtensionState* s, DocTreeNode* node)
{
	log_debug("-asdf-");
	dumpstack(s);
	log_debug("-asdf-");
	switch (node->content->type)
	{
		case WORD:
			return 0;
		case CALL:
		{
			// Remove old result if present
			if (node->content->call_params->result)
				dest_free_doc_tree_node(node->content->call_params->result, true);

			lua_getglobal(s, EM_PUBLIC_TABLE);
			lua_getfield(s, -1, node->name->str);
			if (lua_isnoneornil(s, -1))
			{
				node->flags |= CALL_HAS_NO_EXT_FUNC;
				if (is_empty_list(node->content->call_params->args))
				{
					int rc = log_warn_at(node->src_loc,
						"Directive '.%s' is not an extension function and has no arguments (would style nothing)",
						node->name->str);
					lua_pop(s, -1); // Remove call function
					return rc ? -1 : 0;
				}
				if (node->content->call_params->args->cnt == 1)
				{
					node->content->call_params->result = node->content->call_params->args->fst->data;
					lua_pop(s, -1); // Remove call function
					return 0;
				}

				log_debug("Putting args into lines node for result");
				DocTreeNode* linesNode = malloc(sizeof(DocTreeNode));
				make_doc_tree_node_lines(linesNode, dup_loc(node->src_loc));
				linesNode->flags |= IS_GENERATED_NODE;
				ListIter li;
				make_list_iter(&li, node->content->call_params->args);
				DocTreeNode* currArg;
				while (iter_list((void**)&currArg, &li))
					prepend_doc_tree_node_child(linesNode, linesNode->content->lines, currArg);
				node->content->call_params->result = linesNode;
				lua_pop(s, -1); // Remove call function
				return 0;
			}
			if (!is_callable(s, -1))
			{
				log_err("Expected function or callable table at em.%s, but got a %s", node->name->str, luaL_typename(s, -1));
				node->flags |= CALL_HAS_NO_EXT_FUNC;
				lua_pop(s, -1); // Remove call function
				return -1;
			}

			// Prepare arguments
			const int num_args = node->content->call_params->args->cnt;
			ListIter li;
			make_list_iter(&li, node->content->call_params->args);
			DocTreeNode* argNode;
			LuaPointer argPtrs[num_args];
			int i = 0;
			while (iter_list((void**)&argNode, &li))
			{
				make_lua_pointer(&argPtrs[i], AST_NODE, argNode);
				lua_pushlightuserdata(s, &argPtrs[i]);
				i++;
			}

			log_debug("Stack:");
			dumpstack(s);
			log_debug("Calling %s...", node->name->str);
			log_debug("(Pcalling %s with %d arguments...)", node->name->str, num_args);
			switch (lua_pcall(s, num_args, 1, 0))
			{
				case LUA_OK:
					log_debug("returned: %s", luaL_typename(s, -1));
					return unpack_lua_result(&node->content->call_params->result, s, node);
				case LUA_YIELD:
				{
					int fw
						= log_warn_at(node->src_loc, "Lua function em.%s yielded instead of returned", node->name->str);
					return fw ? -1 : 0;
				}
				default:
					log_err_at(
						node->src_loc, "Calling em.%s failed with error: %s", node->name->str, lua_tostring(s, -1));
					return -1;
			}
		}
		case LINE:
			HANDLE_LUA_LIST_PASS(s, node->content->line);
		case LINES:
			HANDLE_LUA_LIST_PASS(s, node->content->lines);
		case PAR:
			HANDLE_LUA_LIST_PASS(s, node->content->par);
		case PARS:
			HANDLE_LUA_LIST_PASS(s, node->content->pars);
		default:
			log_err("Failed to perform lua pass, encountered node of unknown type %d", node->content->type);
			return -1;
	}
}

static bool is_callable(ExtensionState* s, int idx)
{
	if (lua_isfunction(s, idx))
		return true;

	if (!lua_istable(s, idx))
		return false;

	if (!lua_getmetatable(s, idx))
		return false;

	lua_getfield(s, -1, "__call");
	bool callable = lua_isfunction(s, -1);
	lua_pop(s, -1);

	return callable;
}
