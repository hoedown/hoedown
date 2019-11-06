#include "html.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "escape.h"

#include "charter/src/parser.h"
#include "charter/src/renderer.h"

#define USE_XHTML(opt) (opt->flags & SCIDOWN_RENDER_USE_XHTML)
#define MAX_FILE_SIZE 1000000

scidown_render_tag
hoedown_html_is_tag(const uint8_t *data, size_t size, const char *tagname)
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

static void escape_html(hoedown_buffer *ob, const uint8_t *source, size_t length)
{
	hoedown_escape_html(ob, source, length, 0);
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
	hoedown_html_renderer_state *state = data->opaque;

	if (!link || !link->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<a href=\"");
	if (type == HOEDOWN_AUTOLINK_EMAIL)
		HOEDOWN_BUFPUTSL(ob, "mailto:");
	escape_href(ob, link->data, link->size);

	if (state->link_attributes) {
		hoedown_buffer_putc(ob, '\"');
		state->link_attributes(ob, link, data);
		hoedown_buffer_putc(ob, '>');
	} else {
		HOEDOWN_BUFPUTSL(ob, "\">");
	}

	/*
	 * Pretty printing: if we get an email address as
	 * an actual URI, e.g. `mailto:foo@bar.com`, we don't
	 * want to print the `mailto:` prefix
	 */
	if (hoedown_buffer_prefix(link, "mailto:") == 0) {
		escape_html(ob, link->data + 7, link->size - 7);
	} else {
		escape_html(ob, link->data, link->size);
	}

	HOEDOWN_BUFPUTSL(ob, "</a>");

	return 1;
}

static void
rndr_blockcode(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	hoedown_html_renderer_state *state = data->opaque;
	if (lang && (state->flags & SCIDOWN_RENDER_CHARTER) != 0 && hoedown_buffer_eqs(lang, "charter") != 0){
		if (text){

			char * copy = malloc((text->size + 1)*sizeof(char));
			memset(copy, 0, text->size+1);
			memcpy(copy, text->data, text->size);

			chart * c =  parse_chart(copy);
			char * svg = chart_to_svg(c);

			int n = strlen(svg);
			hoedown_buffer_printf(ob, svg, n);

			free(copy);
			chart_free(c);
			free(svg);

		}
		return;
	}
	if (lang &&  (state->flags & SCIDOWN_RENDER_GNUPLOT) != 0 && hoedown_buffer_eqs(lang, "gnuplot"))
	{
		if (text && text->size){
			char * copy = malloc((text->size + 1)*sizeof(char));
			memset(copy, 0, text->size+1);
			memcpy(copy, text->data, text->size);
			hoedown_buffer * b = hoedown_buffer_new(1);
			hoedown_buffer_printf(b, "gnuplot -e 'set term svg size 300,200;\n%s'", copy);

			FILE *p = popen((char*)b->data, "r");
			hoedown_buffer_free(b);
			char buffer[MAX_FILE_SIZE];
			size_t i;
			for (i = 0; i < MAX_FILE_SIZE; ++i)
			{
			    int c = getc(p);

			    if (c == EOF)
			    {
			        buffer[i] = 0x00;
			        break;
			    }

			    buffer[i] = c;
			}
			if (i)
			{
				hoedown_buffer_printf(ob, buffer, i);
			}
		}

		return;
	}
	if (lang && (state->flags & SCIDOWN_RENDER_MERMAID) != 0 && hoedown_buffer_eqs(lang, "mermaid") != 0){
		if (text){
	        HOEDOWN_BUFPUTSL(ob, "<div class=\"mermaid\">");
			hoedown_buffer_put(ob, text->data, text->size);
			HOEDOWN_BUFPUTSL(ob, "</div>");
		}
		return;
	}
	if (lang) {
		HOEDOWN_BUFPUTSL(ob, "<pre><code class=\"language-");
		escape_html(ob, lang->data, lang->size);
		HOEDOWN_BUFPUTSL(ob, "\">");
	} else {
		HOEDOWN_BUFPUTSL(ob, "<pre><code>");
	}

	if (text)
		escape_html(ob, text->data, text->size);

	HOEDOWN_BUFPUTSL(ob, "</code></pre>\n");
}

