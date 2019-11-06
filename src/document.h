/* document.h - generic markdown parser */

#ifndef HOEDOWN_DOCUMENT_H
#define HOEDOWN_DOCUMENT_H

#include "buffer.h"
#include "autolink.h"
#include "utils.h"
#include "constants.h"

#ifdef __cplusplus
extern "C" {
#endif


/*************
 * CONSTANTS *
 *************/

typedef enum hoedown_extensions {
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
	HOEDOWN_EXT_MATH = (1 << 9),


	/* other flags */
	HOEDOWN_EXT_NO_INTRA_EMPHASIS = (1 << 11),
	HOEDOWN_EXT_SPACE_HEADERS = (1 << 12),
	HOEDOWN_EXT_MATH_EXPLICIT = (1 << 13),

	/* experimental */
	HOEDOWN_EXT_SCI  = (1<<14),

	/* negative flags */
	HOEDOWN_EXT_DISABLE_INDENTED_CODE = (1 << 15)
} hoedown_extensions;

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
	HOEDOWN_EXT_NO_INTRA_EMPHASIS |\
	HOEDOWN_EXT_SPACE_HEADERS |\
	HOEDOWN_EXT_MATH_EXPLICIT )

#define HOEDOWN_EXT_NEGATIVE (\
	HOEDOWN_EXT_DISABLE_INDENTED_CODE )

typedef enum hoedown_list_flags {
	HOEDOWN_LIST_ORDERED = (1 << 0),
	HOEDOWN_LI_BLOCK = (1 << 1)	/* <li> containing block data */
} hoedown_list_flags;

typedef enum hoedown_table_flags {
	HOEDOWN_TABLE_ALIGN_LEFT = 1,
	HOEDOWN_TABLE_ALIGN_RIGHT = 2,
	HOEDOWN_TABLE_ALIGN_CENTER = 3,
	HOEDOWN_TABLE_ALIGNMASK = 3,
	HOEDOWN_TABLE_HEADER = 4
} hoedown_table_flags;

typedef enum hoedown_autolink_type {
	HOEDOWN_AUTOLINK_NONE,		/* used internally when it is not an autolink*/
	HOEDOWN_AUTOLINK_NORMAL,	/* normal http/http/ftp/mailto/etc link */
	HOEDOWN_AUTOLINK_EMAIL		/* e-mail link without explit mailto: */
} hoedown_autolink_type;



/*********
 * TYPES *
 *********/

struct hoedown_document;
typedef struct hoedown_document hoedown_document;

typedef struct metadata {
	char              *title;
	Strings           *authors;
	char              *keywords;
	char              *style;
	char              *affiliation;
	scidow_paper_size  paper_size;
	scidown_doc_class  doc_class;
	int                font_size;
	int                numbering;
} metadata;

struct hoedown_renderer_data {
	void *opaque;
	metadata *meta;
};
typedef struct hoedown_renderer_data hoedown_renderer_data;


enum {
	FIGURE,
	TABLE,
	LISTING,
	EQUATION
}typedef float_type;

struct {
	char * id;
	char * caption;
	float_type type;
} typedef float_args;

struct {
	char * extra_header;
	char * extra_closing;
} typedef ext_definition;

struct {
	char * id;
	int32_t counter;
	float_type type;
	void * next;
} typedef reference;

struct
{
	uint32_t figure;
	uint32_t equation;
	uint32_t listing;
	uint32_t table;
}typedef html_counter;

struct
{
	int chapter;
	int section;
	int subsection;
}typedef h_counter;

struct
{
	int nesting;
	char * text;
	void * sibling;
}typedef toc;


/* hoedown_renderer - functions for rendering parsed data */
struct hoedown_renderer {
	/* state object */
	void *opaque;

	/* document level callbacks */
	void (*head)(hoedown_buffer *ob, metadata * doc_metadata, ext_definition * extensions);
	void (*title)(hoedown_buffer *ob, const hoedown_buffer *content,  const metadata *data);
	void (*authors)(hoedown_buffer *ob, Strings *authors);
	void (*affiliation)(hoedown_buffer *ob, const hoedown_buffer *content,  const hoedown_renderer_data *data);
	void (*keywords)(hoedown_buffer *ob, const hoedown_buffer *content,  const hoedown_renderer_data *data);
	void (*begin)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	void (*inner)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	void (*end)(hoedown_buffer *ob,  ext_definition * extensions, const hoedown_renderer_data *data);
	void (*pagebreak)(hoedown_buffer *ob);

