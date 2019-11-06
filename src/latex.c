#include "latex.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "charter/src/parser.h"
#include "charter/src/renderer.h"

#include "escape.h"

#define MAX_FILE_SIZE 1000000

scidown_render_tag
scidown_latex_is_tag(const uint8_t *data, size_t size, const char *tagname)
{
	size_t i;
	int closed = 0;

	if (size < 3 || data[0] != '<')
		return SCIDOWN_RENDER_TAG_NONE;

	i = 1;

	if (data[i] == '/') {
		closed = 1;
		i++;
	}

	for (; i < size; ++i, ++tagname) {
		if (*tagname == 0)
			break;

		if (data[i] != *tagname)
			return SCIDOWN_RENDER_TAG_NONE;
	}

	if (i == size)
		return SCIDOWN_RENDER_TAG_NONE;

	if (isspace(data[i]) || data[i] == '>')
		return closed ? SCIDOWN_RENDER_TAG_CLOSE : SCIDOWN_RENDER_TAG_OPEN;

	return SCIDOWN_RENDER_TAG_NONE;
}


static void escape_href(hoedown_buffer *ob, const uint8_t *source, size_t length)
{
	hoedown_escape_href(ob, source, length);
}

/********************
 * GENERIC RENDERER *
 ********************/
static int
rndr_autolink(hoedown_buffer *ob, const hoedown_buffer *link, hoedown_autolink_type type, const hoedown_renderer_data *data)
{
	scidown_latex_renderer_state *state = data->opaque;

	if (!link || !link->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\\href{");
	if (type == HOEDOWN_AUTOLINK_EMAIL)
		HOEDOWN_BUFPUTSL(ob, "mailto:");
	escape_href(ob, link->data, link->size);

	if (state->link_attributes) {
		hoedown_buffer_putc(ob, '\"');
		state->link_attributes(ob, link, data);
		hoedown_buffer_puts(ob, "}{");
	} else {
		HOEDOWN_BUFPUTSL(ob, "\"}{");
	}

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */
	if (hoedown_buffer_prefix(link, "mailto:") == 0) {
		hoedown_buffer_put(ob, link->data + 7, link->size - 7);
	} else {
		hoedown_buffer_put(ob, link->data, link->size);
	}

	HOEDOWN_BUFPUTSL(ob, "}");

	return 1;
}

static void
rndr_blockcode(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');


    scidown_latex_renderer_state *state = data->opaque;
	if (lang && (state->flags & SCIDOWN_RENDER_CHARTER) != 0 && hoedown_buffer_eqs(lang, "charter") != 0){
		if (text){

			char * copy = malloc((text->size + 1)*sizeof(char));
			memset(copy, 0, text->size+1);
			memcpy(copy, text->data, text->size);

			chart * c =  parse_chart(copy);
			char * tex = chart_to_latex(c);

			int n = strlen(tex);
			hoedown_buffer_printf(ob, tex, n);

			free(copy);
			chart_free(c);
			free(tex);
		}
		return;
	}

	if (lang) {
		HOEDOWN_BUFPUTSL(ob, "\\begin{lstlisting}[language=");
		hoedown_buffer_put(ob, lang->data, lang->size);
		HOEDOWN_BUFPUTSL(ob, "]\n");
	} else {
		HOEDOWN_BUFPUTSL(ob, "\\begin{lstlisting}\n");
	}

	if (text)
		hoedown_buffer_put(ob, text->data, text->size);

	HOEDOWN_BUFPUTSL(ob, "\\end{lstlisting}\n");
}

static void
rndr_blockquote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "\\begin{quote}\n");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\\end{quote}\n");
}

static int
rndr_codespan(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "\\texttt{");
	if (text) hoedown_buffer_put(ob, text->data, text->size);
	HOEDOWN_BUFPUTSL(ob, "}");
	return 1;
}

static int
rndr_strikethrough(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\\st{");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");
	return 1;
}

static int
rndr_double_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "{\\bf ");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");

	return 1;
}