static void
rndr_blockquote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "<blockquote>\n");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</blockquote>\n");
}

static int
rndr_codespan(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "<code>");
	if (text) escape_html(ob, text->data, text->size);
	HOEDOWN_BUFPUTSL(ob, "</code>");
	return 1;
}

static int
rndr_strikethrough(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<del>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</del>");
	return 1;
}

static int
rndr_double_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<strong>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</strong>");

	return 1;
}

static int
rndr_emphasis(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "<em>");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</em>");
	return 1;
}

static int
rndr_underline(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<u>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</u>");

	return 1;
}

static int
rndr_highlight(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<mark>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</mark>");

	return 1;
}

static int
rndr_quote(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size)
		return 0;

	HOEDOWN_BUFPUTSL(ob, "<q>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</q>");

	return 1;
}

static int
rndr_linebreak(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	hoedown_buffer_puts(ob, USE_XHTML(state) ? "<br/>\n" : "<br>\n");
	return 1;
}

static void
rndr_header(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data, h_counter counter, int numbering)
{
	if (ob->size)
		hoedown_buffer_putc(ob, '\n');

	if (level > 3) {
		hoedown_buffer_printf(ob, "<h%d id=\"toc_%d.%d.%d.%d\">", level+1, counter.chapter, counter.section, counter.subsection, level);
	} else if (counter.subsection) {
		hoedown_buffer_printf(ob, "<h%d id=\"toc_%d.%d.%d\">", level+1, counter.chapter, counter.section, counter.subsection);
	} else if (counter.section) {
		hoedown_buffer_printf(ob, "<h%d id=\"toc_%d.%d\">", level+1, counter.chapter, counter.section);
	} else if (counter.chapter) {
		hoedown_buffer_printf(ob, "<h%d id=\"toc_%d\">", level+1, counter.chapter);
	}

	if (numbering && level <= 3)
	{
		if (counter.subsection) {
			hoedown_buffer_printf(ob, "%d.%d.%d. ", counter.chapter, counter.section, counter.subsection);
		} else if (counter.section) {
			hoedown_buffer_printf(ob, "%d.%d. ", counter.chapter, counter.section);
		} else if (counter.chapter) {
			hoedown_buffer_printf(ob, "%d. ", counter.chapter);
		}
	}


	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_printf(ob, "</h%d>\n", level+1);
}

static int
rndr_link(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	HOEDOWN_BUFPUTSL(ob, "<a href=\"");

	if (link && link->size)
		escape_href(ob, link->data, link->size);

	if (title && title->size) {
		HOEDOWN_BUFPUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size);
	}

	if (state->link_attributes) {
		hoedown_buffer_putc(ob, '\"');
		state->link_attributes(ob, link, data);
		hoedown_buffer_putc(ob, '>');
	} else {
		HOEDOWN_BUFPUTSL(ob, "\">");
	}

	if (content && content->size) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</a>");
	return 1;
}

static void
rndr_list(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	hoedown_buffer_puts(ob, (flags & HOEDOWN_LIST_ORDERED ? "<ol dir=\"auto\">\n" : "<ul dir=\"auto\">\n"));
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	hoedown_buffer_put(ob, (const uint8_t *)(flags & HOEDOWN_LIST_ORDERED ? "</ol>\n" : "</ul>\n"), 6);
}

static void
rndr_listitem(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "<li>");
	if (content) {
		size_t size = content->size;
		while (size && content->data[size - 1] == '\n')
			size--;

		hoedown_buffer_put(ob, content->data, size);
	}
	HOEDOWN_BUFPUTSL(ob, "</li>\n");
}

