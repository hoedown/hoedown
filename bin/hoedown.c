#include "document.h"
#include "html.h"

#include "common.h"
//#include <time.h>

/* NULL RENDERER */

enum renderer_type {
	RENDERER_HTML,
	RENDERER_HTML_TOC,
	RENDERER_NULL,
};

hoedown_renderer *
null_renderer_new() {
	hoedown_renderer *rend = malloc(sizeof(hoedown_renderer));
	if (rend)
		memset(rend, 0x00, sizeof(hoedown_renderer));
	return rend;
}

void
null_renderer_free(hoedown_renderer *rend) {
	free(rend);
}


/* FEATURES INFO / DEFAULTS */

struct extension_category_info {
	unsigned int flags;
	const char *option_name;
	const char *label;
};

struct extension_info {
	unsigned int flag;
	const char *option_name;
	const char *description;
};

struct html_flag_info {
	unsigned int flag;
	const char *option_name;
	const char *description;
};

static struct extension_category_info categories_info[] = {
	{HOEDOWN_EXT_BLOCK, "block", "Block extensions"},
	{HOEDOWN_EXT_SPAN, "span", "Span extensions"},
	{HOEDOWN_EXT_FLAGS, "flags", "Other flags"},
	{HOEDOWN_EXT_NEGATIVE, "negative", "Negative flags"},
};

static struct extension_info extensions_info[] = {
	{HOEDOWN_EXT_TABLES, "tables", "Parse PHP-Markdown style tables."},
	{HOEDOWN_EXT_FENCED_CODE, "fenced-code", "Parse fenced code blocks."},
	{HOEDOWN_EXT_FOOTNOTES, "footnotes", "Parse footnotes."},

	{HOEDOWN_EXT_AUTOLINK, "autolink", "Automatically turn safe URLs into links."},
	{HOEDOWN_EXT_STRIKETHROUGH, "strikethrough", "Parse ~~stikethrough~~ spans."},
	{HOEDOWN_EXT_UNDERLINE, "underline", "Parse _underline_ instead of emphasis."},
	{HOEDOWN_EXT_HIGHLIGHT, "highlight", "Parse ==highlight== spans."},
	{HOEDOWN_EXT_QUOTE, "quote", "Render \"quotes\" as <q>quotes</q>."},
	{HOEDOWN_EXT_SUPERSCRIPT, "superscript", "Parse super^script."},
	{HOEDOWN_EXT_MATH, "math", "Parse MathJax syntax."},

	{HOEDOWN_EXT_LAX_SPACING, "lax-spacing", "Allow HTML blocks on the same line as text."},
	{HOEDOWN_EXT_NO_INTRA_EMPHASIS, "disable-intra-emphasis", "Disable emphasis_between_words."},
	{HOEDOWN_EXT_SPACE_HEADERS, "space-headers", "Require a space after '#' in headers."},
	{HOEDOWN_EXT_MATH_DOLLAR, "math-dollar", "Render $math$ as MathJax inline."},

	{HOEDOWN_EXT_DISABLE_INDENTED_CODE, "disable-indented-code", "Don't parse indented code blocks."},
};

static struct html_flag_info html_flags_info[] = {
	{HOEDOWN_HTML_SKIP_HTML, "skip-html", "Strip all HTML tags."},
	{HOEDOWN_HTML_ESCAPE, "escape", "Escape all HTML."},
	{HOEDOWN_HTML_SAFELINK, "safelink", "Only allow links to safe protocols."},
	{HOEDOWN_HTML_HARD_WRAP, "hard-wrap", "Render each linebreak as <br>."},
	{HOEDOWN_HTML_USE_XHTML, "xhtml", "Render XHTML."},
};

static const char *category_prefix = "all-";
static const char *negative_prefix = "no-";

#define DEF_IUNIT 1024
#define DEF_OUNIT 64
#define DEF_MAX_NESTING 16


/* PRINT HELP */

