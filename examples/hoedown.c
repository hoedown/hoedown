#include "markdown.h"
#include "html.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define READ_UNIT 1024
#define OUTPUT_UNIT 64

int
main(int argc, char **argv)
{
	hoedown_buffer *ib, *ob;
	FILE *in = stdin;

	hoedown_renderer *renderer;
	hoedown_markdown *markdown;
	unsigned int extensions = 0;

	int i;
	char *earg;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-e") == 0) {
			if (i+1 >= argc) {
				fprintf(stderr, "Expected a list of extensions to activate\n");
				return 1;
			}

			while ((earg = strsep(&argv[i+1], ",")) != NULL) {
				if (strcmp(earg, "no-intra-emphasis") == 0) {
					extensions |= HOEDOWN_EXT_NO_INTRA_EMPHASIS;
				} else if (strcmp(earg, "tables") == 0) {
					extensions |= HOEDOWN_EXT_TABLES;
				} else if (strcmp(earg, "fenced-code") == 0) {
					extensions |= HOEDOWN_EXT_FENCED_CODE;
				} else if (strcmp(earg, "autolink") == 0) {
					extensions |= HOEDOWN_EXT_AUTOLINK;
				} else if (strcmp(earg, "strikethrough") == 0) {
					extensions |= HOEDOWN_EXT_STRIKETHROUGH;
				} else if (strcmp(earg, "underline") == 0) {
					extensions |= HOEDOWN_EXT_UNDERLINE;
				} else if (strcmp(earg, "space-headers") == 0) {
					extensions |= HOEDOWN_EXT_SPACE_HEADERS;
				} else if (strcmp(earg, "superscript") == 0) {
					extensions |= HOEDOWN_EXT_SUPERSCRIPT;
				} else if (strcmp(earg, "lax-spacing") == 0) {
					extensions |= HOEDOWN_EXT_LAX_SPACING;
				} else if (strcmp(earg, "disable-indented-code") == 0) {
					extensions |= HOEDOWN_EXT_DISABLE_INDENTED_CODE;
				} else if (strcmp(earg, "highlight") == 0) {
					extensions |= HOEDOWN_EXT_HIGHLIGHT;
				} else if (strcmp(earg, "footnotes") == 0) {
					extensions |= HOEDOWN_EXT_FOOTNOTES;
				} else if (strcmp(earg, "quote") == 0) {
					extensions |= HOEDOWN_EXT_QUOTE;
				}
			}
			i++;

		} else {
			/* opening the file if given from the command line */
			in = fopen(argv[i], "r");
			if (!in) {
				fprintf(stderr, "Unable to open input file \"%s\": %s\n",
				argv[i], strerror(errno));
				return 1;
			}
		}
	}

	/* reading everything */
	ib = hoedown_buffer_new(READ_UNIT);
	while (!feof(in) && !ferror(in)) {
		hoedown_buffer_grow(ib, ib->size + READ_UNIT);
		ib->size += fread(ib->data + ib->size, 1, READ_UNIT, in);
	}

	if (in != stdin)
		fclose(in);

	/* performing markdown parsing */
	ob = hoedown_buffer_new(OUTPUT_UNIT);

	renderer = hoedown_html_renderer_new(0, 0);
	markdown = hoedown_markdown_new(extensions, 16, renderer);

	hoedown_markdown_render(ob, ib->data, ib->size, markdown);

	hoedown_markdown_free(markdown);
	hoedown_html_renderer_free(renderer);

	/* writing the result to stdout */
	(void)fwrite(ob->data, 1, ob->size, stdout);

	/* cleanup */
	hoedown_buffer_free(ib);
	hoedown_buffer_free(ob);

	return ferror(stdout);
}