static int
rndr_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "{\\em ");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");
	return 1;
}

static int
rndr_underline(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\\underline{");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");

	return 1;
}

static int
rndr_highlight(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\\hl{");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");

	return 1;
}

static int
rndr_quote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "\"");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "\"");

	return 1;
}

static int
rndr_linebreak(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	hoedown_buffer_puts(ob, "\n");
	return 1;
}

static void
rndr_header(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data, h_counter counter, int numbering)
{

	if (data->meta->doc_class == CLASS_BOOK || data->meta->doc_class == CLASS_REPORT) {
		level --;
	}

	if (ob->size)
		hoedown_buffer_putc(ob, '\n');

	if (!content)
	  return;

  	if (level == 0) {
	  hoedown_buffer_puts(ob, "\\chapter{");
	} else if (level == 1) {
		  if (data->meta->doc_class == CLASS_BEAMER)
	  	hoedown_buffer_puts(ob, "\\frametitle{");
	  else
  	    hoedown_buffer_puts(ob, "\\section{");
	} else if (level == 2) {
	  hoedown_buffer_puts(ob, "\\subsection{");
	} else if (level == 3) {
 	  hoedown_buffer_puts(ob, "\\subsubsection{");
	} else if (level == 4) {
	  hoedown_buffer_puts(ob, "\\paragraph{");
	} else if (level == 5) {
	  hoedown_buffer_puts(ob, "\\subparagraph{");
	}

	hoedown_buffer_put(ob, content->data, content->size);
    hoedown_buffer_puts(ob, "}\n");
}

static int
rndr_link(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data)
{
	scidown_latex_renderer_state *state = data->opaque;

	HOEDOWN_BUFPUTSL(ob, "\\href{");

	if (link && link->size)
		escape_href(ob, link->data, link->size);

	if (title && title->size) {
		HOEDOWN_BUFPUTSL(ob, "\" title=\"");
		hoedown_buffer_put(ob, title->data, title->size);
	}

	if (state->link_attributes) {
		hoedown_buffer_putc(ob, '\"');
		state->link_attributes(ob, link, data);
		hoedown_buffer_puts(ob, "}{");
	} else {
		HOEDOWN_BUFPUTSL(ob, "\"}{");
	}

	if (content && content->size) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");
	return 1;
}

static void
rndr_list(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	hoedown_buffer_puts(ob, (flags & HOEDOWN_LIST_ORDERED ? "\\begin{enumerate}" : "\\begin{itemize}"));
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_puts(ob, (flags & HOEDOWN_LIST_ORDERED ? "\\end{enumerate}\n" : "\\end{itemize}\n"));
}

static void
rndr_listitem(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "\n\\item ");
	if (content) {
		size_t size = content->size;
		while (size && content->data[size - 1] == '\n')
			size--;

		hoedown_buffer_put(ob, content->data, size);
	}
}

static void
rndr_paragraph(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	scidown_latex_renderer_state *state = data->opaque;
	size_t i = 0;

	if (ob->size) hoedown_buffer_puts(ob, "\n");

	if (!content || !content->size)
		return;

	while (i < content->size && isspace(content->data[i])) i++;

	if (i == content->size)
		return;

	if (state->flags) {
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
	HOEDOWN_BUFPUTSL(ob, "\n\n");
}

static void
rndr_raw_block(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	size_t org, sz;

	if (!text)
		return;

	/* FIXME: Do we *really* need to trim the HTML? How does that make a difference? */
	sz = text->size;
	while (sz > 0 && text->data[sz - 1] == '\n')
		sz--;

	org = 0;
	while (org < sz && text->data[org] == '\n')
		org++;

	if (org >= sz)
		return;

	if (ob->size)
		hoedown_buffer_putc(ob, '\n');

	hoedown_buffer_put(ob, text->data + org, sz - org);
	hoedown_buffer_putc(ob, '\n');
}

static int
rndr_triple_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "{\\bf{\\em ");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}}");
	return 1;
}

