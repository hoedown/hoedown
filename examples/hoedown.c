#include "document.h"
#include "html.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#define READ_UNIT 1024
#define OUTPUT_UNIT 64

int
main(int argc, char **argv)
{
	hoedown_buffer *ib, *ob;
	FILE *in = stdin;

	hoedown_renderer *renderer;
	hoedown_document *document;

	int html = 0, extensions = 0;

	int opt;
	const struct option long_options[] = {
		/* html */
		{ "skip_html", 0, NULL, 0 },
		{ "skip_style", 0, NULL, 1 },
		{ "skip_images", 0, NULL, 2 },
		{ "skip_links", 0, NULL, 3 },
		{ "expnand_tabs", 0, NULL, 4 },
		{ "safelink", 0, NULL, 5 },
		{ "toc", 0, NULL, 6 },
		{ "hard_wrap", 0, NULL, 7 },
		{ "use_xhtml", 0, NULL, 8 },
		{ "escape", 0, NULL, 9 },
		/* extensions */
		{ "space_headers", 0, NULL, 100 },
		{ "tables", 0, NULL, 101 },
		{ "fenced_code", 0, NULL, 102 },
		{ "footnotes", 0, NULL, 103 },
		{ "autolink", 0, NULL, 104 },
		{ "strikethrough", 0, NULL, 105 },
		{ "underline", 0, NULL, 106 },
		{ "highlight", 0, NULL, 107 },
		{ "quote", 0, NULL, 108 },
		{ "superscript", 0, NULL, 109 },
		{ "lax_spacing", 0, NULL, 110 },
		{ "no_intra_emphasis", 0, NULL, 111 },
		{ "disable_indented_code", 0, NULL, 112 },
		{ "special_attribute", 0, NULL, 113 },
		{ NULL, 0, NULL, 0 }
	};

	while ((opt = getopt_long_only(argc, argv, "", long_options, NULL)) != -1) {
		if (opt > 0 && opt < 100) {
			html |= (1 << opt);
		} else if (opt >= 100) {
			extensions |= (1 << (opt - 100));
		}
	}

	/* opening the file if given from the command line */
	if ((argc - optind) > 0) {
		in = fopen(argv[optind], "r");
		if (!in) {
			fprintf(stderr, "Unable to open input file \"%s\": %s\n", argv[1], strerror(errno));
			return 1;
		}
	}

	/* reading everything */
	ib = hoedown_buffer_new(READ_UNIT);
	if (!ib) {
		fprintf(stderr, "Couldn't allocate input buffer.\n");
		return 1;
	}

	while (!feof(in)) {
		if (ferror(in)) {
			fprintf(stderr, "I/O errors found while reading input.\n");
			return 1;
		}
		if (hoedown_buffer_grow(ib, ib->size + READ_UNIT) != HOEDOWN_BUF_OK) {
			fprintf(stderr, "Couldn't grow input buffer.\n");
			return 1;
		}
		ib->size += fread(ib->data + ib->size, 1, READ_UNIT, in);
	}

	if (in != stdin)
		fclose(in);

	/* performing markdown parsing */
	ob = hoedown_buffer_new(OUTPUT_UNIT);
	if (!ob) {
		fprintf(stderr, "Couldn't allocate output buffer.\n");
		return 1;
	}

	renderer = hoedown_html_renderer_new(0, 0);
	if (html > 0) {
		hoedown_html_renderer_state *state;
		state = (hoedown_html_renderer_state *)renderer->opaque;
		state->flags = html;
	}
	document = hoedown_document_new(renderer, extensions, 16);

	hoedown_document_render(document, ob, ib->data, ib->size);

	hoedown_document_free(document);
	hoedown_html_renderer_free(renderer);

	/* writing the result to stdout */
	(void)fwrite(ob->data, 1, ob->size, stdout);

	/* cleanup */
	hoedown_buffer_free(ib);
	hoedown_buffer_free(ob);

	if (ferror(stdout)) {
		fprintf(stderr, "I/O errors found while writing output.\n");
		return 1;
	}

	return 0;
}
