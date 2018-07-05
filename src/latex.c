#include "latex.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "escape.h"

static void escape_latex(hoedown_buffer *ob, const uint8_t *source, size_t length)
{
	hoedown_escape_latex(ob, source, length, 0);
}

static void escape_href(hoedown_buffer *ob, const uint8_t *source, size_t length)
{
	hoedown_escape_href(ob, source, length);
}

static int
span(hoedown_buffer *ob, const char *span, const hoedown_buffer *content, const hoedown_renderer_data *data, int escape)
{
  if (!content || !content->size || !span)
    return 0;

  hoedown_buffer_printf(ob, "\\%s{", span);
	if (escape)
		escape_latex(ob, content->data, content->size);
	else
		hoedown_buffer_put(ob, content->data, content->size);
  hoedown_buffer_putc(ob, '}');
  return 1;
}

static int
env(hoedown_buffer *ob, const char *env, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	hoedown_buffer_printf(ob, "\\begin{%s}\n", env);
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_printf(ob, "\\end{%s}\n", env);
  return 1;
}


/********************
 * GENERIC RENDERER *
 ********************/
static void
rndr_blockcode(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, const hoedown_renderer_data *data)
{
	char *type = (lang) ? "minted" : "verbatim";

	if (ob->size) hoedown_buffer_putc(ob, '\n');

	hoedown_buffer_printf(ob, "\\begin{%s}", type);
	if (lang) {
		hoedown_buffer_putc(ob, '{');
		escape_latex(ob, lang->data, lang->size);
		hoedown_buffer_putc(ob, '}');
	}
	hoedown_buffer_putc(ob, '\n');

	if (text) hoedown_buffer_put(ob, text->data, text->size);

	hoedown_buffer_printf(ob, "\\end{%s}\n", type);
}

static void
rndr_blockquote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	env(ob, "quotation", content, data);
}

static int
rndr_codespan(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	return span(ob, "texttt", text, data, 1);
}

static int
rndr_strikethrough(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "sout", content, data, 0);
}

static int
rndr_double_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "textbf", content, data, 0);
}

static int
rndr_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "textit", content, data, 0);
}

static int
rndr_underline(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "uline", content, data, 0);
}

static int
rndr_highlight(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "colorbox{yellow}", content, data, 0);
}

static int
rndr_quote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return env(ob, "quote", content, data);
}

static int
rndr_linebreak(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "\\\\\n");
	return 1;
}

static void
rndr_header(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data)
{
	hoedown_latex_renderer_state *state = data->opaque;
	if (ob->size) hoedown_buffer_putc(ob, '\n');

	switch (level) {
		case 1:
			HOEDOWN_BUFPUTSL(ob, "\\section{"); break;
		case 2:
			HOEDOWN_BUFPUTSL(ob, "\\subsection{"); break;
		case 3:
			HOEDOWN_BUFPUTSL(ob, "\\subsubsection{"); break;
		case 4:
			HOEDOWN_BUFPUTSL(ob, "\\paragraph{"); break;
		case 5:
			HOEDOWN_BUFPUTSL(ob, "\\subparagraph{"); break;
		default:
			hoedown_buffer_putc(ob, '{'); break;
	}

	if (state->flags & HOEDOWN_LATEX_HEADER_ID) {
		HOEDOWN_BUFPUTSL(ob, "\\label{");
		if (content) hoedown_buffer_put(ob, content->data, content->size);
		HOEDOWN_BUFPUTSL(ob, "}{");
	}
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	if (state->flags & HOEDOWN_LATEX_HEADER_ID)
		hoedown_buffer_putc(ob, '}');
	HOEDOWN_BUFPUTSL(ob, "}\n");
}

static int
rndr_link(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data)
{
	if (link && link->size > 0 && hoedown_buffer_prefix(link, "#") == 0) {
		HOEDOWN_BUFPUTSL(ob, "\\hyperref[");

		if (link && link->size)
			hoedown_buffer_put(ob, link->data + 1, link->size - 1);

		HOEDOWN_BUFPUTSL(ob, "]{");
	}
	else {
		HOEDOWN_BUFPUTSL(ob, "\\hyperref{");

		if (link && link->size)
			escape_href(ob, link->data, link->size);

		HOEDOWN_BUFPUTSL(ob, "}{}{");

		if (title && title->size)
			escape_latex(ob, title->data, title->size);

		HOEDOWN_BUFPUTSL(ob, "}{");
	}

	if (content && content->size) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_putc(ob, '}');
	return 1;
}

static void
rndr_list(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	char *type = (flags & HOEDOWN_LIST_ORDERED) ? "enumerate" : "itemize";
	env(ob, type, content, data);
}

static void
rndr_listitem(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "\\item ");
	if (content) {
		size_t size = content->size;
		while (size && content->data[size - 1] == '\n')
			size--;

		hoedown_buffer_put(ob, content->data, size);
		hoedown_buffer_putc(ob, '\n');
	}
}

static void
rndr_paragraph(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_latex_renderer_state *state = data->opaque;
	size_t i = 0;

	if (ob->size) hoedown_buffer_putc(ob, '\n');

	if (!content || !content->size)
		return;

	while (i < content->size && isspace(content->data[i])) i++;

	if (i == content->size)
		return;

	if (state->flags & HOEDOWN_LATEX_HARD_WRAP) {
		size_t org;
		while (i < content->size) {
			org = i;
			while (i < content->size && content->data[i] != '\n')
				i++;

			if (i > org)
				hoedown_buffer_put(ob, content->data + org, i - org);

			/*
			 * do not insert a line break if this newline
			 * is the last character on the paragraph
			 */
			if (i >= content->size - 1)
				break;

			rndr_linebreak(ob, data);
			i++;
		}
	} else {
		hoedown_buffer_put(ob, content->data + i, content->size - i);
	}
	hoedown_buffer_putc(ob, '\n');
}