	/* block level callbacks - NULL skips the block */
	void (*close)(hoedown_buffer *ob);
	void (*abstract)(hoedown_buffer *ob);
	void (*opn_equation)(hoedown_buffer *ob, const char * ref,const hoedown_renderer_data *data);
	void (*cls_equation)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	void (*open_float)(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data);
	void (*close_float)(hoedown_buffer *ob, float_args args, const hoedown_renderer_data *data);
	void (*blockcode)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang, const hoedown_renderer_data *data);
	void (*blockquote)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*header)(hoedown_buffer *ob, const hoedown_buffer *content, int level, const hoedown_renderer_data *data, h_counter counter, int numbering);
	void (*hrule)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	void (*list)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data);
	void (*listitem)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_list_flags flags, const hoedown_renderer_data *data);
	void (*paragraph)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data, hoedown_table_flags *flags, int columns);
	void (*table_header)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_body)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_row)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*table_cell)(hoedown_buffer *ob, const hoedown_buffer *content, hoedown_table_flags flags, const hoedown_renderer_data *data);
	void (*footnotes)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	void (*footnote_def)(hoedown_buffer *ob, const hoedown_buffer *content, unsigned int num, const hoedown_renderer_data *data);
	void (*blockhtml)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);
	void (*toc)(hoedown_buffer *ob, toc* ToC, int numbering);

	/* span level callbacks - NULL or return 0 prints the span verbatim */
	int (*autolink)(hoedown_buffer *ob, const hoedown_buffer *link, hoedown_autolink_type type, const hoedown_renderer_data *data);
	int (*codespan)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);
	int (*double_emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*underline)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*highlight)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*quote)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*image)(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_buffer *alt, const hoedown_renderer_data *data);
	int (*linebreak)(hoedown_buffer *ob, const hoedown_renderer_data *data);
	int (*link)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_buffer *link, const hoedown_buffer *title, const hoedown_renderer_data *data);
	int (*triple_emphasis)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*strikethrough)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*superscript)(hoedown_buffer *ob, const hoedown_buffer *content, const hoedown_renderer_data *data);
	int (*footnote_ref)(hoedown_buffer *ob, int num, const hoedown_renderer_data *data);
	int (*math)(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data);
	int (*eq_math)(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode, const hoedown_renderer_data *data);
	int (*ref)(hoedown_buffer *ob, char * id, int count);
	int (*raw_html)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);

	/* low level callbacks - NULL copies input directly into the output */
	void (*entity)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);
	void (*normal_text)(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data);

	/* miscellaneous callbacks */
	void (*doc_header)(hoedown_buffer *ob, int inline_render, const hoedown_renderer_data *data);
	void (*doc_footer)(hoedown_buffer *ob, int inline_render, const hoedown_renderer_data *data);
	
	/* position reference */
	void (*position)(hoedown_buffer *ob);
};
typedef struct hoedown_renderer hoedown_renderer;




/*************
 * FUNCTIONS *
 *************/

/* hoedown_document_new: allocate a new document processor instance */
hoedown_document *hoedown_document_new(
	const hoedown_renderer *renderer,
	hoedown_extensions extensions,
	ext_definition * exeternal_extensions,
    const char * base_folder,
	size_t max_nesting
) __attribute__ ((malloc));

/* hoedown_document_render: render regular Markdown using the document processor */
void hoedown_document_render(hoedown_document *doc, hoedown_buffer *ob, const uint8_t *data, size_t size, int position);

/* hoedown_document_render_inline: render inline Markdown using the document processor */
void hoedown_document_render_inline(hoedown_document *doc, hoedown_buffer *ob, const uint8_t *data, size_t size, int position);

/* hoedown_document_free: deallocate a document processor instance */
void hoedown_document_free(hoedown_document *doc);


metadata* document_metadata(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /** HOEDOWN_DOCUMENT_H **/