void
print_help(const char *basename) {
	/* usage */
	printf("Usage: %s [OPTION]... [FILE]\n\n", basename);

	/* description */
	printf("Process the Markdown in FILE (or standard input) and render it to standard output, using the Hoedown library. "
	       "Parsing and rendering can be customized through the options below. The default is to parse pure markdown and output HTML.\n\n");

	/* main options */
	printf("Main options:\n");
	print_option('n', "max-nesting=N", "Maximum level of block nesting parsed. Default is " str(DEF_MAX_NESTING) ".");
	print_option('t', "toc-level=N", "Maximum level for headers included in the TOC. Zero disables TOC (the default).");
	print_option(  0, "html", "Render (X)HTML. The default.");
	print_option(  0, "html-toc", "Render the Table of Contents in (X)HTML.");
	print_option(  0, "null", "Use a special \"null\" renderer that has no callbacks.");
	print_option('T', "time", "Show time spent in rendering.");
	print_option('i', "input-unit=N", "Reading block size. Default is " str(DEF_IUNIT) ".");
	print_option('o', "output-unit=N", "Writing block size. Default is " str(DEF_OUNIT) ".");
	print_option('h', "help", "Print this help text.");
	print_option('v', "version", "Print Hoedown version.");
	printf("\n");

	/* extensions */
	size_t i;
	size_t e;
	for (i = 0; i < count_of(categories_info); i++) {
		struct extension_category_info *category = categories_info+i;
		printf("%s (--%s%s):\n", category->label, category_prefix, category->option_name);
		for (e = 0; e < count_of(extensions_info); e++) {
			struct extension_info *extension = extensions_info+e;
			if (extension->flag & category->flags) {
				print_option(  0, extension->option_name, extension->description);
			}
		}
		printf("\n");
	}

	/* html-specific */
	printf("HTML-specific options:\n");
	for (i = 0; i < count_of(html_flags_info); i++) {
		struct html_flag_info *html_flag = html_flags_info+i;
		print_option(  0, html_flag->option_name, html_flag->description);
	}
	printf("\n");

	/* ending */
	printf("Flags and extensions can be negated by prepending 'no' to them, as in '--no-tables', '--no-span' or '--no-escape'. "
	       "Options are processed in order, so in case of contradictory options the last specified stands.\n\n");

	printf("When FILE is '-', read standard input. If no FILE was given, read standard input. Use '--' to signal end of option parsing. "
	       "Exit status is 0 if no errors occured, 1 with option parsing errors, 4 with memory allocation errors or 5 with I/O errors.\n\n");
}


/* MAIN LOGIC */