static void
rndr_paragraph(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	size_t i = 0;

	if (ob->size) hoedown_buffer_putc(ob, '\n');

	if (!content || !content->size)
		return;

	while (i < content->size && isspace(content->data[i])) i++;

	if (i == content->size)
		return;

	HOEDOWN_BUFPUTSL(ob, "<p>");
	if (state->flags & SCIDOWN_RENDER_HARD_WRAP) {
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
	HOEDOWN_BUFPUTSL(ob, "</p>\n");
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
	HOEDOWN_BUFPUTSL(ob, "<strong><em>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</em></strong>");
	return 1;
}

static void
rndr_hrule(hoedown_buffer *ob, const hoedown_renderer_data *data)
{

	hoedown_html_renderer_state *state = data->opaque;
	if (ob->size) hoedown_buffer_putc(ob, '\n');
	if (data->meta->doc_class == CLASS_BEAMER) {
		hoedown_buffer_puts(ob, "</div></div>\n");
		if (data->meta->paper_size == B169)
			hoedown_buffer_puts(ob, "<div class=\"slide slide_169\"><div class=\"slide_body\">");
		else
			hoedown_buffer_puts(ob, "<div class=\"slide\"><div class=\"slide_body\">");
	} else
		hoedown_buffer_puts(ob, USE_XHTML(state) ? "<hr/>\n" : "<hr>\n");
}

static int
rndr_image(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	if (!link || !link->size) return 0;



	HOEDOWN_BUFPUTSL(ob, "<img src=\"");
	escape_href(ob, link->data, link->size);
	HOEDOWN_BUFPUTSL(ob, "\" alt=\"");

	if (alt && alt->size)
		escape_html(ob, alt->data, alt->size);

	if (title && title->size) {
		HOEDOWN_BUFPUTSL(ob, "\" title=\"");
		escape_html(ob, title->data, title->size); }
	hoedown_buffer_puts(ob, USE_XHTML(state) ? "\"/>" : "\">");


	return 1;
}

static int
rndr_raw_html(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	/* ESCAPE overrides SKIP_HTML. It doesn't look to see if
	 * there are any valid tags, just escapes all of them. */
	if((state->flags & SCIDOWN_RENDER_ESCAPE) != 0) {
		escape_html(ob, text->data, text->size);
		return 1;
	}

	if ((state->flags & SCIDOWN_RENDER_SKIP_HTML) != 0)
		return 1;

	hoedown_buffer_put(ob, text->data, text->size);
	return 1;
}

static void
rndr_table(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data, hoedown_table_flags *flags, int cols)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "<table dir=\"auto\">\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</table>\n");
}

static void
rndr_table_header(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    HOEDOWN_BUFPUTSL(ob, "<thead>\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</thead>\n");
}

static void
rndr_table_body(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
    if (ob->size) hoedown_buffer_putc(ob, '\n');
    HOEDOWN_BUFPUTSL(ob, "<tbody>\n");
    hoedown_buffer_put(ob, content->data, content->size);
    HOEDOWN_BUFPUTSL(ob, "</tbody>\n");
}

static void
rndr_tablerow(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	HOEDOWN_BUFPUTSL(ob, "<tr>\n");
	if (content) hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</tr>\n");
}

static void
rndr_tablecell(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data)
{
	if (flags & HOEDOWN_TABLE_HEADER) {
		HOEDOWN_BUFPUTSL(ob, "<th");
	} else {
		HOEDOWN_BUFPUTSL(ob, "<td");
	}

	switch (flags & HOEDOWN_TABLE_ALIGNMASK) {
	case HOEDOWN_TABLE_ALIGN_CENTER:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: center\">");
		break;

	case HOEDOWN_TABLE_ALIGN_LEFT:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: left\">");
		break;

	case HOEDOWN_TABLE_ALIGN_RIGHT:
		HOEDOWN_BUFPUTSL(ob, " style=\"text-align: right\">");
		break;

	default:
		HOEDOWN_BUFPUTSL(ob, ">");
	}

	if (content)
		hoedown_buffer_put(ob, content->data, content->size);

	if (flags & HOEDOWN_TABLE_HEADER) {
		HOEDOWN_BUFPUTSL(ob, "</th>\n");
	} else {
		HOEDOWN_BUFPUTSL(ob, "</td>\n");
	}
}

static int
rndr_superscript(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (!content || !content->size) return 0;
	HOEDOWN_BUFPUTSL(ob, "<sup>");
	hoedown_buffer_put(ob, content->data, content->size);
	HOEDOWN_BUFPUTSL(ob, "</sup>");
	return 1;
}

static void
rndr_normal_text(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	if (content)
		escape_html(ob, content->data, content->size);
}

