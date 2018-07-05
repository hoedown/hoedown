/* latex.h - LaTeX renderer and utilities */

#ifndef HOEDOWN_LATEX_H
#define HOEDOWN_LATEX_H

#include "document.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/*************
 * CONSTANTS *
 *************/

typedef enum hoedown_latex {
	HOEDOWN_LATEX_HARD_WRAP = (1 << 0),
	HOEDOWN_LATEX_HEADER_ID = (1 << 1)
} hoedown_latex_flags;

/*********
 * TYPES *
 *********/

struct hoedown_latex_renderer_state {
	void *opaque;

	struct {
		int header_count;
		int current_level;
		int level_offset;
		int nesting_level;
	} toc_data;

	hoedown_latex_flags flags;

	/* extra callbacks */
	void (*link_attributes)(hoedown_buffer *ob, const hoedown_buffer *url, const hoedown_renderer_data *data);
};
typedef struct hoedown_latex_renderer_state hoedown_latex_renderer_state;


/*************
 * FUNCTIONS *
 *************/

/* hoedown_latex_renderer_new: allocates a LaTeX renderer */
hoedown_renderer *hoedown_latex_renderer_new(
	hoedown_latex_flags render_flags,
	int nesting_level
) __attribute__ ((malloc));

/* hoedown_latex_renderer_free: deallocate a LaTeX renderer */
void hoedown_latex_renderer_free(hoedown_renderer *renderer);


#ifdef __cplusplus
}
#endif

#endif /** HOEDOWN_LATEX_H **/
