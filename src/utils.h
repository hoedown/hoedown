/* html.h - HTML renderer and utilities */

#ifndef SCIDOWN_UTILS_H
#define SCIDOWN_UTILS_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif


/*************
 * CONSTANTS *
 *************/

typedef enum scidown_render_flags {
	SCIDOWN_RENDER_SKIP_HTML  = (1 << 0),
  	SCIDOWN_RENDER_ESCAPE     = (1 << 1),
	SCIDOWN_RENDER_HARD_WRAP  = (1 << 2),
	SCIDOWN_RENDER_USE_XHTML  = (1 << 3),
	/* -- experimental flags -- */
	SCIDOWN_RENDER_MERMAID    = (1 << 4),
	SCIDOWN_RENDER_CHARTER    = (1 << 5),
	SCIDOWN_RENDER_GNUPLOT    = (1 << 6),
	SCIDOWN_RENDER_CSS        = (1 << 7),
} scidown_render_flags;

typedef enum scidown_render_tag {
	SCIDOWN_RENDER_TAG_NONE = 0,
	SCIDOWN_RENDER_TAG_OPEN,
	SCIDOWN_RENDER_TAG_CLOSE
} scidown_render_tag;


/*********
 * TYPES *
 *********/
typedef struct {
	char* figure;
	char* listing;
	char* table;
} localization;


struct {
	char *str;
	int   size;
	void *next;
} typedef Strings;


void     free_strings (Strings *head);
Strings* add_string   (Strings *head,
                       char    *str);

void     remove_char  (char    *source,
                       char     target);
char*    clean_string (char    *string,
                       size_t  size);

#ifdef __cplusplus
}
#endif

#endif /** SCIDOWN_UTILS_H **/