static void
rndr_hrule(hoedown_buffer *ob, const hoedown_renderer_data *data)
{

	if (ob->size) hoedown_buffer_putc(ob, '\n');
	if (data->meta->doc_class == CLASS_BEAMER) {
		hoedown_buffer_puts(ob, "\\end{frame}\n");
		hoedown_buffer_puts(ob, "\\begin{frame}\n");
	} else {
		hoedown_buffer_puts(ob, "\\rule{\\linewidth}{.1pt}\n");
	}
}

static int
rndr_image(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data)
{
	/*scidown_latex_renderer_state *state = data->opaque;*/
	if (!link || !link->size) return 0;


	HOEDOWN_BUFPUTSL(ob, "\\includegraphics[width=\\linewidth]{");
	escape_href(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "}");

	return 1;
}

static int
rndr_raw_html(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	scidown_latex_renderer_state *state = data->opaque;

	/* ESCAPE overrides SKIP_HTML. It doesn't look to see if
	 * there are any valid tags, just escapes all of them. */
	if((state->flags) != 0) {
		hoedown_buffer_put(ob, text->data, text->size);
		return 1;
	}

	if ((state->flags) != 0)
		return 1;

	hoedown_buffer_put(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data,  hoedown_table_flags *flags, int cols)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "\\begin{tabular}{");
	int i;
	for (i = 0;i < cols;i++)
	{
		hoedown_buffer_puts(ob, " | ");
		switch(flags[i]  & HOEDOWN_TABLE_ALIGNMASK)
		{
		case HOEDOWN_TABLE_ALIGN_RIGHT:
			hoedown_buffer_putc(ob, 'r');
			break;
		case HOEDOWN_TABLE_ALIGN_CENTER:
			hoedown_buffer_putc(ob, 'c');
			break;
		default:
			hoedown_buffer_putc(ob, 'l');
		}
	}

	hoedown_buffer_puts(ob, " | }\n\\hline\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "\\hline\n\\end{tabular}\n");

}

static void
rndr_table_header(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	/* TODO implement */
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "\\hline\n");
}

static void
rndr_table_body(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	/* TODO implement */
    hoedown_buffer_put(ob, content->data, content->size);
}

static void
rndr_tablerow(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	/* TODO implement */

	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_replace_last(ob, "\\\\\n");
}

static void
rndr_tablecell(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data)
{
	/* TODO implement */
	if (flags & HOEDOWN_TABLE_HEADER) {
		HOEDOWN_BUFPUTSL(ob, "{\\bf ");
	}

	if (content)
		hoedown_buffer_put(ob, content->data, content->size);
	if (flags & HOEDOWN_TABLE_HEADER) {
		hoedown_buffer_putc(ob, '}');
	}

	HOEDOWN_BUFPUTSL(ob, " & ");
}

static int
rndr_superscript(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "\\textsuperscript{");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "}");
	return 1;
}

static void
rndr_normal_text(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (content)
		hoedown_buffer_put(ob, content->data, content->size);
}

static void
rndr_footnotes(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	/*TOOD fix that*/
	/*scidown_latex_renderer_state *state = data->opaque;*/
	hoedown_buffer_puts(ob, "\\begin{thebibliography}{00}\n");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_puts(ob, "\\end{thebibliography}\n");
}

static void
rndr_footnote_def(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data)
{
	/*TODO fix that*/
	if (!content)
		return;

	char * tmp = malloc(content->size+1);
	tmp[content->size] = 0;
	memcpy(tmp, content->data, content->size);
	hoedown_buffer_printf(ob, "\\bibitem{fnref:%d}%s\n", num, tmp);
	free(tmp);
}

static int
rndr_footnote_ref(hoedown_buffer *ob, int num, const hoedown_renderer_data *data)
{
	if (num > 0)
		hoedown_buffer_printf(ob, "\\cite{fnref:%d}", num);

	return 1;
}

static int
rndr_math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\[" : "\\("), 2);

	hoedown_buffer_put(ob, text->data, text->size);
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\]" : "\\)"), 2);
	return 1;
}

static int
rndr_eq_math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, text->data+1, text->size-1);
	return 1;
}

