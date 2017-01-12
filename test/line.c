#include <stdio.h>
#include <unistd.h>
#include "../src/html.h"

typedef struct {
    char *content;
    size_t length;
} RawBuffer;

void do_printing(const char *name, size_t line) {
    printf("%s => line:%zu\n", name, line);
}

void blockcode(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *lang,
           const hoedown_renderer_data *data, size_t line) {
    do_printing("blockcode", line);
}

void blockquote(hoedown_buffer *ob, const hoedown_buffer *text,
                const hoedown_renderer_data *data, size_t line) {
    do_printing("blockquote", line);
}

void header(hoedown_buffer *ob, const hoedown_buffer *text, int level,
            const hoedown_renderer_data *data, size_t line) {
    do_printing("header", line);
}

void list(hoedown_buffer *ob, const hoedown_buffer *text, hoedown_list_flags flags,
          const hoedown_renderer_data *data, size_t line) {
    do_printing("list", line);
}

void listitem(hoedown_buffer *ob, const hoedown_buffer *text, hoedown_list_flags flags,
              const hoedown_renderer_data *data, size_t line) {
    do_printing("listitem", line);
}

void paragraph(hoedown_buffer *ob, const hoedown_buffer *text,
               const hoedown_renderer_data *data, size_t line) {
    do_printing("paragraph", line);
}

void table(hoedown_buffer *ob, const hoedown_buffer *text,
           const hoedown_renderer_data *data, size_t line) {
    do_printing("table", line);
}

void table_header(hoedown_buffer *ob, const hoedown_buffer *text,
                  const hoedown_renderer_data *data, size_t line) {
    do_printing("table_header", line);
}

void table_body(hoedown_buffer *ob, const hoedown_buffer *text,
                const hoedown_renderer_data *data, size_t line) {
    do_printing("table_body", line);
}

void table_row(hoedown_buffer *ob, const hoedown_buffer *text,
               const hoedown_renderer_data *data, size_t line) {
    do_printing("table_row", line);
}

void table_cell(hoedown_buffer *ob, const hoedown_buffer *text,
                hoedown_table_flags flags, const hoedown_renderer_data *data, size_t line) {
    do_printing("table_cell", line);
}

void footnotes(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_renderer_data *data, size_t line) {
    do_printing("footnotes", line);
}

void footnote_def(hoedown_buffer *ob, const hoedown_buffer *text, unsigned int num,
                  const hoedown_renderer_data *data, size_t line) {
    do_printing("footnote_def", line);
}

void blockhtml(hoedown_buffer *ob, const hoedown_buffer *text,
               const hoedown_renderer_data *data, size_t line) {
    do_printing("blockhtml", line);
}

int codespan(hoedown_buffer *ob, const hoedown_buffer *text,
             const hoedown_renderer_data *data, size_t line) {
    do_printing("codespan", line);
    return 0;
}

int double_emphasis(hoedown_buffer *ob, const hoedown_buffer *text,
                    const hoedown_renderer_data *data, size_t line) {
    do_printing("double_emphasis", line);
    return 0;
}

int emphasis(hoedown_buffer *ob, const hoedown_buffer *text,
             const hoedown_renderer_data *data, size_t line) {
    do_printing("emphasis", line);
    return 0;
}

int underline(hoedown_buffer *ob, const hoedown_buffer *text,
              const hoedown_renderer_data *data, size_t line) {
    do_printing("underline", line);
    return 0;
}

int highlight(hoedown_buffer *ob, const hoedown_buffer *text,
              const hoedown_renderer_data *data, size_t line) {
    do_printing("highlight", line);
    return 0;
}

int quote(hoedown_buffer *ob, const hoedown_buffer *text,
          const hoedown_renderer_data *data, size_t line) {
    do_printing("quote", line);
    return 0;
}

int image(hoedown_buffer *ob, const hoedown_buffer *link, const hoedown_buffer *title,
          const hoedown_buffer *alt, const hoedown_renderer_data *data, size_t line) {
    do_printing("image", line);
    return 0;
}

int _link(hoedown_buffer *ob, const hoedown_buffer *text, const hoedown_buffer *link,
          const hoedown_buffer *title, const hoedown_renderer_data *data, size_t line) {
    do_printing("link", line);
    return 0;
}

int triple_emphasis(hoedown_buffer *ob, const hoedown_buffer *text,
                    const hoedown_renderer_data *data, size_t line) {
    do_printing("triple_emphasis", line);
    return 0;
}

int strikethrough(hoedown_buffer *ob, const hoedown_buffer *text,
                  const hoedown_renderer_data *data, size_t line) {
    do_printing("strikethrough", line);
    return 0;
}

int superscript(hoedown_buffer *ob, const hoedown_buffer *text,
                const hoedown_renderer_data *data, size_t line) {
    do_printing("superscript", line);
    return 0;
}

int math(hoedown_buffer *ob, const hoedown_buffer *text, int displaymode,
         const hoedown_renderer_data *data, size_t line) {
    do_printing("math", line);
    return 0;
}

int raw_html(hoedown_buffer *ob, const hoedown_buffer *text,
             const hoedown_renderer_data *data, size_t line) {
    do_printing("raw_html", line);
    return 0;
}

int get_file_content(RawBuffer *buffer, char *file_name) {
    FILE *fp = fopen(file_name, "r");

    if (!fp) {
        fprintf(stderr, "Cannot read file: '%s'\n", file_name);
        return 2;
    }
    fseek(fp, 0, SEEK_END);
    buffer->length = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (!(buffer->content = malloc(buffer->length))) {
        fprintf(stderr, "%s\n", "Malloc failed...");
        fclose(fp);
        return 3;
    }
    fread(buffer->content, 1, buffer->length, fp);
    fclose(fp);
    return 0;
}

int main(int ac, char **av) {
    if (ac != 2) {
        fprintf(stderr, "%s\n", "A file name is expected.");
        return 1;
    }
    RawBuffer buffer = {0, 0};
    int ret = get_file_content(&buffer, av[1]);
    if (ret) {
        return ret;
    }
    hoedown_renderer *renderer = hoedown_html_renderer_new(0, 0);
    renderer->blockcode = blockcode;
    renderer->blockquote = blockquote;
    renderer->header = header;
    renderer->list = list;
    renderer->listitem = listitem;
    renderer->paragraph = paragraph;
    renderer->table = table;
    renderer->table_header = table_header;
    renderer->table_cell = table_cell;
    renderer->table_row = table_row;
    renderer->table_body = table_body;
    renderer->footnotes = footnotes;
    renderer->footnote_def = footnote_def;
    renderer->blockhtml = blockhtml;
    renderer->codespan = codespan;
    renderer->double_emphasis = double_emphasis;
    renderer->emphasis = emphasis;
    renderer->underline = underline;
    renderer->highlight = highlight;
    renderer->quote = quote;
    renderer->image = image;
    renderer->link = _link;
    renderer->triple_emphasis = triple_emphasis;
    renderer->strikethrough = strikethrough;
    renderer->superscript = superscript;
    renderer->math = math;
    renderer->raw_html = raw_html;
    hoedown_document *document = hoedown_document_new(renderer, 0, 16);
    hoedown_buffer *html = hoedown_buffer_new(16);
    hoedown_document_render(document, html, (const uint8_t*)buffer.content, buffer.length);
    free(buffer.content);
    hoedown_buffer_free(html);
    hoedown_document_free(document);
    hoedown_html_renderer_free(renderer);
    return 0;
}
