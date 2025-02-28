%{
/* Substitute the type names.  */
#define YYSTYPE EM_STYPE
#define YYLTYPE EM_LTYPE
/* Substitute the variable and function names.  */
#define yyparse em_parse
#define yylex	em_lex
#define yyerror em_error
#define yydebug em_debug
#define yynerrs em_nerrs

#include "data/list.h"
#include "data/locked.h"
#include "data/str.h"
#include "emblem-parser.h"
#include "logs/logs.h"
#include "parser.h"
#include "sanitise-word.h"
#include "sugar.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* #define YY_DECL int em_lex(EM_STYPE* yylval_param, EM_LTYPE* yylloc_param, yyscan_t yyscanner, ParserData* params) */

#define YY_USER_INIT do \
	{\
		BEGIN(INITIAL_WHITE);\
	} while (0)

#define YY_EXTRA_TYPE LexerData*
#define YY_USER_ACTION update_yylloc(yylloc, yytext);
static void update_yylloc(EM_LTYPE* loc, char* text);
static void make_header_call_str(Sugar* hdr, char* ytext, size_t yleng);
static void make_emph_str(Sugar* emph, char* ytext, size_t yleng);
static void handle_line_directive(YY_EXTRA_TYPE yextra, EM_LTYPE* yloc, char* ytext);

#if __GNUC__
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#endif

static void handle_llerror(EM_LTYPE* yyloc, LexerData* data, const char* err, ...);
#define llerror(...) handle_llerror(yylloc, yyextra, __VA_ARGS__)
static int indent_len(int tab_size, char* inp);

#define EMPHASIS(T_open, T_close)\
	if (yyextra->opening_emph)\
	{\
		make_emph_str(&yylval->sugar, yytext, yyleng);\
		return T_open;\
	}\
	yylval->len = yyleng;\
	return T_close;
%}

/* %option outfile="lex.yy.c" */
%option prefix="em_"
%option header-file="lex.em_.h"
%option 8bit
%option bison-bridge
%option bison-locations
%option debug
%option nodefault
%option noline
%option noyywrap
%option reentrant
%option yylineno
%option extra-type="LexerData*"
%pointer