static void
rndr_head(hoedown_buffer *ob, metadata * doc_meta, ext_definition * extension)
{
	hoedown_buffer_printf(ob, "\\documentclass[%s, %dpt]{%s}\n",
	                      paper_to_latex(doc_meta->paper_size),
	                      doc_meta->font_size,
	                      class_to_string(doc_meta->doc_class));
	hoedown_buffer_puts(ob, "\\usepackage[utf8]{inputenc}\n"
		                    "\\usepackage{cite}\n"
		                    "\\usepackage{amsmath,amssymb,amsfonts}\n"
		                    "\\usepackage{algorithmic}\n"
		                    "\\usepackage{float}\n"
		                    "\\usepackage{hyperref}\n"
		                    "\\usepackage{graphicx}\n"
		                    "\\usepackage{textcomp}\n"
		                    "\\usepackage{listings}\n"
		                    "\\usepackage{epsfig}\n"
		                    "\\usepackage{tikz}\n"
		                    "\\usepackage{pgfplots}\n\n"
		                    "\\pgfplotsset{compat=1.15}\n\n"
		                    "\\providecommand{\\keywords}[1]{{\\bf{\\em Index terms---}} #1}\n"
							"\\newfloat{program}{thp}{lop}\n\\floatname{program}{Listing}\n"
						);

	if (doc_meta->title){
		hoedown_buffer_printf(ob, "\\title{%s}\n\\date{}\n", doc_meta->title);
	}

	if (doc_meta->authors)
	{
		hoedown_buffer_puts(ob, "\\author{");
		Strings* it;
		for (it = doc_meta->authors; it != NULL; it = it->next){
			if (it->size == 1) {
				hoedown_buffer_puts(ob, it->str);
			} else {
				hoedown_buffer_printf(ob, "%s \\and ", it->str);
			}
		}
		hoedown_buffer_puts(ob, "}\n");
	}

	if (extension && extension->extra_header)
	{
		hoedown_buffer_puts(ob, extension->extra_header);
	}

	hoedown_buffer_puts(ob,"\n\\begin{document}\n");
}

static void
rndr_title(hoedown_buffer *ob, const hoedown_buffer *content, const metadata *data)
{
	if (data->doc_class == CLASS_BEAMER) {
		hoedown_buffer_puts(ob, "\\frame{\\titlepage}");
	} else {
		hoedown_buffer_puts(ob, "\\maketitle");
	}
}

static void
rndr_authors(hoedown_buffer *ob, Strings *authors)
{
}

static void
rndr_affiliation(hoedown_buffer *ob, const hoedown_buffer *content,  const hoedown_renderer_data *data)
{

}

static void
rndr_keywords(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_buffer_puts(ob, "\\keywords{");
	hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_puts(ob, "}\n");
}

static void
rndr_begin(hoedown_buffer *ob, const hoedown_renderer_data *data)
{

}

static void
rndr_inner(hoedown_buffer *ob , const hoedown_renderer_data *data)
{
	if (data->meta->doc_class == CLASS_BEAMER)
		hoedown_buffer_puts(ob, "\\begin{frame}\n");
}

static void
rndr_end(hoedown_buffer *ob, ext_definition * extension, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');

	if (data->meta->doc_class == CLASS_BEAMER) {
		hoedown_buffer_puts(ob, "\\end{frame}\n");
	}

	if (extension && extension->extra_closing)
	{
		hoedown_buffer_puts(ob, extension->extra_closing);
	}
	hoedown_buffer_puts(ob, "\\end{document}\n");
}

static void
rndr_pagebreak(hoedown_buffer *ob)
{
	hoedown_buffer_puts(ob, "\\newpage\n");
}


static void
rndr_abstract(hoedown_buffer *ob){
	hoedown_buffer_puts(ob, "\\begin{abstract}\n");
}

static void
rndr_close(hoedown_buffer *ob){
	hoedown_buffer_puts(ob, "\\end{abstract}");
}

