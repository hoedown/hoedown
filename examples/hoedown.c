#include "markdown.h"
#include "html.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_UNIT 1024
#define OUTPUT_UNIT 64

static void
error(const char *msg)
{
	fprintf(stderr, "Error: %s\n", msg);
	exit(1);
}

int
main(int argc, char **argv)
{
	hoedown_buffer *ib, *ob;
	FILE *in = stdin;
	int err;
	hoedown_renderer *renderer;
	hoedown_markdown *markdown;

	/* opening the file if given from the command line */
	if (argc > 1) {
		in = fopen(argv[1], "r");
		if (!in) {
			fprintf(stderr, "Unable to open input file \"%s\": %s\n", argv[1], strerror(errno));
			return 1;
		}
	}

	/* reading everything */
	ib = hoedown_buffer_new(READ_UNIT);
	if (!ib)
		error("input buffer creation failed");
	while (!feof(in) && !ferror(in)) {
		err = hoedown_buffer_grow(ib, ib->size + READ_UNIT);
		if (err != HOEDOWN_BUF_OK)
			error("input buffer re-allocation failed");
		ib->size += fread(ib->data + ib->size, 1, READ_UNIT, in);
	}

	if (in != stdin)
		fclose(in);

	/* performing markdown parsing */
	ob = hoedown_buffer_new(OUTPUT_UNIT);
	if (!ob)
		error("output buffer creation failed");

	renderer = hoedown_html_renderer_new(0, 0);
	if (!renderer)
		error("renderer allocation failed");

	markdown = hoedown_markdown_new(0, 16, renderer);
	if (!markdown)
		error("parser state allocation failed");

	err = hoedown_markdown_render(ob, ib->data, ib->size, markdown);
	if (err)
		error("Markdown processing failed");

	hoedown_markdown_free(markdown);
	hoedown_html_renderer_free(renderer);

	/* writing the result to stdout */
	(void)fwrite(ob->data, 1, ob->size, stdout);

	/* cleanup */
	hoedown_buffer_free(ib);
	hoedown_buffer_free(ob);

	return ferror(stdout);
}