static void
rndr_footnotes(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	if (ob->size) hoedown_buffer_putc(ob, '\n');
	HOEDOWN_BUFPUTSL(ob, "<div class=\"footnotes\">\n");
	hoedown_buffer_puts(ob, USE_XHTML(state) ? "<hr/>\n" : "<hr>\n");
	HOEDOWN_BUFPUTSL(ob, "<ol>\n");

	if (content) hoedown_buffer_put(ob, content->data, content->size);

	HOEDOWN_BUFPUTSL(ob, "\n</ol>\n</div>\n");
}

static void
rndr_footnote_def(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data)
{
	size_t i = 0;
	int pfound = 0;

	/* insert anchor at the end of first paragraph block */
	if (content) {
		while ((i+3) < content->size) {
			if (content->data[i++] != '<') continue;
			if (content->data[i++] != '/') continue;
			if (content->data[i++] != 'p' && content->data[i] != 'P') continue;
			if (content->data[i] != '>') continue;
			i -= 3;
			pfound = 1;
			break;
		}
	}

	hoedown_buffer_printf(ob, "\n<li id=\"fn%d\">\n", num);
	if (pfound) {
		hoedown_buffer_put(ob, content->data, i);
		hoedown_buffer_printf(ob, "&nbsp;<a href=\"#fnref%d\" rev=\"footnote\">&#8617;</a>", num);
		hoedown_buffer_put(ob, content->data + i, content->size - i);
	} else if (content) {
		hoedown_buffer_put(ob, content->data, content->size);
	}
	HOEDOWN_BUFPUTSL(ob, "</li>\n");
}

static int
rndr_footnote_ref(hoedown_buffer *ob, int num, const hoedown_renderer_data *data)
{
	if (num >= 0) {
		hoedown_buffer_printf(ob, "<sup id=\"fnref%d\"><a href=\"#fn%d\" rel=\"footnote\">%d</a></sup>", num, num, num);
	} else {
		hoedown_buffer_printf(ob, "<sup>?</sup>");
	}
	return 1;
}

