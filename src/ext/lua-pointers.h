#pragma once

#include "data/destructor.h"

typedef enum
{
	AST_NODE,
	STYLER,
	EXT_ENV,
} LuaPointerType;

typedef struct
{
	LuaPointerType type;
	void* data;
} LuaPointer;

void make_lua_pointer(LuaPointer* pointer, LuaPointerType type, void* data);
void dest_lua_pointer(LuaPointer* pointer, Destructor ed);
