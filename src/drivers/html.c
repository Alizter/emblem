#include "html.h"

#include "data/cmp.h"
#include "data/hash.h"
#include "data/list.h"
#include "data/map.h"
#include "data/str.h"
#include "driver-util.h"
#include "linear-formatter.h"
#include "logs/logs.h"
#include "write-out.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>

static Pair const html_special_functions[] = {
	{ "p", "p" },
	{ "h1", "h1" },
	{ "h2", "h2" },
	{ "h3", "h3" },
	{ "h4", "h4" },
	{ "h5", "h5" },
	{ "h6", "h6" },
	{ "h1*", "h1" },
	{ "h2*", "h2" },
	{ "h3*", "h3" },
	{ "h4*", "h4" },
	{ "h5*", "h5" },
	{ "h6*", "h6" },
};
static const size_t num_html_special_functions = sizeof(html_special_functions) / sizeof(*html_special_functions);

static int driver_runner(Doc* doc, DriverParams* params);
static int output_stylesheet(LinearFormatter* css_formatter, Str* time_str);
static int format_doc_as_html(LinearFormatter* formatter, Str* time_str, Doc* doc);
static int format_node_as_html(LinearFormatter* formatter, DocTreeNode* node);

#define HTML_HEADER                                                                                                    \
	"<!DOCTYPE html>\n"                                                                                                \
	"<!-- This file was generated by `em` on %s. -->\n"                                                                \
	"<!-- Any changes will be overwritten next time typesetting is run -->\n"
#define STYLESHEET_HEADER                                                                                              \
	"/*\n"                                                                                                             \
	" * This file was generated by `em` on %s.\n"                                                                      \
	" * Any changes will be overwritten next time typesetting is run.\n"                                               \
	" */\n"
#define STYLESHEET_LINK				  "<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\">"
#define TITLE_DEF					  "<title>%s</title>"
#define HTML_DOCUMENT_OUTPUT_NAME_FMT "%s.html"
#define CSS_DOCUMENT_OUTPUT_NAME_FMT  "%s.css"

int make_html_driver(InternalDriver* driver)
{
	OutputDriverInf* driver_inf = malloc(sizeof(OutputDriverInf));
	driver_inf->support			= TS_BASIC_STYLING,

	driver->name = "html";
	driver->inf	 = driver_inf;
	driver->run	 = driver_runner;

	return 0;
}

static int driver_runner(Doc* doc, DriverParams* params)
{
	int rc;
	LinearFormatter formatter;
	Str document_output_name_fmt;
	make_strv(&document_output_name_fmt, HTML_DOCUMENT_OUTPUT_NAME_FMT);
	make_linear_formatter(
		&formatter, params, num_html_special_functions, html_special_functions, &document_output_name_fmt);
	Str time_str;
	get_time_str(&time_str);

	// Reformat the document and output it
	rc = format_doc_as_html(&formatter, &time_str, doc);
	if (rc)
		return rc;
	rc = write_linear_formatter_output(&formatter, true);
	if (rc)
		return rc;

	// Output the style file
	Str stylesheet_output_name_fmt;
	make_strv(&stylesheet_output_name_fmt, CSS_DOCUMENT_OUTPUT_NAME_FMT);
	LinearFormatter css_formatter;
	make_linear_formatter(&css_formatter, params, 0, NULL, &stylesheet_output_name_fmt);
	concat_linear_formatter_content(&css_formatter, doc->styler->snippets);
	rc = output_stylesheet(&css_formatter, &time_str);
	if (rc)
		return rc;

	dest_linear_formatter(&css_formatter);
	dest_str(&time_str);
	dest_linear_formatter(&formatter);
	return 0;
}

static int format_doc_as_html(LinearFormatter* formatter, Str* time_str, Doc* doc)
{
	append_linear_formatter_strf(formatter, HTML_HEADER, time_str->str);
	append_linear_formatter_raw(formatter, "<html>");
	append_linear_formatter_raw(formatter, "\n<head>\n");
	append_linear_formatter_strf(formatter, STYLESHEET_LINK, formatter->stylesheet_name->str);
	append_linear_formatter_strf(formatter, TITLE_DEF, formatter->output_name_stem->str);
	/* append_linear_formatter_raw(formatter, "<meta name=\"theme-color\" content=\"#2d2d2d\"/>"); */
	append_linear_formatter_raw(formatter, "\n</head>");
	append_linear_formatter_raw(formatter, "\n<body>\n");
	int rc = format_node_as_html(formatter, doc->root);
	if (rc)
		return rc;
	append_linear_formatter_raw(formatter, "\n</body>");
	append_linear_formatter_raw(formatter, "\n</html>");

	return 0;
}

static int format_node_as_html(LinearFormatter* formatter, DocTreeNode* node)
{
	switch (node->content->type)
	{
		case WORD:
		{
			append_linear_formatter_raw(formatter, " ");
			append_linear_formatter_raw(formatter, node->content->word->str);
			return 0;
		}
		case CALL:
		{
			Str* html_node_name;
			Maybe m;
			get_call_name_map(&m, formatter, node->name);
			switch (m.type)
			{
				case JUST:
					html_node_name = m.just;
					break;
				case NOTHING:
					html_node_name = malloc(sizeof(Str));
					make_strv(html_node_name, "span");
					assign_ownership_to_formatter(formatter, html_node_name);
					break;
				default:
					log_err("Maybe object has unknown data constructor: %d", m.type);
					return 1;
			}
			int rc = 0;
			if (node->content->call->result)
			{
				append_linear_formatter_strf(formatter, "<%s class=\"%s\">", html_node_name->str, node->name->str);
				rc = format_node_as_html(formatter, node->content->call->result);
				append_linear_formatter_strf(formatter, "</%s>", html_node_name->str);
			}
			dest_maybe(&m, NULL);
			return rc;
		}
		case CONTENT:
		{
			int rc = 0;

			ListIter li;
			make_list_iter(&li, node->content->content);
			DocTreeNode* node;
			while (iter_list((void**)&node, &li))
			{
				rc = format_node_as_html(formatter, node);
				if (rc)
					break;
			}
			dest_list_iter(&li);
			return rc;
		}
		default:
			log_err("Unknown node content type: %d", node->content->type);
			return 1;
	}
}

static int output_stylesheet(LinearFormatter* css_formatter, Str* time_str)
{
	log_info("Preparing stylesheet for output");

	// Add the header to the css
	size_t header_len	 = 1 + snprintf(NULL, 0, STYLESHEET_HEADER, time_str->str);
	char* header_content = malloc(header_len);
	snprintf(header_content, header_len, STYLESHEET_HEADER, time_str->str);
	Str* header_str = malloc(sizeof(Str));
	make_strr(header_str, header_content);
	prepend_linear_formatter_str(css_formatter, header_str);

	return write_linear_formatter_output(css_formatter, false);
}