static int
rndr_math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data)
{
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\[" : "\\("), 2);

	escape_html(ob, text->data, text->size);
	hoedown_buffer_put(ob, (const uint8_t *)(displaymode ? "\\]" : "\\)"), 2);
	return 1;
}

static void
rndr_head(hoedown_buffer *ob, metadata * doc_meta, ext_definition * extension)
{
	hoedown_buffer_puts(ob, "<!DOCTYPE html>\n<html><head>\n<meta charset=\"UTF-8\">\n");
	if (doc_meta->title){
		hoedown_buffer_printf(ob, "<title>%s</title>\n", doc_meta->title);
	}
	if (doc_meta->authors)
	{
		hoedown_buffer_puts(ob, "<meta name=\"author\" content=\"");
		Strings * it;
		for (it = doc_meta->authors; it != NULL; it = it->next)
		{
			if (it->size == 1) {
				hoedown_buffer_puts(ob, it->str);
			} else {
				hoedown_buffer_printf(ob, "%s, ", it->str);
			}
		}
		hoedown_buffer_puts(ob, "\">\n");
	}
	if (doc_meta->keywords)
	{
		hoedown_buffer_printf(ob, "<meta name=\"keywords\" content=\"%s\">\n", doc_meta->keywords);
	}
	if (doc_meta->style)
	{
		hoedown_buffer_printf(ob, "<link rel=\"stylesheet\" href=\"%s\">\n", doc_meta->style);
	}
	if (extension && extension->extra_header)
	{
		hoedown_buffer_puts(ob, extension->extra_header);
	}

	hoedown_buffer_puts(ob, "</head>\n<body>\n");
}

static void
rndr_title(hoedown_buffer *ob, const hoedown_buffer *content, const metadata *data)
{
	hoedown_buffer_puts(ob, "<h1 class=\"title\">");
	escape_html(ob, content->data, content->size);
	hoedown_buffer_puts(ob, "</h1>\n");
}

static void
rndr_authors(hoedown_buffer *ob, Strings *authors)
{
	hoedown_buffer_puts(ob, "<div class=\"authors\">");
	Strings *it;
	for (it = authors; it != NULL; it = it->next){
		if (it->str){
			hoedown_buffer_printf(ob,"<span class=\"author\">%s</span><span class=\"and\"> </span>", it->str);
		}
	}
	hoedown_buffer_puts(ob, "</div>\n");
}

static void
rndr_affiliation(hoedown_buffer *ob, const hoedown_buffer *content,  const hoedown_renderer_data *data)
{
	hoedown_buffer_puts(ob, "<div class=\"affiliation\">");
	escape_html(ob, content->data, content->size);
	hoedown_buffer_puts(ob, "</div>\n");
}

static void
rndr_keywords(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data)
{
	hoedown_buffer_puts(ob, "<div class=\"keywords\">");
	hoedown_buffer_puts(ob, "<b>Keywords: </b>");
	escape_html(ob, content->data, content->size);
	hoedown_buffer_puts(ob, "</div>\n");
}

static void
rndr_begin(hoedown_buffer *ob,  const hoedown_renderer_data *data)
{
	if (data->meta->doc_class == CLASS_BEAMER) {
		if (data->meta->title || data->meta->authors || data->meta->affiliation) {
			if (data->meta->paper_size == B169)
				hoedown_buffer_puts(ob, "<div class=\"document\">\n<div class=\"header slide slide_169\"><div class=\"slide_body\">");
			else
				hoedown_buffer_puts(ob, "<div class=\"document\">\n<div class=\"header slide\"><div class=\"slide_body\">");
		}
	} else
		hoedown_buffer_puts(ob, "<div class=\"document\">\n<div class=\"header\">");
}

static void
rndr_inner(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	if (data->meta->doc_class == CLASS_BEAMER) {
		if (data->meta->title || data->meta->authors || data->meta->affiliation) {

			hoedown_buffer_puts(ob, "</div></div><div class=\"inner\">");
		}
	} else
		hoedown_buffer_puts(ob, "</div><div class=\"inner\">");
	if (data->meta->doc_class == CLASS_BEAMER) {
		if (data->meta->paper_size == B169)
			hoedown_buffer_puts(ob, "\n<div class=\"slide slide_169\"><div class=\"slide_body\">");
		else
			hoedown_buffer_puts(ob, "\n<div class=\"slide\"><div class=\"slide_body\">");
	}
}

static void
rndr_end(hoedown_buffer *ob, ext_definition * extension, const hoedown_renderer_data *data)
{
	if (data->meta->doc_class == CLASS_BEAMER) {
		hoedown_buffer_puts(ob, "</div></div>\n");
	}
	hoedown_buffer_puts(ob, "</div>\n</div>\n");
	if (extension && extension->extra_closing)
	{
		hoedown_buffer_puts(ob, extension->extra_closing);
	}
	hoedown_buffer_puts(ob, "</body>\n</html>\n");
}

static void
rndr_pagebreak(hoedown_buffer *ob)
{
	hoedown_buffer_puts(ob, "<div class=\"pagebreak\"> </div>");
}


static void
rndr_abstract(hoedown_buffer *ob){
	hoedown_buffer_puts(ob, "<div id=\"abstract\" class=\"abstract\">\n");
	hoedown_buffer_puts(ob, "<h2>Abstract</h2>\n");
}

static void
rndr_close(hoedown_buffer *ob){
	hoedown_buffer_puts(ob, "\n</div>\n");
}

static int rndr_ref (hoedown_buffer *ob, char * id, int count)
{
	if (count < 0 )	{
		hoedown_buffer_printf(ob, "<a href=\"#%s\">(\?\?)</a>", id);
	}else {
		hoedown_buffer_printf(ob, "<a href=\"#%s\">%d</a>", id, count);
	}
	return 1;
}

static void rndr_open_equation(hoedown_buffer *ob, const char * ref, const hoedown_renderer_data *data)
{
	if (ref){
		hoedown_buffer_puts(ob,"<div id=\"");
		hoedown_buffer_puts(ob, ref);
		hoedown_buffer_puts(ob, "\" class=\"equation\">\n");
	}
	else {
		hoedown_buffer_puts(ob, "<div class=\"equation\">\n");
	}
	hoedown_html_renderer_state *state = data->opaque;
	state->counter.equation ++;
	hoedown_buffer_printf(ob, "<table class=\"eq_table\"><tr><td class=\"eq_code\">");
}

static void rndr_close_equation(hoedown_buffer *ob, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;
	hoedown_buffer_printf(ob, "</td><td class=\"counter\">\\[(%d)\\]</td></tr></table></div>\n", state->counter.equation);
}

static void rndr_open_float(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data)
{
	if (args.id){
		hoedown_buffer_puts(ob,"<figure id=\"");
		hoedown_buffer_puts(ob, args.id);
		hoedown_buffer_puts(ob, "\">\n");
		return;
	}
	hoedown_buffer_puts(ob, "<figure>\n");
}

static void rnrd_close_float(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state = data->opaque;

	if (args.caption){
		hoedown_buffer_puts(ob, "<figcaption><b>");
		switch (args.type)
		{
		case FIGURE:
			state->counter.figure++;
			hoedown_buffer_printf(ob,  "Figure %u.</b> ", state->counter.figure);
			break;
		case LISTING:
			state->counter.listing++;
			hoedown_buffer_printf(ob,  "Listing %u.</b> ", state->counter.listing);
			break;
		case TABLE:
			state->counter.table++;
			hoedown_buffer_printf(ob,  "Table %u.</b> ", state->counter.table);
			break;
		default:
			break;
		}
		hoedown_buffer_puts(ob, args.caption);
		hoedown_buffer_puts(ob, "</figcaption>\n");
	}
	hoedown_buffer_puts(ob, "</figure>\n");
}

static void
rndr_toc_entry(hoedown_buffer *ob, toc * tree, int * chapter, int * section, int * subsection, int numbering)
{
	if (!tree)
		return;
	if (tree->nesting == 1) {
		if ((*chapter))
			hoedown_buffer_puts(ob, "</ul>\n");
		(*chapter) ++;

		if ((*section))
			hoedown_buffer_puts(ob, "</ul>\n");

		(*section) = 0;
		(*subsection) = 0;

		hoedown_buffer_printf(ob, "<li><a href=\"#toc_%d\">", (*chapter));
		if (numbering)
			hoedown_buffer_printf(ob, "%d. ", (*chapter));
		hoedown_buffer_printf(ob, "%s</a></li>\n<ul dir=\"auto\">\n", tree->text);
	} else if (tree->nesting == 2)
	{
		if ((*section))
			hoedown_buffer_puts(ob, "</ul>\n");
		(*section) ++;
		(*subsection) = 0;
		hoedown_buffer_printf(ob, "<li><a href=\"#toc_%d.%d\">", (*chapter), (*section));
		if (numbering)
			hoedown_buffer_printf(ob, "%d.%d. ", (*chapter), (*section));
		hoedown_buffer_printf(ob, "%s</a></li>\n<ul dir=\"auto\">\n", tree->text);
	} else if (tree->nesting == 3)
	{
		(*subsection) ++;
		hoedown_buffer_printf(ob, "<li><a href=\"#toc_%d.%d.%d\">",
			                      (*chapter), (*section), (*subsection));
		if (numbering)
			hoedown_buffer_printf(ob, "%d.%d.%d. ",
			                      (*chapter), (*section), (*subsection));
		hoedown_buffer_printf(ob, "%s</a></li>\n",tree->text);
	}
	rndr_toc_entry(ob, tree->sibling, chapter, section, subsection, numbering);
}

static void
rndr_toc(hoedown_buffer *ob, toc * tree, int numbering)
{
	hoedown_buffer_puts(ob, "<div class=\"toc_container\">\n<h2 class=\"toc_header\">Table of Contents</h2>\n<ul class=\"toc_list\">\n");
	int cpt=0, sct=0, sbs=0;
	rndr_toc_entry(ob, tree, &cpt, &sct, &sbs, numbering);
	hoedown_buffer_puts(ob, "</ul></div>\n");
}

static void
rndr_position(hoedown_buffer *ob)
{
	hoedown_buffer_puts(ob, "<span id=\"cursor_pos\"></span>");
}


static void
toc_header(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data, h_counter none, int numbering)
{
	hoedown_html_renderer_state *state = data->opaque;

	if (level <= state->toc_data.nesting_level) {
		/* set the level offset if this is the first header
		 * we're parsing for the document */
		if (state->toc_data.current_level == 0)
			state->toc_data.level_offset = level - 1;

		level -= state->toc_data.level_offset;

		if (level > state->toc_data.current_level) {
			while (level > state->toc_data.current_level) {
				HOEDOWN_BUFPUTSL(ob, "<ul dir=\"auto\">\n<li>\n");
				state->toc_data.current_level++;
			}
		} else if (level < state->toc_data.current_level) {
			HOEDOWN_BUFPUTSL(ob, "</li>\n");
			while (level < state->toc_data.current_level) {
				HOEDOWN_BUFPUTSL(ob, "</ul>\n</li>\n");
				state->toc_data.current_level--;
			}
			HOEDOWN_BUFPUTSL(ob,"<li>\n");
		} else {
			HOEDOWN_BUFPUTSL(ob,"</li>\n<li>\n");
		}

		hoedown_buffer_printf(ob, "<a href=\"#toc_%d\">", state->toc_data.header_count++);
		if (content) hoedown_buffer_put(ob, content->data, content->size);
		HOEDOWN_BUFPUTSL(ob, "</a>\n");
	}
}

static int
toc_link(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data)
{
	if (content && content->size) hoedown_buffer_put(ob, content->data, content->size);
	return 1;
}

static void
toc_finalize(hoedown_buffer *ob, int inline_render, const hoedown_renderer_data *data)
{
	hoedown_html_renderer_state *state;

	if (inline_render)
		return;

	state = data->opaque;

	while (state->toc_data.current_level > 0) {
		HOEDOWN_BUFPUTSL(ob, "</li>\n</ul>\n");
		state->toc_data.current_level--;
	}

	state->toc_data.header_count = 0;
}

hoedown_renderer *
hoedown_html_toc_renderer_new(int nesting_level, localization local)
{
	static const hoedown_renderer cb_default = {
		NULL,

		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,

		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		toc_header,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,

		NULL,
		rndr_codespan,
		rndr_double_emphasis,
		rndr_emphasis,
		rndr_underline,
		rndr_highlight,
		rndr_quote,
		NULL,
		NULL,
		toc_link,
		rndr_triple_emphasis,
		rndr_strikethrough,
		rndr_superscript,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,

		NULL,
		rndr_normal_text,

		NULL,
		toc_finalize,
		NULL
	};

	hoedown_html_renderer_state *state;
	hoedown_renderer *renderer;

	/* Prepare the state pointer */
	state = hoedown_malloc(sizeof(hoedown_html_renderer_state));
	memset(state, 0x0, sizeof(hoedown_html_renderer_state));

	state->toc_data.nesting_level = nesting_level;
	state->counter.figure = 0;
	state->counter.equation = 0;
	state->counter.listing =0;
	state->counter.table = 0;
	state->localization = local;

	/* Prepare the renderer */
	renderer = hoedown_malloc(sizeof(hoedown_renderer));
	memcpy(renderer, &cb_default, sizeof(hoedown_renderer));

	renderer->opaque = state;
	return renderer;
}

hoedown_renderer *
hoedown_html_renderer_new(scidown_render_flags render_flags, int nesting_level, localization local)
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
		rndr_math,
		rndr_ref,
		rndr_raw_html,

		NULL,
		rndr_normal_text,

		NULL,
		NULL,
		rndr_position,
	};

	hoedown_html_renderer_state *state;
	hoedown_renderer *renderer;

	/* Prepare the state pointer */
	state = hoedown_malloc(sizeof(hoedown_html_renderer_state));
	memset(state, 0x0, sizeof(hoedown_html_renderer_state));

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

	if (render_flags & SCIDOWN_RENDER_SKIP_HTML || render_flags & SCIDOWN_RENDER_ESCAPE)
		renderer->blockhtml = NULL;

	renderer->opaque = state;
	return renderer;
}

void
hoedown_html_renderer_free(hoedown_renderer *renderer)
{
	free(renderer->opaque);
	free(renderer);
}