int
main(int argc, char **argv)
{
	int show_time = 0;
	//struct timespec start, end;

	/* buffers */
	hoedown_buffer *ib, *ob;
	size_t iunit = DEF_IUNIT, ounit = DEF_OUNIT;

	/* files */
	FILE *in = NULL;

	/* renderer */
	int toc_level = 0;
	int renderer_type = RENDERER_HTML;

	/* document */
	hoedown_document *document;
	unsigned int extensions = 0;
	size_t max_nesting = DEF_MAX_NESTING;

	/* HTML renderer-specific */
	unsigned int html_flags = 0;


	/* option parsing */
	int just_args = 0;
	int i, j;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (!arg[0]) continue;

		if (just_args || arg[0] != '-') {
			/* regular argument */
			in = fopen(arg, "r");
			if (!in) {
				fprintf(stderr, "Unable to open input file \"%s\": %s\n", arg, strerror(errno));
				return 5;
			}
			continue;
		}

		if (!arg[1]) {
			/* arg is "-" */
			in = stdin;
			continue;
		}

		if (arg[1] != '-') {
			/* parse short options */
			char opt;
			const char *val;
			for (j = 1; (opt = arg[j]); j++) {
				if (opt == 'h') {
					print_help(argv[0]);
					return 1;
				}

				if (opt == 'v') {
					print_version();
					return 1;
				}

				if (opt == 'T') {
					show_time = 1;
					continue;
				}

				/* options requiring value */
				if (arg[++j]) val = arg+j;
				else if (argv[++i]) val = argv[i];
				else {
					fprintf(stderr, "Wrong option '-%c' found.\n", opt);
					return 1;
				}

				long int num;
				int isNum = parseint(val, &num);

				if (opt == 'n' && isNum) {
					max_nesting = num;
					break;
				}

				if (opt == 't' && isNum) {
					toc_level = num;
					break;
				}

				if (opt == 'i' && isNum) {
					iunit = num;
					break;
				}

				if (opt == 'o' && isNum) {
					ounit = num;
					break;
				}

				fprintf(stderr, "Wrong option '-%c' found.\n", opt);
				return 1;
			}
			continue;
		}

		if (!arg[2]) {
			/* arg is "--" */
			just_args = 1;
			continue;
		}

		/* parse long option */
		char opt [100];
		strncpy(opt, arg+2, 100);
		opt[99] = 0;

		char *val = strchr(opt, '=');

		long int num = 0;
		int isNum = 0;

		if (val) {
			*val = 0;
			val++;

			if (*val)
				isNum = parseint(val, &num);
		}

		int opt_parsed = 0;

		if (strcmp(opt, "help")==0) {
			print_help(argv[0]);
			return 1;
		}

		if (strcmp(opt, "version")==0) {
			print_version();
			return 1;
		}

		if (strcmp(opt, "max-nesting")==0 && isNum) {
			opt_parsed = 1;
			max_nesting = num;
		}
		if (strcmp(opt, "toc-level")==0 && isNum) {
			opt_parsed = 1;
			toc_level = num;
		}
		if (strcmp(opt, "input-unit")==0 && isNum) {
			opt_parsed = 1;
			iunit = num;
		}
		if (strcmp(opt, "output-unit")==0 && isNum) {
			opt_parsed = 1;
			ounit = num;
		}

		if (strcmp(opt, "html")==0) {
			opt_parsed = 1;
			renderer_type = RENDERER_HTML;
		}
		if (strcmp(opt, "html-toc")==0) {
			opt_parsed = 1;
			renderer_type = RENDERER_HTML_TOC;
		}
		if (strcmp(opt, "null")==0) {
			opt_parsed = 1;
			renderer_type = RENDERER_NULL;
		}

		const char *name;
		size_t i;

		/* extension categories */
		if ((name = strprefix(opt, category_prefix))) {
			for (i = 0; i < count_of(categories_info); i++) {
				struct extension_category_info *category = categories_info+i;
				if (strcmp(name, category->option_name)==0) {
					opt_parsed = 1;
					extensions |= category->flags;
					break;
				}
			}
		}

		/* extensions */
		for (i = 0; i < count_of(extensions_info); i++) {
			struct extension_info *extension = extensions_info+i;
			if (strcmp(opt, extension->option_name)==0) {
				opt_parsed = 1;
				extensions |= extension->flag;
				break;
			}
		}

		/* html flags */
		for (i = 0; i < count_of(html_flags_info); i++) {
			struct html_flag_info *html_flag = html_flags_info+i;
			if (strcmp(opt, html_flag->option_name)==0) {
				opt_parsed = 1;
				html_flags |= html_flag->flag;
				break;
			}
		}

		/* negations */
		if ((name = strprefix(opt, negative_prefix))) {
			for (i = 0; i < count_of(categories_info); i++) {
				struct extension_category_info *category = categories_info+i;
				if (strcmp(name, category->option_name)==0) {
					opt_parsed = 1;
					extensions &= ~(category->flags);
					break;
				}
			}
			for (i = 0; i < count_of(extensions_info); i++) {
				struct extension_info *extension = extensions_info+i;
				if (strcmp(name, extension->option_name)==0) {
					opt_parsed = 1;
					extensions &= ~(extension->flag);
					break;
				}
			}
			for (i = 0; i < count_of(html_flags_info); i++) {
				struct html_flag_info *html_flag = html_flags_info+i;
				if (strcmp(name, html_flag->option_name)==0) {
					opt_parsed = 1;
					html_flags &= ~(html_flag->flag);
					break;
				}
			}
		}

		if (strcmp(opt, "time")==0) {
			opt_parsed = 1;
			show_time = 1;
		}

		if (!opt_parsed) {
			fprintf(stderr, "Wrong option '%s' found.\n", arg);
			return 1;
		}
	}

	if (!in)
		in = stdin;


	/* reading everything */
	ib = hoedown_buffer_new(iunit);
	if (!ib) {
		fprintf(stderr, "Couldn't allocate input buffer.\n");
		return 4;
	}

	while (!feof(in)) {
		if (ferror(in)) {
			fprintf(stderr, "I/O errors found while reading input.\n");
			return 5;
		}
		if (hoedown_buffer_grow(ib, ib->size + iunit) != HOEDOWN_BUF_OK) {
			fprintf(stderr, "Couldn't grow input buffer.\n");
			return 4;
		}
		ib->size += fread(ib->data + ib->size, 1, iunit, in);
	}

	if (in != stdin)
		fclose(in);


	/* creating the renderer */
	hoedown_renderer *renderer;
	void (*renderer_free)(hoedown_renderer*);

	switch (renderer_type) {
		case RENDERER_HTML:
			renderer = hoedown_html_renderer_new(html_flags, toc_level);
			renderer_free = hoedown_html_renderer_free;
			break;
		case RENDERER_HTML_TOC:
			renderer = hoedown_html_toc_renderer_new(toc_level);
			renderer_free = hoedown_html_renderer_free;
			break;
		case RENDERER_NULL:
			renderer = null_renderer_new();
			renderer_free = null_renderer_free;
			break;
		default:
			renderer = NULL;
			renderer_free = NULL;
	};

	if (!renderer) {
		fprintf(stderr, "Couldn't allocate renderer.\n");
		return 4;
	}


	/* performing markdown rendering */
	ob = hoedown_buffer_new(ounit);
	if (!ob) {
		fprintf(stderr, "Couldn't allocate output buffer.\n");
		return 4;
	}

	document = hoedown_document_new(renderer, extensions, max_nesting);
	if (!document) {
		fprintf(stderr, "Couldn't allocate document parser.\n");
		return 4;
	}

	//clock_gettime(CLOCK_MONOTONIC, &start);
	hoedown_document_render(document, ob, ib->data, ib->size);
	//clock_gettime(CLOCK_MONOTONIC, &end);


	/* writing the result to stdout */
	(void)fwrite(ob->data, 1, ob->size, stdout);


	/* showing rendering time */
	if (show_time) {
		//TODO: enable this
		//long long elapsed = (  end.tv_sec*1000000000 +   end.tv_nsec)
		//                  - (start.tv_sec*1000000000 + start.tv_nsec);
		//if (elapsed < 1000000000)
		//	fprintf(stderr, "Time spent on rendering: %.2f ms.\n", ((double)elapsed)/1000000);
		//else
		//	fprintf(stderr, "Time spent on rendering: %.3f s.\n", ((double)elapsed)/1000000000);
	}


	/* cleanup */
	hoedown_buffer_free(ib);
	hoedown_buffer_free(ob);

	hoedown_document_free(document);
	renderer_free(renderer);

	if (ferror(stdout)) {
		fprintf(stderr, "I/O errors found while writing output.\n");
		return 5;
	}

	return 0;
}
