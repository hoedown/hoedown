
#ifndef __SCIDOWN_CONSTANT_H__
#define __SCIDOWN_CONSTANT_H__


#define A0_STRING    "a0paper"
#define A1_STRING    "a1paper"
#define A2_STRING    "a2paper"
#define A3_STRING    "a3paper"
#define A4_STRING    "a4paper"
#define A5_STRING    "a5paper"
#define A6_STRING    "a6paper"

#define A0_STR       "A0"
#define A1_STR       "A1"
#define A2_STR       "A2"
#define A3_STR       "A3"
#define A4_STR       "A4"
#define A5_STR       "A5"
#define A6_STR       "A6"

#define A0_STRGTK    "iso_a0"
#define A1_STRGTK    "iso_a1"
#define A2_STRGTK    "iso_a2"
#define A3_STRGTK    "iso_a3"
#define A4_STRGTK    "iso_a4"
#define A5_STRGTK    "iso_a5"
#define A6_STRGTK    "iso_a6"

#define B0_STRING    "b0paper"
#define B1_STRING    "b1paper"
#define B2_STRING    "b2paper"
#define B3_STRING    "b3paper"
#define B4_STRING    "b4paper"
#define B5_STRING    "b5paper"
#define B6_STRING    "b6paper"

#define B0_STR       "B0"
#define B1_STR       "B1"
#define B2_STR       "B2"
#define B3_STR       "B3"
#define B4_STR       "B4"
#define B5_STR       "B5"
#define B6_STR       "B6"

#define B0_STRGTK    "iso_b0"
#define B1_STRGTK    "iso_b1"
#define B2_STRGTK    "iso_b2"
#define B3_STRGTK    "iso_b3"
#define B4_STRGTK    "iso_b4"
#define B5_STRGTK    "iso_b5"
#define B6_STRGTK    "iso_b6"

#define C0_STRING    "c0paper"
#define C1_STRING    "c1paper"
#define C2_STRING    "c2paper"
#define C3_STRING    "c3paper"
#define C4_STRING    "c4paper"
#define C5_STRING    "c5paper"
#define C6_STRING    "c6paper"

#define C0_STR       "C0"
#define C1_STR       "C1"
#define C2_STR       "C2"
#define C3_STR       "C3"
#define C4_STR       "C4"
#define C5_STR       "C5"
#define C6_STR       "C6"

#define C0_STRGTK    "iso_c0"
#define C1_STRGTK    "iso_c1"
#define C2_STRGTK    "iso_c2"
#define C3_STRGTK    "iso_c3"
#define C4_STRGTK    "iso_c4"
#define C5_STRGTK    "iso_c5"
#define C6_STRGTK    "iso_c6"

#define B0J_STRING   "b0j"
#define B1J_STRING   "b1j"
#define B2J_STRING   "b2j"
#define B3J_STRING   "b3j"
#define B4J_STRING   "b4j"
#define B5J_STRING   "b5j"
#define B6J_STRING   "b6j"

#define B0J_STR      "b0j"
#define B1J_STR      "b1j"
#define B2J_STR      "b2j"
#define B3J_STR      "b3j"
#define B4J_STR      "b4j"
#define B5J_STR      "b5j"
#define B6J_STR      "b6j"

#define B0J_STRGTK   "jis_b0"
#define B1J_STRGTK   "jis_b1j"
#define B2J_STRGTK   "jis_b2j"
#define B3J_STRGTK   "jis_b3j"
#define B4J_STRGTK   "jis_b4j"
#define B5J_STRGTK   "jis_b5j"
#define B6J_STRGTK   "jis_b6j"


#define B43_STR      "4:3"
#define B169_STR     "16:9"

#define LETTE_STRING "letterpaper"
#define EXECU_STRING "executivepaper"
#define LEGAL_STRING "legalpaper"

#define LETTE_STR    "letter"
#define EXECU_STR    "executive"
#define LEGAL_STR    "legal"

#define LETTE_STRGTK "na_letter"
#define EXECU_STRGTK "na_executive"
#define LEGAL_STRGTK "na_legal"

#define C_PRO_STRING "proc"
#define C_ART_STRING "article"
#define C_BOO_STRING "book"
#define C_MIN_STRING "minimal"
#define C_REP_STRING "report"
#define C_SLI_STRING "slides"
#define C_MEM_STRING "memoir"
#define C_BEA_STRING "beamer"


typedef enum scidown_paper_size {
	A0PAPER,
	A1PAPER,
	A2PAPER,
	A3PAPER,
	A4PAPER,
	A5PAPER,
	A6PAPER,

	B0PAPER,
	B1PAPER,
	B2PAPER,
	B3PAPER,
	B4PAPER,
	B5PAPER,
	B6PAPER,

	C0PAPER,
	C1PAPER,
	C2PAPER,
	C3PAPER,
	C4PAPER,
	C5PAPER,
	C6PAPER,

	B0J,
	B1J,
	B2J,
	B3J,
	B4J,
	B5J,
	B6J,

	B43,
	B169,

	LETTERPAPER,
	EXECUTIVEPAPER,
	LEGALPAPER
} scidow_paper_size;

typedef enum scidown_doc_class {
    CLASS_PROC,
    CLASS_ARTICLE,
    CLASS_BOOK,
    CLASS_MINIMAL,
    CLASS_REPORT,
    CLASS_SLIDES,
    CLASS_MEMOIR,
    CLASS_BEAMER
} scidown_doc_class;

scidow_paper_size latex_to_paper  (char *            str);
char*             paper_to_latex  (scidow_paper_size size);
scidow_paper_size string_to_paper (char *            str);
char*             paper_to_string (scidow_paper_size size);
scidow_paper_size gtkstr_to_paper (char *            str);
char*             paper_to_gtkstr (scidow_paper_size size);


scidown_doc_class string_to_class (char *            str);
char*             class_to_string (scidown_doc_class cls);

#endif