EMPH_UNDERSCORE     "_""_"?
EMPH_ASTERISK       "*""*"?
EMPH_BACKTICK		"`"
EMPH_EQUALS			"=""="?
BLOCK_COMMENT_CLOSE "*/"
BLOCK_COMMENT_OPEN  "/*"
COLON				":"
DOUBLE_COLON		"::"
COMMENT_LINE		{WHITE_SPACE}*{LINE_COMMENT_START}.*{LN}
DIRECTIVE			"."[^ \t\r\n:{}]+
GROUP_CLOSE			"}"
GROUP_OPEN			"{"
HEADING				"#"{1,6}\*?
LINE_COMMENT_START	"//"
LINE_DIRECTIVE		":line \""("\\"[^\r\n]|[^\\"])*"\" "[0-9]+" "[0-9]*{LN}
LN					"\n"|"\r"|"\r\n"
SHEBANG				"#!".*{LN}
WHITE_SPACE			[ \t]
WORD_ESCAPE_CHAR	"\\"[^ \t\r\n]
WORD_START_CHAR		({WORD_ESCAPE_CHAR}|[^\\_*`=. \t\r\n{}])
WORD_MID_CHAR 		({WORD_ESCAPE_CHAR}|[^\\ \t\r\n{}])
WORD_EASY_END_CHAR  ({WORD_ESCAPE_CHAR}|[^\\_*`= \t\r\n{}])
WORD_END_REGULAR 	{WORD_EASY_END_CHAR}{2}
WORD_END_EMPH		[_*`=]({WORD_ESCAPE_CHAR}|[^\\ \t\r\n{}_*`=,.'":;])
WORD_END 			({WORD_ESCAPE_CHAR}|{WORD_END_REGULAR}|{WORD_END_EMPH})
WORD 				"."|{WORD_START_CHAR}({WORD_EASY_END_CHAR}|{WORD_MID_CHAR}*{WORD_END})?

%x COMMENT
%x INITIAL_WHITE
%x FILE_BODY

%%

	lex_start:
	if (yyextra->indent_lvl < yyextra->indent_lvl_target)
	{
		yyextra->indent_lvl++;
		return T_INDENT;
	}
	else if (yyextra->indent_lvl > yyextra->indent_lvl_target)
	{
		yyextra->indent_lvl--;
		return T_DEDENT;
	}
	else if (yyextra->par_break_required)
	{
		yyextra->par_break_required = false;
		return T_PAR_BREAK;
	}

<COMMENT>{BLOCK_COMMENT_OPEN}		{ yyextra->comment_lvl++; }
<COMMENT>{BLOCK_COMMENT_CLOSE}		{ yyextra->comment_lvl--; if (!yyextra->comment_lvl) { BEGIN(FILE_BODY); } }
<COMMENT>.|{LN}						;
<COMMENT><<EOF>>					{ llerror("Unexpected EOF in multi-line comment"); return EM_error; }

{BLOCK_COMMENT_OPEN}				{ yyextra->comment_lvl = 1; BEGIN(COMMENT); }
{BLOCK_COMMENT_CLOSE}				{ llerror("No comment to close"); return EM_error; }

<<EOF>>								{
										if (yyextra->indent_lvl)
										{
											yyextra->indent_lvl_target = 0;
											goto lex_start;
										}
										else
											return EM_EOF;
									}

<INITIAL_WHITE>{COMMENT_LINE}		;
<INITIAL_WHITE>{WHITE_SPACE}*{LN}	{ yyextra->par_break_required = true; }
<INITIAL_WHITE>{WHITE_SPACE}+		{
										yyextra->opening_emph = true;
										yyextra->indent_lvl_target = indent_len(yyextra->tab_size, yytext);
										BEGIN(INITIAL);
										goto lex_start;
									}
<INITIAL_WHITE>{LINE_DIRECTIVE}		{ handle_line_directive(yyextra, yylloc, yytext); }
<INITIAL_WHITE>.					{
										yyextra->opening_emph = true;
										yyless(0);
										yyextra->indent_lvl_target = 0;
										BEGIN(INITIAL);
										goto lex_start;
									}
<FILE_BODY>{WHITE_SPACE}+			{ yyextra->opening_emph = true; }
<FILE_BODY>{EMPH_UNDERSCORE}		{ EMPHASIS(T_UNDERSCORE_OPEN, T_UNDERSCORE_CLOSE); }
<FILE_BODY>{EMPH_ASTERISK}			{ EMPHASIS(T_ASTERISK_OPEN, T_ASTERISK_CLOSE); }
<FILE_BODY>{EMPH_BACKTICK}			{ EMPHASIS(T_BACKTICK_OPEN, T_BACKTICK_CLOSE); }
<FILE_BODY>{EMPH_EQUALS}			{ EMPHASIS(T_EQUALS_OPEN, T_EQUALS_CLOSE); }
<FILE_BODY>{COMMENT_LINE}			{ BEGIN(INITIAL_WHITE); return T_LN; }
<FILE_BODY>{LN}						{ BEGIN(INITIAL_WHITE); return T_LN; }
<FILE_BODY>{DIRECTIVE}				{ yylval->str = malloc(sizeof(Str)); make_strc(yylval->str, yytext + 1); return T_DIRECTIVE; }
<FILE_BODY>{DOUBLE_COLON}			{ yyextra->opening_emph = true; return T_DOUBLE_COLON; }
<FILE_BODY>{COLON}					{ yyextra->opening_emph = true; return T_COLON; }
<FILE_BODY>{GROUP_OPEN}				{ yyextra->opening_emph = true; return T_GROUP_OPEN; }
<FILE_BODY>{GROUP_CLOSE}			{ yyextra->opening_emph = false; return T_GROUP_CLOSE; }
<FILE_BODY>{HEADING}				{ make_header_call_str(&yylval->sugar, yytext, yyleng); return T_HEADING; }
<FILE_BODY>{WORD}					{ yyextra->opening_emph = false; yylval->str = malloc(sizeof(Str)); make_strr(yylval->str, sanitise_word(yylloc, yyextra->ifn, yytext, yyleng)); return T_WORD; }

<FILE_BODY>.						{ llerror("Unrecognised character '%c' (%#x)", yytext[0], yytext[0]); }

{SHEBANG}							;
{LN}								{ BEGIN(INITIAL_WHITE); }
. 									{ yyless(0); BEGIN(FILE_BODY); }

%%

static void handle_llerror(EM_LTYPE* yyloc, LexerData* data, const char* err, ...)
{
	++*data->nerrs;
	va_list va;
	va_start(va, err);
	Location loc = {
		.first_line = yyloc->first_line,
		.first_column = yyloc->first_column,
		.last_line = yyloc->last_line,
		.last_column = yyloc->last_column,
		.src_file = data->ifn,
	};
	vlog_err_at(&loc, err, va);
	va_end(va);
}

static int indent_len(int tab_size, char* inp)
{
	int ret = 0;
	char c;
	while ((c = *inp++))
		if (c == '\t')
			ret += tab_size;
		else if (c == ' ')
			ret++;
	return ceil(ret / tab_size);
}

static void update_yylloc(EM_LTYPE* loc, char* text)
{
	loc->first_line = loc->last_line;
	loc->first_column = loc->last_column;

	for(unsigned int i = 0; text[i] != '\0'; i++)
	{
		if(text[i] == '\r' || (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')))
		{
			loc->last_line++;
			loc->last_column = 0;
		}
		else
			loc->last_column++;
	}
}

static void make_emph_str(Sugar* emph, char* ytext, size_t yleng)
{
	Str* emph_call = malloc(sizeof(Str));
	switch (ytext[0])
	{
		case '_':
		case '*':
			make_strv(emph_call, yleng == 2 ? "bf" : "it");
			break;
		case '`':
			make_strv(emph_call, "tt");
			break;
		case '=':
			make_strv(emph_call, yleng == 2 ? "af" : "sc");
			break;
		default:
			log_err("Unknown emphasis delimiter '%s' (this is a bug, you shounldn't be seeing this!)", ytext);
			exit(1);
	}
	make_sugar(emph, emph_call, yleng);
}

static void make_header_call_str(Sugar* hdr, char* ytext, size_t yleng)
{
	// Assume that yleng <= 7
	bool star              = ytext[yleng - 1] == '*';
	char* hdr_raw          = malloc(3 + star);
	unsigned int hdr_depth = yleng - star;
	hdr_raw[0]             = 'h';
	hdr_raw[1]             = '0' + hdr_depth;
	hdr_raw[2]             = star ? '*' : '\0';
	if (star)
		hdr_raw[3] = '\0';

	Str* hdr_call = malloc(sizeof(Str));
	make_strr(hdr_call, hdr_raw);

	make_sugar(hdr, hdr_call, yleng);
}

static void handle_line_directive(YY_EXTRA_TYPE yextra, EM_LTYPE* yloc, char* ytext)
{
	// Extract file name
	char* fname = 1 + strchr(ytext, '"');
	char* fnameclosepos = strrchr(ytext, '"');
	*fnameclosepos = '\0';

	// Only record a new filename if it is not the same as the last (imperfect but checks quickly)
	if (!streq(yextra->ifn->str, fname))
	{
		Str* nifn = malloc(sizeof(Str));
		make_strc(nifn, fname);
		yextra->ifn = nifn;

		USE_LOCK(List* namesList, yextra->mtNamesList, append_list(namesList, nifn));
	}

	// Extract line and column number strings
	char* flnums = fnameclosepos + 2;
	char* flcnumgap = strchr(flnums, ' ');
	*flcnumgap = '\0';
	char* fcnums = flcnumgap + 1;

	// Assign locations
	yloc->first_line = atoi(flnums);
	yloc->first_column = atoi(fcnums);
	yloc->last_line = yloc->first_line;
	yloc->last_column = yloc->first_column;
}

#if __GNUC__
#	pragma GCC diagnostic pop
#endif