static int
rndr_triple_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
  if (!content || !content->size) return 0;
  HOEDOWN_BUFPUTSL(ob, "\\textbf{\\textit{");
  hoedown_buffer_put(ob, content->data, content->size);
  HOEDOWN_BUFPUTSL(ob, "}}");
  return 1;
}

static void
rndr_hrule(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	  if (ob->size) hoedown_buffer_putc(ob, '\n');
		  HOEDOWN_BUFPUTSL(ob, "\\hrule\\vskip\\baselineskip\n");
}

static int
rndr_image(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data)
{
	if (!link || !link->size) return 0;

	HOEDOWN_BUFPUTSL(ob, "\\begin{figure*}\n\\includegraphics*[width=\\textwidth]{");
	escape_href(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "}\n");

	if (alt && alt->size) {
		HOEDOWN_BUFPUTSL(ob, "\\caption{");
		escape_latex(ob, alt->data, alt->size);
		HOEDOWN_BUFPUTSL(ob, "}\n"); }

	if (title && title->size) {
		escape_latex(ob, title->data, title->size);
		hoedown_buffer_putc(ob, '\n'); }

	HOEDOWN_BUFPUTSL(ob, "\\end{figure*}");
	return 1;
}

static int
rndr_raw_html(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	return 1;
}


static void
rndr_table(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data, size_t columns, hoedown_table_flags *col_data)
{
	size_t col;
	if (ob->size) hoedown_buffer_putc(ob, '\n');

	HOEDOWN_BUFPUTSL(ob, "\\begin{tabular}{|");
	for (col = 0; col < columns; ++col) {
		switch (col_data[col] & HOEDOWN_TABLE_ALIGNMASK) {
			case HOEDOWN_TABLE_ALIGN_CENTER:
				HOEDOWN_BUFPUTSL(ob, "c|"); break;
			case HOEDOWN_TABLE_ALIGN_LEFT:
				HOEDOWN_BUFPUTSL(ob, "l|"); break;
			case HOEDOWN_TABLE_ALIGN_RIGHT:
			default:
				HOEDOWN_BUFPUTSL(ob, "r|"); break;
		}
	}
	HOEDOWN_BUFPUTSL(ob, "}\\hline\n");

	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\end{tabular}\n");
}

static void
rndr_table_header(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, content->data, content->size);
}

static void
rndr_table_body(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, content->data, content->size);
}

static void
rndr_tablerow(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\\\\\hline\n");
}

static void
rndr_tablecell(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data, size_t col)
{
	if (col > 0) HOEDOWN_BUFPUTSL(ob, " & ");
	hoedown_buffer_put(ob, content->data, content->size);
}

static int
rndr_superscript(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	return span(ob, "textsuperscript", content, data, 0);
}

static void
rndr_normal_text(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (content)
		escape_latex(ob, content->data, content->size);
}

static void
rndr_footnotes(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	/* Intentionally left blank */
}

static int
rndr_footnote_ref(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data)
{
	return span(ob, "footnote", content, data, 0);
}

static int
rndr_math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "$");
	hoedown_buffer_put(ob, text->data, text->size);
	HOEDOWN_BUFPUTSL(ob, "$");
	return 1;
}

hoedown_renderer *
hoedown_latex_renderer_new(hoedown_latex_flags render_flags, int nesting_level)
{
	static const hoedown_renderer cb_default = {
		NULL,

		rndr_blockcode,
		rndr_blockquote,
		rndr_header,
		rndr_hrule,
		rndr_list,
		rndr_listitem,
		rndr_paragraph,
		rndr_table,
		rndr_table_header,
		rndr_table_body,
		rndr_tablerow,
		rndr_tablecell,
		rndr_footnotes,
		NULL, /* rndr_footnote_def, */ /* unnecessary for this renderer */
		NULL, /* rndr_raw_block,*/

		NULL, /* rndr_autolink,*/
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		rndr_underline,
		rndr_highlight,
		rndr_quote,
		rndr_image,
		rndr_linebreak,
		rndr_link,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,
		rndr_footnote_ref,
		rndr_math,
		rndr_raw_html,

		NULL,
		rndr_normal_text,

		NULL,
		NULL
	};

	hoedown_latex_renderer_state *state;
	hoedown_renderer *renderer;

	/* Prepare the state pointer */
	state = hoedown_malloc(sizeof(hoedown_latex_renderer_state));
	memset(state, 0x0, sizeof(hoedown_latex_renderer_state));

	state->flags = render_flags;
	state->toc_data.nesting_level = nesting_level;

	/* Prepare the renderer */
	renderer = hoedown_malloc(sizeof(hoedown_renderer));
	memcpy(renderer, &cb_default, sizeof(hoedown_renderer));

	renderer->blockhtml = NULL;

	renderer->opaque = state;
	return renderer;
}

void
hoedown_latex_renderer_free(hoedown_renderer *renderer)
{
	free(renderer->opaque);
	free(renderer);
}
