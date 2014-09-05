/* document.h - generic markdown parser */

#ifndef HOEDOWN_DOCUMENT_H
#define HOEDOWN_DOCUMENT_H

#include "buffer.h"
#include "autolink.h"

#ifdef __cplusplus
extern "C" {
#endif

/*********
 * FLAGS *
 *********/

enum hoedown_extensions {
	/* block-level extensions */
	HOEDOWN_EXT_TABLES = (1 << 0),
	HOEDOWN_EXT_FENCED_CODE = (1 << 1),
	HOEDOWN_EXT_FOOTNOTES = (1 << 2),

	/* span-level extensions */
	HOEDOWN_EXT_AUTOLINK = (1 << 3),
	HOEDOWN_EXT_STRIKETHROUGH = (1 << 4),
	HOEDOWN_EXT_UNDERLINE = (1 << 5),
	HOEDOWN_EXT_HIGHLIGHT = (1 << 6),
	HOEDOWN_EXT_QUOTE = (1 << 7),
	HOEDOWN_EXT_SUPERSCRIPT = (1 << 8),
	HOEDOWN_EXT_MATH = (1 << 13),

	/* other flags */
	HOEDOWN_EXT_LAX_SPACING = (1 << 9),
	HOEDOWN_EXT_NO_INTRA_EMPHASIS = (1 << 10),
	HOEDOWN_EXT_SPACE_HEADERS = (1 << 11),
	HOEDOWN_EXT_MATH_EXPLICIT = (1 << 14),

	/* negative flags */
	HOEDOWN_EXT_DISABLE_INDENTED_CODE = (1 << 12)
};

#define HOEDOWN_EXT_BLOCK (\
	HOEDOWN_EXT_TABLES |\
	HOEDOWN_EXT_FENCED_CODE |\
	HOEDOWN_EXT_FOOTNOTES )

#define HOEDOWN_EXT_SPAN (\
	HOEDOWN_EXT_AUTOLINK |\
	HOEDOWN_EXT_STRIKETHROUGH |\
	HOEDOWN_EXT_UNDERLINE |\
	HOEDOWN_EXT_HIGHLIGHT |\
	HOEDOWN_EXT_QUOTE |\
	HOEDOWN_EXT_SUPERSCRIPT |\
	HOEDOWN_EXT_MATH )

#define HOEDOWN_EXT_FLAGS (\
	HOEDOWN_EXT_LAX_SPACING |\
	HOEDOWN_EXT_NO_INTRA_EMPHASIS |\
	HOEDOWN_EXT_SPACE_HEADERS |\
	HOEDOWN_EXT_MATH_EXPLICIT )

#define HOEDOWN_EXT_NEGATIVE (\
	HOEDOWN_EXT_DISABLE_INDENTED_CODE )

/* list/listitem flags */
enum hoedown_listflags {
	HOEDOWN_LIST_ORDERED = (1 << 0),
	HOEDOWN_LI_BLOCK = (1 << 1)	/* <li> containing block data */
};

enum hoedown_tableflags {
	HOEDOWN_TABLE_ALIGN_LEFT = 1,
	HOEDOWN_TABLE_ALIGN_RIGHT = 2,
	HOEDOWN_TABLE_ALIGN_CENTER = 3,
	HOEDOWN_TABLE_ALIGNMASK = 3,
	HOEDOWN_TABLE_HEADER = 4
};

/* hoedown_autolink - type of autolink */
enum hoedown_autolink {
	HOEDOWN_AUTOLINK_NONE,		/* used internally when it is not an autolink*/
	HOEDOWN_AUTOLINK_NORMAL,	/* normal http/http/ftp/mailto/etc link */
	HOEDOWN_AUTOLINK_EMAIL		/* e-mail link without explit mailto: */
};

/********************
 * TYPE DEFINITIONS *
 ********************/

/* hoedown_renderer - functions for rendering parsed data */
struct hoedown_renderer {
	/* state object */
	void *opaque;

	/* block level callbacks - NULL skips the block */
	void (*blockcode)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, void *opaque);
	void (*blockquote)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	void (*blockhtml)(hoedown_buffer *ob,const  hoedown_buffer *text, void *opaque);
	void (*header)(hoedown_buffer *ob, const hoedown_buffer *text, int level, void *opaque);
	void (*hrule)(hoedown_buffer *ob, void *opaque);
	void (*list)(hoedown_buffer *ob, const hoedown_buffer *text, unsigned int flags, void *opaque);
	void (*listitem)(hoedown_buffer *ob, const hoedown_buffer *text, unsigned int flags, void *opaque);
	void (*paragraph)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	void (*table)(hoedown_buffer *ob, const hoedown_buffer *header, const hoedown_buffer *body, void *opaque);
	void (*table_row)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	void (*table_cell)(hoedown_buffer *ob, const hoedown_buffer *text, unsigned int flags, void *opaque);
	void (*footnotes)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	void (*footnote_def)(hoedown_buffer *ob, const hoedown_buffer *text, unsigned int num, void *opaque);

	/* span level callbacks - NULL or return 0 prints the span verbatim */
	int (*autolink)(hoedown_buffer *ob, const hoedown_buffer *link, enum hoedown_autolink type, void *opaque);
	int (*codespan)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*double_emphasis)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*emphasis)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*underline)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*highlight)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*quote)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*image)(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, void *opaque);
	int (*linebreak)(hoedown_buffer *ob, void *opaque);
	int (*link)(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *content, void *opaque);
	int (*raw_html_tag)(hoedown_buffer *ob, const hoedown_buffer *tag, void *opaque);
	int (*triple_emphasis)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*strikethrough)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*superscript)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);
	int (*footnote_ref)(hoedown_buffer *ob, unsigned int num, void *opaque);
	int (*math)(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, void *opaque);

	/* low level callbacks - NULL copies input directly into the output */
	void (*entity)(hoedown_buffer *ob, const hoedown_buffer *entity, void *opaque);
	void (*normal_text)(hoedown_buffer *ob, const hoedown_buffer *text, void *opaque);

	/* header and footer */
	void (*doc_header)(hoedown_buffer *ob, void *opaque);
	void (*doc_footer)(hoedown_buffer *ob, void *opaque);
};

typedef struct hoedown_renderer hoedown_renderer;

struct hoedown_document;

typedef struct hoedown_document hoedown_document;

/**********************
 * EXPORTED FUNCTIONS *
 **********************/

extern hoedown_document *
hoedown_document_new(
	const hoedown_renderer *renderer,
	unsigned int extensions,
	size_t max_nesting);

extern size_t
hoedown_document_locate(hoedown_document *doc, const uint8_t *character, size_t *position);

extern void
hoedown_document_render(hoedown_document *doc, hoedown_buffer *ob, hoedown_buffer *text, const uint8_t *document, size_t doc_size);

extern void
hoedown_document_render_inline(hoedown_document *doc, hoedown_buffer *ob, const uint8_t *document, size_t doc_size);

extern void
hoedown_document_free(hoedown_document *doc);

#ifdef __cplusplus
}
#endif

#endif /** HOEDOWN_DOCUMENT_H **/