static int rndr_ref (hoedown_buffer *ob, char * id, int count)
{
	hoedown_buffer_printf(ob, "(\\ref{%s})", id);
	return 1;
}

static void rndr_open_equation(hoedown_buffer *ob, const char * ref, const hoedown_renderer_data *data)
{
    hoedown_buffer_puts(ob,"\\begin{equation}\n");
	if (ref){
		hoedown_buffer_puts(ob,"\\label{");
		hoedown_buffer_puts(ob, ref);
		hoedown_buffer_puts(ob, "}\n");
	}
}

static void rndr_close_equation(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	/* scidown_latex_renderer_state *state = data->opaque; */
	hoedown_buffer_puts(ob, "\n\\end{equation}");
}

static void rndr_open_float(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data)
{
	switch (args.type)
	{
	case FIGURE:
		hoedown_buffer_puts(ob,  "\\begin{figure}[ht!]\n");
		break;
	case LISTING:
		/**TODO make it better**/
		hoedown_buffer_puts(ob,  "\\begin{program}\n");
		break;
	case TABLE:

		hoedown_buffer_puts(ob,  "\\begin{table}\n");
		break;
	default:
		break;
	}
	hoedown_buffer_puts(ob, "\\begin{center}\n");
}

static void rnrd_close_float(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data)
{
	if (args.caption){
		hoedown_buffer_puts(ob, "\\caption{");
		hoedown_buffer_puts(ob, args.caption);
		hoedown_buffer_puts(ob, "}\n");
	}
	if (args.id)
	{
		hoedown_buffer_puts(ob, "\\label{");
		hoedown_buffer_puts(ob, args.id);
		hoedown_buffer_puts(ob, "}\n");
	}

	hoedown_buffer_puts(ob,  "\\end{center}\n");
	switch (args.type)
	{
	case FIGURE:
		hoedown_buffer_puts(ob,  "\\end{figure}\n");
		break;
	case LISTING:
		/**TODO make it better**/
		hoedown_buffer_puts(ob,  "\\end{program}\n");
		break;
	case TABLE:

		hoedown_buffer_puts(ob,  "\\end{table}\n");
		break;
	default:
		break;
	}
}


static void
rndr_toc(hoedown_buffer *ob, toc * tree, int numbering)
{
	hoedown_buffer_puts(ob, "\\tableofcontents");
}

hoedown_renderer *
scidown_latex_renderer_new(scidown_render_flags render_flags, int nesting_level, localization local)
{
	static const hoedown_renderer cb_default = {
		NULL,

		rndr_head,
		rndr_title,
		rndr_authors,
		rndr_affiliation,
		rndr_keywords,
		rndr_begin,
		rndr_inner,
		rndr_end,
		rndr_pagebreak,

		rndr_close,
		rndr_abstract,
		rndr_open_equation,
		rndr_close_equation,
		rndr_open_float,
		rnrd_close_float,
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
		rndr_footnote_def,
		rndr_raw_block,
		rndr_toc,

		rndr_autolink,
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
		rndr_eq_math,
		rndr_ref,
		rndr_raw_html,

		NULL,
		rndr_normal_text,

		NULL,
		NULL,
		NULL,
	};

	scidown_latex_renderer_state *state;
	hoedown_renderer *renderer;

	/* Prepare the state pointer */
	state = hoedown_malloc(sizeof(scidown_latex_renderer_state));
	memset(state, 0x0, sizeof(scidown_latex_renderer_state));

	state->flags = render_flags;
	state->counter.figure = 0;
	state->counter.equation = 0;
	state->counter.listing = 0;
	state->counter.table = 0;

	state->localization = local;

  state->toc_data.nesting_level = nesting_level;

	/* Prepare the renderer */
	renderer = hoedown_malloc(sizeof(hoedown_renderer));
	memcpy(renderer, &cb_default, sizeof(hoedown_renderer));

	renderer->opaque = state;
	return renderer;
}

void
scidown_latex_renderer_free(hoedown_renderer *renderer)
{
	free(renderer->opaque);
	free(renderer);
}
