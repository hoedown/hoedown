#include "document.h"

#include "pool.h"
#include "escape.h"

#include <string.h>
#include <assert.h>

#include "_case_folding.h"


#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)



// TYPE DEFINITION
// ===============
//
// Define the various types used across the code. As a convention, stacks,
// pools, tables, counters and other fields are initialized once in
// the constructor, then reset to their initial state at the end of a render.
//
// Head to the corresponding sections for detailed explanations of
// what each type does.

#define LINK_REFS_TABLE_SIZE 16


typedef size_t (*char_trigger)(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size);

typedef enum parsing_mode {
  DUMB_PARSING,
  MARKER_PARSING,
  NORMAL_PARSING
} parsing_mode;

typedef struct block_char_entry {
  struct block_char_entry *next;

  char_trigger trigger;
  int can_interrupt;
  hoedown_features lazy_ft;
} block_char_entry;

typedef struct inline_char_entry {
  struct inline_char_entry *next;

  char_trigger trigger;
} inline_char_entry;

typedef struct inline_nesting {
  struct inline_nesting *previous;

  void *parent;
  hoedown_features ft;
  size_t parsed;
  size_t start;
  size_t end;
} inline_nesting;

typedef struct inline_data {
  struct inline_data *previous;

  void *target;
  const uint8_t *data;
  inline_nesting *nesting;

  size_t unbalanced_brackets;
  size_t link_not_found;
  int cdata_not_found;
  int instruction_not_found;
  int declaration_not_found;
  int latex_inline_math_not_found;
  int latex_block_math_not_found;
  int tex_inline_math_not_found;
  int tex_block_math_not_found;
} inline_data;

typedef struct link_ref {
  struct link_ref *next;

  unsigned int id;
  hoedown_buffer dest_src;
  hoedown_buffer title_src;
  hoedown_buffer *dest;
  hoedown_buffer *title;
  int has_title;
} link_ref;

typedef struct buffer_mapping {
  struct buffer_mapping *previous;

  hoedown_buffer *buffer;
  hoedown_list ranges;
  size_t size;

  size_t last_i;
  size_t last_position;
} buffer_mapping;

struct hoedown_document {
  // Renderer stuff
  hoedown_renderer rndr;
  hoedown_renderer_data data;

  // Common parsing
  hoedown_features ft;
  size_t current_nesting;
  size_t max_nesting;

  // Block parsing
  hoedown_pool block_buffers;
  block_char_entry *block_chars [256];
  hoedown_pool block_chars__pool;
  parsing_mode mode;
  size_t is_tight;
  size_t inside_footnote;

  // Inline parsing
  hoedown_pool inline_buffers;
  inline_char_entry *inline_chars [256];
  hoedown_pool inline_chars__pool;
  inline_data *inline_data;
  hoedown_pool inline_data__pool;
  hoedown_pool inline_nesting__pool;
  size_t inside_link;
  size_t plain_links_forbidden;

  // Marker parsing
  link_ref *link_refs [LINK_REFS_TABLE_SIZE];
  hoedown_pool link_refs__pool;

  // Other features: source mapping
  buffer_mapping *mapping;
  hoedown_pool mapping__pool;
};



// TYPE UTILITTIES
// ===============
//
// Constructors, destructors, and other logic for easy handling
// of tables, hashing and much more.

// ALLOCATION

#define SIMPLE_ALLOCATOR(TYPE)                                                \
  static void *_new_##TYPE(void *opaque) { return hoedown_malloc(sizeof(TYPE)); }

SIMPLE_ALLOCATOR(block_char_entry)
SIMPLE_ALLOCATOR(inline_char_entry)
SIMPLE_ALLOCATOR(inline_data)
SIMPLE_ALLOCATOR(inline_nesting)

static void _free_pool_item(void *item, void *opaque) {
  free(item);
}


static void *_new_buffer_mapping(void *opaque) {
  buffer_mapping *mapping = hoedown_malloc(sizeof(buffer_mapping));
  mapping->previous = NULL;
  mapping->buffer = NULL;
  hoedown_list_init(&mapping->ranges, sizeof(hoedown_range), 2);
  return mapping;
}

static void _free_buffer_mapping(void *item, void *opaque) {
  buffer_mapping *mapping = item;
  hoedown_list_uninit(&mapping->ranges);
  free(item);
}


// CHAR TRIGGERS

static void register_block_char(hoedown_document *doc, uint8_t c, char_trigger trigger, int can_interrupt, hoedown_features lazy_ft) {
  block_char_entry *entry = hoedown_pool_get(&doc->block_chars__pool);

  entry->next = NULL;
  entry->trigger = trigger;
  entry->can_interrupt = can_interrupt;
  entry->lazy_ft = lazy_ft;

  block_char_entry **slot = &doc->block_chars[c];
  while (*slot) slot = &(*slot)->next;
  *slot = entry;
}

static void register_inline_char(hoedown_document *doc, uint8_t c, char_trigger trigger) {
  inline_char_entry *entry = hoedown_pool_get(&doc->inline_chars__pool);

  entry->next = NULL;
  entry->trigger = trigger;

  inline_char_entry **slot = &doc->inline_chars[c];
  while (*slot) slot = &(*slot)->next;
  *slot = entry;
}

static void register_block_chars(hoedown_document *doc, const char *chars, char_trigger trigger, int can_interrupt, hoedown_features lazy_ft) {
  if (chars) {
    for (; *chars; chars++)
      register_block_char(doc, *chars, trigger, can_interrupt, lazy_ft);
  } else {
    int c;
    for (c = 0; c < 256; c++)
      register_block_char(doc, c, trigger, can_interrupt, lazy_ft);
  }
}

static void register_inline_chars(hoedown_document *doc, const char *chars, char_trigger trigger) {
  for (; *chars; chars++)
    register_inline_char(doc, *chars, trigger);
}

static void reset_block_chars(hoedown_document *doc) {
  size_t i;
  block_char_entry *entry;
  for (i = 0; i < 256; i++) {
    for (entry = doc->block_chars[i]; entry; entry = entry->next)
      hoedown_pool_pop(&doc->block_chars__pool, entry);
  }
}

static void reset_inline_chars(hoedown_document *doc) {
  size_t i;
  inline_char_entry *entry;
  for (i = 0; i < 256; i++) {
    for (entry = doc->inline_chars[i]; entry; entry = entry->next)
      hoedown_pool_pop(&doc->inline_chars__pool, entry);
  }
}


// HASHING / NORMALIZATION

static inline size_t normalize_case_next(struct case_mapping *mapping, const uint8_t *data, size_t size) {
  size_t i = 1;

  // Collect continuation characters
  while (i < size && (data[i] & 0xc0) == 0x80) i++;

  // Lookup case mapping, return original data if not found
  const struct case_mapping *omapping = find_case_mapping((const char *)data, i);
  if (omapping == NULL) {
    mapping->value = (const char *)data;
    mapping->length = i;
  } else {
    mapping->value = omapping->value;
    mapping->length = omapping->length;
  }

  return i;
}

static inline unsigned int hash_string(const uint8_t *data, size_t size) {
  unsigned int hash = 0;
  size_t i = 0;
  struct case_mapping mapping;

  while (i < size) {
    uint8_t c = data[i];
    if (c >= 0xc0) {
      i += normalize_case_next(&mapping, data + i, size - i);
      for (size_t e = 0; e < mapping.length; e++)
        hash = mapping.value[e] + (hash << 6) + (hash << 16) - hash;
    } else {
      if (c >= 'A' && c <= 'Z') c += 0x20;
      hash = c + (hash << 6) + (hash << 16) - hash;
      i++;
    }
  }

  return hash;
}


// LINK REFS

static void *_new_link_ref(void *opaque) {
  link_ref *ref = hoedown_malloc(sizeof(link_ref));
  memset(&ref->dest_src, 0, sizeof(ref->dest_src));
  memset(&ref->title_src, 0, sizeof(ref->title_src));
  ref->dest = hoedown_buffer_new(16);
  ref->title = hoedown_buffer_new(16);
  return ref;
}

static inline void add_link_ref(hoedown_document *doc, link_ref *ref) {
  link_ref **slot = &doc->link_refs[ref->id % LINK_REFS_TABLE_SIZE];
  ref->next = *slot;
  *slot = ref;
}

static inline link_ref *find_link_ref(hoedown_document *doc, unsigned int id) {
  link_ref *ref;

  for (ref = doc->link_refs[id % LINK_REFS_TABLE_SIZE]; ref; ref = ref->next)
    if (ref->id == id) return ref;

  return NULL;
}

static inline void pop_link_refs(hoedown_document *doc) {
  link_ref *ref;
  size_t i;

  for (i = 0; i < LINK_REFS_TABLE_SIZE; i++) {
    for (ref = doc->link_refs[i]; ref; ref = ref->next)
      hoedown_pool_pop(&doc->link_refs__pool, ref);
  }
}


// INLINE DATA

static inline void open_inline_data(hoedown_document *doc, void *target, const uint8_t *data) {
  inline_data *inline_data = hoedown_pool_get(&doc->inline_data__pool);
  inline_data->previous = doc->inline_data;
  doc->inline_data = inline_data;

  inline_data->target = target;
  inline_data->data = data;
  inline_data->nesting = NULL;

  inline_data->unbalanced_brackets = 0;
  inline_data->link_not_found = 0;
  inline_data->cdata_not_found = 0;
  inline_data->instruction_not_found = 0;
  inline_data->declaration_not_found = 0;
  inline_data->latex_inline_math_not_found = 0;
  inline_data->latex_block_math_not_found = 0;
  inline_data->tex_inline_math_not_found = 0;
  inline_data->tex_block_math_not_found = 0;
}

static inline void close_inline_data(hoedown_document *doc, void *target) {
  inline_data *inline_data = doc->inline_data;

  assert(inline_data->target == target);
  assert(inline_data->nesting == NULL);

  doc->inline_data = inline_data->previous;
  hoedown_pool_pop(&doc->inline_data__pool, inline_data);
}


// SOURCE MAPPING

// Get a buffer from a pool, and create a mapping for it.
static inline hoedown_buffer *get_mapped_buffer(hoedown_document *doc, hoedown_pool *pool) {
  hoedown_buffer *buffer = hoedown_pool_get(pool);

  buffer_mapping *mapping = hoedown_pool_get(&doc->mapping__pool);
  mapping->previous = doc->mapping;
  mapping->buffer = buffer;
  mapping->ranges.size = 0;
  mapping->size = 0;
  mapping->last_i = 0;
  mapping->last_position = 0;
  doc->mapping = mapping;
  return buffer;
}

// Return a buffer to its pool, and delete its mapping.
static inline void pop_mapped_buffer(hoedown_document *doc, hoedown_pool *pool, hoedown_buffer *buffer) {
  buffer_mapping *mapping = doc->mapping;
  assert(mapping->buffer == buffer);
  assert(mapping->size <= buffer->size);
  doc->mapping = doc->mapping->previous;
  hoedown_pool_pop(&doc->mapping__pool, mapping);
  hoedown_pool_pop(pool, buffer);
}

// Copy ranges covering start..end into another list.
// Returns position where the last copied range ends
static size_t copy_ranges(hoedown_list *dest, buffer_mapping *mapping, size_t start, size_t end) {
  size_t i = 0, count = mapping->ranges.size;
  size_t position = 0, mark;
  const hoedown_range *ranges = mapping->ranges.data;

  // Optimization: try to start from last looked up range
  if (mapping->last_position <= start) {
    i = mapping->last_i;
    position = mapping->last_position;
  }

  // Find the first pertinent range
  while (position <= start) {
    if (i >= count) return start;
    position += ranges[i].skip + ranges[i].size;
    i++;
  }

  // Skip previous ranges
  ranges += i-1;
  count -= i-1;

  // Find the last pertinent range
  mark = position - ranges[0].size;
  if (mark >= end) return start;
  for (i = 1; i < count; i++) {
    if (position + ranges[i].skip >= end) break;
    position += ranges[i].skip + ranges[i].size;
  }

  // Copy pertinent ranges to dest!
  hoedown_list_grow(dest, dest->size + i);
  hoedown_range *dranges = dest->data + dest->size*sizeof(hoedown_range);
  memcpy(dranges, ranges, i*sizeof(hoedown_range));
  dest->size += i;
  mapping->last_i = i-1 + mapping->ranges.size - count;
  mapping->last_position = position - (ranges[i-1].skip + ranges[i-1].size);

  // Adjust first range
  if (likely(mark < start)) {
    dranges[0].skip = 0;
    dranges[0].size -= start - mark;
    dranges[0].source += start - mark;
  } else {
    dranges[0].skip = start - mark;
  }

  // Adjust last range
  if (likely(position > end)) {
    dranges[i-1].size -= position - end;
    return end;
  }
  return position;
}

// Locate a string in `mapping` or previous ones.
static inline int locate_ranges(hoedown_list *ranges, const uint8_t *data, size_t size, buffer_mapping *mapping, size_t *span) {
  for (; mapping; mapping = mapping->previous) {
    // Check to see if the data chunk points to this buffer
    hoedown_buffer *buffer = mapping->buffer;
    if (data < buffer->data || data + size > buffer->data + buffer->size)
      continue;

    // Search for the first range to cover this chunk of data
    size_t start = data - buffer->data;
    size_t end = start + size;
    end = copy_ranges(ranges, mapping, start, end);
    if (span) *span = end - start;
    return 1;
  }
  return 0;
}

// Put a string into a mapping's buffer, and map it.
static inline void mapping_put(buffer_mapping *mapping, buffer_mapping *srcmapping, const uint8_t *data, size_t size) {
  hoedown_buffer *buffer = mapping->buffer;
  assert(mapping->size <= buffer->size);

  // Locate the data to put, append to current ranges
  size_t first = mapping->ranges.size, span;
  if (locate_ranges(&mapping->ranges, data, size, srcmapping, &span) && span) {
    hoedown_range *ranges = mapping->ranges.data;
    ranges[first].skip += buffer->size - mapping->size;
    mapping->size = buffer->size + size;
  }

  // Put the data into the buffer
  hoedown_buffer_put(buffer, data, size);
}

// Update a mapping as necessary to rewind the buffer to a certain size
static inline void mapping_rewind(buffer_mapping *mapping, size_t size) {
  hoedown_range *ranges = mapping->ranges.data;
  assert(mapping->buffer->size >= size);
  mapping->buffer->size = size;

  while (size < mapping->size) {
    hoedown_range *current = &ranges[mapping->ranges.size-1];
    if (size > mapping->size - current->size) {
      current->size -= mapping->size - size;
      break;
    }
    mapping->size -= current->size + current->skip;
    mapping->ranges.size--;
  }
}

// Put a string into a buffer, search for its mapping, and map it.
static inline void mapped_buffer_put(hoedown_document *doc, hoedown_buffer *buffer, const uint8_t *data, size_t size) {
  buffer_mapping *mapping;
  for (mapping = doc->mapping; mapping; mapping = mapping->previous) {
    // Is this the mapping for our buffer?
    if (mapping->buffer != buffer) continue;
    mapping_put(mapping, mapping->previous, data, size);
    return;
  }

  // We can't let this happen
  abort();
}

// Search for a buffer's mapping, and rewind it to the specified size
static inline void mapped_buffer_rewind(hoedown_document *doc, hoedown_buffer *buffer, size_t size) {
  buffer_mapping *mapping;
  for (mapping = doc->mapping; mapping; mapping = mapping->previous) {
    // Is this the mapping for our buffer?
    if (mapping->buffer != buffer) continue;
    mapping_rewind(mapping, size);
    return;
  }

  // We can't let this happen
  abort();
}


// OTHER

// Sets the data and size of a read-only buffer.
static inline void set_buffer_data(hoedown_buffer *buf, const uint8_t *data, size_t start, size_t end) {
  assert(!buf->unit);
  buf->data = (uint8_t *)data + start;
  buf->size = end - start;
}



// LOW LEVEL PARSING UTILITIES
// ===========================
//
// Very simple methods that help in parsing.

// Checks if the char is an ASCII digit.
static inline int is_digit_ascii(uint8_t ch) {
  return ch >= '0' && ch <= '9';
}

// Checks if the char is an uppercase ASCII letter.
static inline int is_upper_ascii(uint8_t ch) {
  return ch >= 'A' && ch <= 'Z';
}

// Checks if the char is an lowercase ASCII letter.
static inline int is_lower_ascii(uint8_t ch) {
  return ch >= 'a' && ch <= 'z';
}

// Checks if the char is an alphabetic ASCII letter.
static inline int is_alpha_ascii(uint8_t ch) {
  return is_lower_ascii(ch) || is_upper_ascii(ch);
}

// Checks if the char is an alphanumeric ASCII character.
static inline int is_alnum_ascii(uint8_t ch) {
  return is_alpha_ascii(ch) || is_digit_ascii(ch);
}

// Checks if the char is an ASCII punctuation character.
static inline int is_punct_ascii(uint8_t ch) {
  static const char punctuation_chars [256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  return punctuation_chars[ch];
}

// Checks if the char is an atext character,
// according to RFC 5322 section 3.2.3.
static inline int is_atext(uint8_t ch) {
  static const char atext_chars [256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  return atext_chars[ch];
}

// Checks if the char is a valid space character.
// Tabs and carriage returns are stripped during preprocessing.
// Form feeds aren't treated as spacing (except for HTML).
static inline int is_space(uint8_t ch) {
  return ch == 0x20 // space
      // ch == 0x09 // tab
      || ch == 0x0a // linefeed
      // ch == 0x0c // form feed
      // ch == 0x0d // carriage return
      ;
}

// Checks if the character at a certain position in the input
// is backslash escaped or not.
static inline int is_escaped(const uint8_t *data, size_t position) {
  size_t start = position;
  while (start > 0 && data[start-1] == '\\') start--;
  return (position - start) % 2;
}

// Replaces all uppercase ASCII letters in the string
// with their lowercase counterparts.
static inline void to_lower_ascii(uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (is_upper_ascii(data[i]))
      data[i] += 0x20;
  }
}

// Checks if the data passed is all spacing.
static inline int is_empty(const uint8_t *data, size_t size) {
  size_t i = 0;
  while (i < size && is_space(data[i])) i++;
  return i >= size;
}

// Returns position of the next line (or end of input, wathever happens before)
// but only if the current line is all spacing. Otherwise, zero is returned.
static inline size_t next_line_empty(const uint8_t *data, size_t size) {
  size_t i = 0;

  while (i < size && data[i] == ' ') i++;

  if (unlikely(i >= size)) return i;
  if (data[i] == '\n') return i + 1;
  return 0;
}

// Replaces all occurences of consecutive spacing characters with a single
// space character.
static inline void collapse_spacing(hoedown_buffer *ob, const uint8_t *data, size_t size) {
  size_t i = 0, mark = 0;
  while (1) {
    while (i < size && !is_space(data[i])) i++;

    // Optimization: it's a single space, we don't need to replace anything
    if (likely(i+1 < size && data[i] == ' ' && !is_space(data[i+1]))) {
      i += 2;
      continue;
    }

    // Optimization: there's nothing to replace
    if (mark == 0 && i >= size) {
      hoedown_buffer_put(ob, data, size);
      return;
    }

    hoedown_buffer_put(ob, data + mark, i - mark);
    if (i >= size) break;

    hoedown_buffer_putc(ob, ' ');
    i++;

    while (i < size && is_space(data[i])) i++;
    mark = i;
  }
}

// Unescapes ASCII punctuation characters escaped with a backslash.
static inline void unescape_backslash(hoedown_buffer *ob, const uint8_t *data, size_t size) {
  size_t i = 0, mark = 0;
  while (1) {
    while (i < size && data[i] != '\\') i++;

    // Optimization: there's nothing to unescape
    if (mark == 0 && i >= size) {
      hoedown_buffer_put(ob, data, size);
      return;
    }

    if (i+1 < size && !is_punct_ascii(data[i+1])) {
      i += 2;
      continue;
    }

    hoedown_buffer_put(ob, data + mark, i - mark);
    if (i >= size) break;

    i++;
    mark = i;
    i++;
  }
}

// Utility method to unescape both backslashes and HTML entities.
static inline void unescape_both(hoedown_document *doc, hoedown_buffer *ob, const uint8_t *data, size_t size) {
  hoedown_buffer *intermediate = hoedown_pool_get(&doc->inline_buffers);
  intermediate->size = 0;

  unescape_backslash(intermediate, data, size);
  hoedown_unescape_html(ob, intermediate->data, intermediate->size);

  hoedown_pool_pop(&doc->inline_buffers, intermediate);
}

// Preprocesses the input by normalizing linebreaks and expanding tabs to a four-character tabstop.
static inline void normalize_spacing(hoedown_document *doc, hoedown_buffer *ob, const uint8_t *data, size_t size) {
  size_t i = 0, line_start = ob->size, mark;
  static const uint8_t *tab = (const uint8_t *)"    ";
  hoedown_buffer_grow(ob, size);

  while (1) {
    mark = i;

    // Advance until we find a tab or different line ending, but keep multi-byte characters in mind
    while (1) {
      while (i < size && (data[i] & 0xc0) != 0x80 && data[i] != '\t' && data[i] != '\n' && data[i] != '\r') i++;
      if (i < size && (data[i] & 0xc0) == 0x80) {
        // this byte should not be counted
        i++; line_start++;
      } else if (i < size && data[i] == '\n') {
        // just start a new line and move on
        i++; line_start = ob->size + i - mark;
      } else break; // action needed for this character
    }

    // Copy accumulated data
    if (doc) mapped_buffer_put(doc, ob, data + mark, i - mark);
    else hoedown_buffer_put(ob, data + mark, i - mark);

    if (i >= size) break;

    // React to the character
    if (data[i] == '\t') {
      hoedown_buffer_put(ob, tab, 4 - (ob->size - line_start) % 4);
    } else {
      hoedown_buffer_putc(ob, '\n');
      line_start = ob->size;
    }
    i++;
  }
}



// HTML PARSING
// ============
//
// HTML parsing functions. This code has been adapted from Lanli,
// make sure to keep it up to date with latest master changes.

// HTML-specific version of is_space
static inline int html_is_space(uint8_t ch) {
  return ch == 0x20 // space
      // ch == 0x09 // tab
      || ch == 0x0a // linefeed
      || ch == 0x0c // form feed
      // ch == 0x0d // carriage return
  ;
}

// Checks if the char can be part of an attribute name.
// NOTE: CommonMark is stricter than HTML5.
static inline int html_is_attr_name_char(uint8_t ch) {
  // If we wanted to be 100% HTML-compliant, we should allow
  // more characters to be part of attribute names.
  return is_alnum_ascii(ch) || ch == '-' || ch == '_' || ch == ':' || ch == '.';
}

// Checks if the char is sensitive and can't be found inside
// unquoted attribute values, according to the HTML5 spec.
static inline int html_is_attr_sensitive(uint8_t ch) {
  return ch == '<' || ch == '>' || ch == '='
      || ch == '"' || ch == '`' || ch == '\'';
}

// Parse an attribute value if there's one, according to the HTML5 spec.
// Sets parsed value to the buffer passed.
// Returns 0 if there's no value, size of the value otherwise.
static size_t html_parse_attribute_value(const uint8_t *data, size_t size) {
  size_t i = 0, mark;
  while (i < size && html_is_space(data[i])) i++;

  if (!(i < size)) return 0;
  uint8_t delimiter = data[i];

  if (delimiter == '\'' || delimiter == '"') {
    i++;
    // Quoted attribute
    while (i < size && data[i] != delimiter) i++;
    if (likely(i < size)) return i + 1;
    return 0;
  }

  // Unquoted attribute
  mark = i;
  while (i < size && !html_is_space(data[i]) && !html_is_attr_sensitive(data[i])) i++;
  if (unlikely(mark == i)) return 0;

  return i;
}

// Parse an attribute if there's one, according to the HTML5 spec.
// Returns 0 if there's no valid attribute, size of the attribute otherwise.
static size_t html_parse_attribute(const uint8_t *data, size_t size) {
  size_t i = 0, mark;

  // There must be at least one space character as separation
  mark = i;
  while (i < size && html_is_space(data[i])) i++;
  if (mark == i) return 0;

  // Collect attribute name
  mark = i;
  while (i < size && html_is_attr_name_char(data[i])) i++;
  if (mark == i || data[mark] == '.' || data[mark] == '-') return 0;

  // Collect attribute value, if there is
  mark = i;
  while (i < size && html_is_space(data[i])) i++;
  if (i < size && data[i] == '=') {
    i++;
    // Attribute with value
    mark = i;
    i += html_parse_attribute_value(data + i, size - i);
    if (mark == i) return 0;
    return i;
  } else {
    // Attribute without value
    return mark;
  }

  // If we wanted to be 100% HTML-compliant, we should enforce
  // an unquoted attribute to have at least one space character
  // next, if it's followed by a slash.
}

// Parse a start tag if there's one, according to CommonMark.
// This method assumes that data[0] == '<', so check that before calling it.
// Returns 0 if there's no start tag, size of the tag otherwise.
// NOTE: CommonMark requires a tag name to start with a letter
static size_t html_parse_start_tag(hoedown_buffer *name, const uint8_t *data, size_t size) {
  size_t i = 1, mark;

  // Collect the tag name
  mark = i;

  if (i < size && (is_lower_ascii(data[i]) || is_upper_ascii(data[i]))) i++;
  else return 0;

  while (i < size && is_alnum_ascii(data[i])) i++;

  if (mark == i) return 0;
  if (name) hoedown_buffer_set(name, data + mark, i - mark);

  // Collect the attributes
  while (1) {
    mark = i;
    i += html_parse_attribute(data + i, size - i);
    if (mark == i) break;
  }

  // Collect optional spacing
  while (i < size && html_is_space(data[i])) i++;

  // Optional slash
  if (i < size && data[i] == '/') i++;

  // Ending angle bracket
  if (i < size && data[i] == '>')
    return i + 1;
  return 0;
}

// Parse an end tag if there's one, according to the HTML5 spec.
// This method assumes that data[0] == '<', so check that before calling it.
// Returns 0 if there's no end tag, size of the tag otherwise.
static size_t html_parse_end_tag(hoedown_buffer *name, const uint8_t *data, size_t size) {
  size_t i = 1, mark;

  // Slash
  if (i < size && data[i] == '/') i++;
  else return 0;

  // Collect tag name
  mark = i;
  while (i < size && is_alnum_ascii(data[i])) i++;
  if (mark == i) return 0;
  if (name) hoedown_buffer_set(name, data + mark, i - mark);

  // Collect optional spacing
  while (i < size && html_is_space(data[i])) i++;

  // Ending angle bracket
  if (i < size && data[i] == '>')
    return i + 1;
  return 0;
}

// Parse a comment if there's one, according to the HTML5 spec.
// This method assumes that data[0] == '<', so check that before calling it.
// Returns 0 if there's no comment, size of the comment otherwise.
static size_t html_parse_comment(const uint8_t *data, size_t size) {
  size_t i = 1;
  if (size < 7) return 0;

  // Ensure starting sequence
  if (data[1] == '!' && data[2] == '-' && data[3] == '-') i = 4;
  else return 0;

  // Validate start of comment content
  if (i+2 >= size) return 0;
  if (data[i] == '>') return 0;
  if (data[i] == '-' && data[i+1] == '>') return 0;

  // Collect content
  while (i+2 < size && !(data[i] == '-' && data[i+1] == '-')) i++;
  if (i+2 >= size) return 0;

  // Verify end of comment
  if (data[i+2] != '>') return 0;
  return i + 3;
}



// ACTUAL PARSING
// ==============
//
// Things start to get interesting! So called "parsing functions" follow.
// Parsing functions have the general form:
//
//      Returns end of the parsed construct (bytes parsed so far)
//      or zero if parsing failed due to illegal syntax
//      |
//     size_t parse_whatever(
//       hoedown_document *doc,    <-- document instance (if needed)
//       [output parameters],      <-- output parameters (optional)
//       const uint8_t *data,      <-- data to parse
//       size_t parsed,            <-- for char triggers only (read below)
//       size_t start,             <-- where to start parsing from (0 if not present)
//       size_t size,              <-- size of data
//       [input parameters]        <-- other input parameters (optional)
//     ) {...}
//
// Parsing functions are small, composable and have a clear target:
// parsing a determinate **construct** found immediately at the start of
// data. Example of constructs: a link, a code block, a list bullet.
//
// They declare a variable `i` initialized at `start` or `0` and start
// advancing through bytes in `data`, incrementing `i` and storing the
// parsed data as necessary. A parsing function should `return 0;` as soon
// as invalid syntax is found. Non-zero will be returned if (and only if)
// a complete, valid construct was found at that location and it could be
// parsed and stored successfully.
//
// There are several useful patterns used to code parsing functions, such
// as peeking, advancing and rewinding. You'll discover them as you read.
//
// ### Char triggers
//
// These are the most important parsing functions. They are associated with
// characters, and called by the parser when a construct starting with one of
// these characters is found. They're of the form:
//
//     void parse_whatever(hoedown_document *doc, void *target, const uint8_t *data,
//                         size_t parsed, size_t start, size_t size) {...}
//
// Imagine we're parsing this inline data:
//
//     `code` [link]()
//
// The first character is a backtick. It'll look up the char trigger associated
// with that character, which is `parse_code`. Thus, it'll call
// `parse_code` with the parameters:
//
//     doc:      <the doc>
//     target:   <some target>
//     data:     "`code` [link]()"
//     parsed:   0
//     start:    0
//     size:     20
//
// There's a valid codespan in that position, so `parse_code` will
// call the renderer's `code` callback to render it and happily
// return `6` to the parser, which will advance past the code span.
//
// The next character to parse is a space. There's no char trigger associated
// with a space, so it cannot be parsed. The parser takes note and carries on.
//
// The next character is a bracket. Good, there's a char trigger associated
// to that, so the parser calls `parse_link` with the following parameters:
//
//     doc:      <the doc>
//     target:   <some target>
//     data:     "`code` [link]()"
//     parsed:   6
//     start:    7
//     size:     20
//
// Notice how `parsed` is different from `start`. This is to inform
// `parse_link` that the region from 6 to 7 of the input (that is, the
// space) hasn't been parsed (nor rendered) by any function yet.
//
// `parse_link` detects a valid link at position `7`, renders it and
// returns `20`.
//
// ### Fallback parsing functions
//
// Fallback parsing functions are catch-all functions taking any unparsed
// input and rendering it. There are two of them: `parse_string` catches all
// inline data, and `parse_paragraph` parses all block data.
//
// What happens with input that is not parsed by any char trigger, such as
// the space above? Yep, it's parsed through `parse_string` and rendered as
// text.
//
// Fallback parsing functions can be called either from the parser, or
// from char triggers to "flush" the unparsed text before rendering again.
// They are the only parse functions that return `void`.
//
// ### Block parsing
//
// There are a few added details when parsing block data, but the concept is
// the same as with inline parsing: the first non-space character in the line
// is matched and the appropiate char trigger is called.
//
// But block nesting has a few complexities which have to be kept in mind
// when writing block char triggers. First, `parsed` and `start` will always
// point at the start of a non-empty line. Then, the char trigger is
// *assumed* to return at the start of a line. Failure to do so will
// have a rather funny effect. Head to "Block parsing" to learn more.
//
// ### Nesting
//
// Some Markdown constructs in turn contain Markdown. In this case, the
// parsing function will call `parse_inline` (if it's a block with inline
// content, such as a paragraph or header), or call `parse_block` (if it's
// a block containing other blocks), or add entries to the nesting stack (if
// it's an inline containing other inlines).
//
// `parse_block` must *never* be called from inline parsing functions, the
// whole parser relies on that assumption.
//
// Please head to the corresponding sections below to learn more about block
// and inline nesting.
//
// ### Auxiliary parsing functions
//
// Of course, not all parsing funtions are char triggers or fallbacks. It's
// common to break large or complex syntax (like links) into small blocks:
// `parse_link_title`, `parse_link_destination`, `parse_link_inline_spec`
// as an example.
//
// These auxiliary parsing functions don't usually call the renderer, just
// return the size and/or store some text into a buffer. In many cases they
// don't have a `start` parameter, they start parsing from the beginning.
//
// Some auxiliary parsing functions are suffixed `__` and a word. This is
// to indincate that they are private, only meant to be called by the function
// with the unsuffixed name.


// These are defined later

static size_t parse_single_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size, size_t lazy_size, hoedown_features lazy_ft);
static size_t parse_block(hoedown_document *doc, void *target, const uint8_t *data, size_t size, size_t lazy_size, hoedown_features lazy_ft);

static size_t parse_single_inline(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size);
static size_t parse_inline(hoedown_document *doc, void *target, const uint8_t *data, size_t size, uint8_t delimiter);

static inline_nesting *open_nesting(hoedown_document *doc, hoedown_features ft, size_t parsed, size_t start, size_t end);
static void close_nesting(hoedown_document *doc, inline_nesting *entry);
static void discard_nestings(hoedown_document *doc, inline_nesting *top);


// COMMON PARSING
// Syntax shared between block and inline constructs

static inline size_t parse_link_destination__enclosed(hoedown_document *doc, hoedown_buffer *destination, hoedown_buffer *destination_src, const uint8_t *data, size_t size) {
  size_t i = 0;

  // Opening angle bracket
  if (i < size && data[i] == '<') i++;
  else return 0;

  // Content and ending angle bracket
  while (1) {
    while (i < size && data[i] != '\n' && data[i] != '>' && data[i] != '<') i++;
    if (i < size && data[i] == '>' && !is_escaped(data, i)) {
      destination->size = 0;
      set_buffer_data(destination_src, data, 1, i);
      unescape_both(doc, destination, data + 1, i - 1);
      return i + 1;
    }

    if (i >= size || data[i] == '\n' || !is_escaped(data, i)) return 0;
    i++;
  }
}

static inline size_t parse_link_destination__free(hoedown_document *doc, hoedown_buffer *destination, hoedown_buffer *destination_src, const uint8_t *data, size_t size) {
  size_t i = 0;
  int inside_parentheses = 0;

  while (1) {
    while (i < size && data[i] > ' ' && data[i] != '(' && data[i] != ')') i++;
    if (i >= size || data[i] <= ' ') break;

    // Parentheses
    if (!is_escaped(data, i)) {
      if ((data[i] == ')') != inside_parentheses) break;
      inside_parentheses = !inside_parentheses;
    }
    i++;
  }

  if (i == 0) return 0;
  destination->size = 0;
  set_buffer_data(destination_src, data, 0, i);
  unescape_both(doc, destination, data, i);
  return i;
}

static inline size_t parse_link_destination(hoedown_document *doc, hoedown_buffer *destination, hoedown_buffer *destination_src, const uint8_t *data, size_t size) {
  size_t result;
  if ((result = parse_link_destination__enclosed(doc, destination, destination_src, data, size)))
    return result;
  return parse_link_destination__free(doc, destination, destination_src, data, size);
}

static inline size_t parse_link_title(hoedown_document *doc, hoedown_buffer *title, hoedown_buffer *title_src, const uint8_t *data, size_t size) {
  size_t i = 0;
  uint8_t delimiter;

  // Opening delimiter
  if (size < 2) return 0;
  delimiter = data[i];

  if (delimiter == '"' || delimiter == '\'' || delimiter == '(') i++;
  else return 0;

  if (delimiter == '(') delimiter = ')';

  // Content and closing delimiter
  while (1) {
    while (i < size && data[i] != delimiter) i++;
    if (i < size && !is_escaped(data, i)) {
      title->size = 0;
      set_buffer_data(title_src, data, 1, i);
      unescape_both(doc, title, data + 1, i - 1);
      return i + 1;
    }

    if (i >= size) return 0;
    i++;
  }
}

static inline size_t parse_link_label(hoedown_buffer *label, const uint8_t *data, size_t size) {
  size_t i = 0;

  // Opening bracket
  if (i < size && data[i] == '[') i++;
  else return 0;

  // Content and closing bracket
  while (1) {
    while (i < size && data[i] != ']' && data[i] != '[') i++;
    if (i < size && data[i] == ']' && !is_escaped(data, i)) {
      if (i > 1000) return 0;
      label->size = 0;
      collapse_spacing(label, data + 1, i - 1);
      return i + 1;
    }

    if (i >= size || !is_escaped(data, i)) return 0;
    i++;
  }
}

static inline int is_left_flanking(const uint8_t *data, size_t start, size_t end, size_t size) {
  //FIXME: remove this when targeted CommonMark has good definitions (jgm/CommonMark#310)
  //FIXME: check for Unicode whitespace / punctuation
  if (end >= size || is_space(data[end])) return 0;
  if (!(end < size && is_punct_ascii(data[end]))) return 1;
  if (start == 0 || is_space(data[start-1]) || is_punct_ascii(data[start-1])) return 1;
  return 0;
}

static inline int is_right_flanking(const uint8_t *data, size_t start, size_t end, size_t size) {
  //FIXME: check for Unicode whitespace / punctuation
  if (start == 0 || is_space(data[start-1])) return 0;
  if (!(start > 0 && is_punct_ascii(data[start-1]))) return 1;
  if (end >= size || is_space(data[end]) || is_punct_ascii(data[end])) return 1;
  return 0;
}


// INLINE PARSING

// This is the fallback parsing function for inline parsing.
static inline void parse_string(hoedown_document *doc, void *target, const uint8_t *data, size_t size) {
  if (size == 0) return;
  hoedown_buffer text = {(uint8_t *)data, size, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, 0, size);
  set_buffer_data(&doc->data.src[1], data, 0, size);
  doc->rndr.string(target, &text, &doc->data);
}

// data[start] is assumed to be `\\`
static size_t parse_escape(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1;

  if (i < size && is_punct_ascii(data[i])) {
    parse_string(doc, target, data + parsed, start - parsed);
    set_buffer_data(&doc->data.src[0], data, start, i+1);
    doc->rndr.escape(target, data[i], &doc->data);
    return i + 1;
  }

  return 0;
}

// data[start] is assumed to be '\n'
static size_t parse_linebreak(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t tail = start, head = start + 1;

  // Rewind on all trailing spaces
  while (tail > parsed && data[tail-1] == ' ') tail--;
  // Skip all leading spaces
  while (head < size && data[head] == ' ') head++;

  // Hard linebreak
  if (doc->ft & HOEDOWN_FT_LINEBREAK_HARD && start > parsed && data[start-1] == '\\') {
    parse_string(doc, target, data + parsed, (start-1) - parsed);
    set_buffer_data(&doc->data.src[0], data, start-1, start+1);
    doc->rndr.linebreak(target, 1, 0, &doc->data);
    return head;
  }

  // Normal linebreak
  if (start - tail >= 2) {
    parse_string(doc, target, data + parsed, tail - parsed);
    set_buffer_data(&doc->data.src[0], data, tail, start+1);
    doc->rndr.linebreak(target, 0, 0, &doc->data);
    return head;
  }

  // Soft linebreak
  if (doc->ft & HOEDOWN_FT_LINEBREAK_SOFT) {
    parse_string(doc, target, data + parsed, tail - parsed);
    set_buffer_data(&doc->data.src[0], data, tail, start+1);
    doc->rndr.linebreak(target, 0, 1, &doc->data);
    return head;
  }

  // Nothing, just a newline.
  if (tail == start && head == start + 1) return 0;
  parse_string(doc, target, data + parsed, tail - parsed);
  parse_string(doc, target, data + start, 1);
  return head;
}

// Assumes data[0] == '`'
static size_t parse_code(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  if (start > parsed && data[start-1] == '`') return 0;

  size_t i = start + 1, content_start, mark;
  size_t width;

  // Opening backticks
  mark = i;
  while (i < size && data[i] == '`') i++;
  width = i - mark;

  // Skip leading spacing
  while (i < size && is_space(data[i])) i++;

  // Consume content
  content_start = i;
  while (1) {
    while (i < size && data[i] != '`') i++;

    if (i < size) i++;
    else return 0;

    mark = i;
    while (i < size && data[i] == '`') i++;
    if (i - mark == width) break;
  }
  i = mark + width;
  mark--;

  // Rewind trailing spaces on content
  while (mark > content_start && is_space(data[mark-1])) mark--;

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  hoedown_buffer *code = hoedown_pool_get(&doc->block_buffers);
  code->size = 0;
  collapse_spacing(code, data + content_start, mark - content_start);

  set_buffer_data(&doc->data.src[0], data, start, i);
  set_buffer_data(&doc->data.src[1], data, content_start, mark);
  doc->rndr.code(target, code, &doc->data);
  hoedown_pool_pop(&doc->block_buffers, code);
  return i;
}

static inline size_t parse_uri_scheme(const uint8_t *data, size_t size) {
  size_t i = 0;

  if (size >= 30) size = 30;
  while (i < size && data[i] != ':') i++;

  if (i < size && hoedown_find_autolink_scheme((const char *)data, i)) i++;
  else return 0;

  return i;
}

// data[0] is assumed to be '<'
static size_t parse_uri_autolink(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1, mark;

  // Collect scheme
  mark = i;
  i += parse_uri_scheme(data + i, size - i);
  if (mark == i) return 0;

  // Rest of URL
  while (i < size && data[i] > ' ' && data[i] != '>' && data[i] != '<') i++;
  if (i >= size || data[i] != '>') return 0;

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  hoedown_buffer url = {(uint8_t *)data + mark, i - mark, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, i+1);
  set_buffer_data(&doc->data.src[1], url.data, 0, url.size);
  doc->rndr.uri_autolink(target, &url, &doc->data);
  return i + 1;
}

static inline size_t parse_email_label(const uint8_t *data, size_t size) {
  size_t i = 0;

  // Starts with alphanumeric character
  if (i < size && is_alnum_ascii(data[i])) i++;
  else return 0;

  // May also have hypens inside
  while (i < size && (is_alnum_ascii(data[i]) || data[i] == '-')) i++;

  // Verify the last character is *not* an hypen
  if (data[i-1] == '-') return 0;

  return i;
}

static inline size_t parse_email_label__cont(const uint8_t *data, size_t size) {
  size_t i = 0, mark;

  if (i < size && data[i] == '.') i++;
  else return 0;

  mark = i;
  i += parse_email_label(data + i, size - i);
  if (mark == i) return 0;

  return i;
}

// data[0] is assumed to be '<'
static size_t parse_email_autolink(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1, mark;

  // Collect username
  mark = i;
  while (i < size && (is_atext(data[i]) || data[i] == '.')) i++;
  if (mark == i) return 0;

  // Separator
  if (i < size && data[i] == '@') i++;
  else return 0;

  // Collect host
  mark = i;
  i += parse_email_label(data + i, size - i);
  if (mark == i) return 0;

  while (1) {
    mark = i;
    i += parse_email_label__cont(data + i, size - i);
    if (mark == i) break;
  }

  // Ending '>'
  if (i < size && data[i] == '>') i++;
  else return 0;

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  hoedown_buffer email = {(uint8_t *)data + start + 1, i - start - 2, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, i);
  set_buffer_data(&doc->data.src[1], email.data, 0, email.size);
  doc->rndr.email_autolink(target, &email, &doc->data);
  return i;
}

static inline size_t parse_link_spec__inline_title(hoedown_document *doc, hoedown_buffer *title, hoedown_buffer *title_src, const uint8_t *data, size_t size) {
  size_t i = 0, mark;

  // Mandatory spacing
  mark = i;
  while (i < size && is_space(data[i])) i++;
  if (mark == i) return 0;

  // Mandatory title
  mark = i;
  i += parse_link_title(doc, title, title_src, data + i, size - i);
  if (mark == i) return 0;

  return i;
}

static size_t parse_link_spec__inline(hoedown_document *doc, link_ref *ref, const uint8_t *data, size_t start, size_t size, size_t text_start, size_t text_end) {
  size_t i = start, mark;

  // Left parenthesis
  if (i < size && data[i] == '(') i++;
  else return 0;

  // Optional spacing
  while (i < size && is_space(data[i])) i++;

  // Optional destination
  ref->dest->size = 0;
  i += parse_link_destination(doc, ref->dest, &ref->dest_src, data + i, size - i);

  // Optional spacing and title
  mark = i;
  i += parse_link_spec__inline_title(doc, ref->title, &ref->title_src, data + i, size - i);
  ref->has_title = (mark < i);

  // Optional spacing
  while (i < size && is_space(data[i])) i++;

  // Right parenthesis
  if (i < size && data[i] == ')') i++;
  else return 0;

  ref->id = 1;
  return i;
}

static size_t parse_link_spec__collapsed(hoedown_document *doc, link_ref *ref, const uint8_t *data, size_t start, size_t size, size_t text_start, size_t text_end) {
  size_t i = start;

  // Optional spacing
  while (i < size && is_space(data[i])) i++;

  // Brackets
  if (i+1 < size && data[i] == '[' && data[i+1] == ']') i += 2;
  else return 0;

  // This is a collapsed link, try to match link text
  hoedown_buffer *label = hoedown_pool_get(&doc->inline_buffers);
  label->size = 0;
  collapse_spacing(label, data + text_start, text_end - text_start);
  ref->id = hash_string(label->data, label->size);
  hoedown_pool_pop(&doc->inline_buffers, label);

  link_ref *oref = find_link_ref(doc, ref->id);
  if (oref) {
    set_buffer_data(&ref->dest_src, oref->dest_src.data, 0, oref->dest_src.size);
    hoedown_buffer_set(ref->dest, oref->dest->data, oref->dest->size);
    ref->has_title = oref->has_title;
    if (oref->has_title) {
      set_buffer_data(&ref->title_src, oref->title_src.data, 0, oref->title_src.size);
      hoedown_buffer_set(ref->title, oref->title->data, oref->title->size);
    }
  } else {
    // Indicate we couldn't match
    ref->id = 0;
  }

  return i;
}

static size_t parse_link_spec__full(hoedown_document *doc, link_ref *ref, const uint8_t *data, size_t start, size_t size, size_t text_start, size_t text_end) {
  size_t i = start, mark;

  // Optional spacing
  while (i < size && is_space(data[i])) i++;

  // Label
  hoedown_buffer *label = hoedown_pool_get(&doc->inline_buffers);
  mark = i;
  i += parse_link_label(label, data + i, size - i);

  if (mark == i) {
    hoedown_pool_pop(&doc->inline_buffers, label);
    return 0;
  }

  // This is a reference link, try to match label
  ref->id = hash_string(label->data, label->size);
  hoedown_pool_pop(&doc->inline_buffers, label);

  link_ref *oref = find_link_ref(doc, ref->id);
  if (oref) {
    set_buffer_data(&ref->dest_src, oref->dest_src.data, 0, oref->dest_src.size);
    hoedown_buffer_set(ref->dest, oref->dest->data, oref->dest->size);
    ref->has_title = oref->has_title;
    if (oref->has_title) {
      set_buffer_data(&ref->title_src, oref->title_src.data, 0, oref->title_src.size);
      hoedown_buffer_set(ref->title, oref->title->data, oref->title->size);
    }
  } else {
    // Indicate we couldn't match
    ref->id = 0;
  }

  return i;
}

static inline size_t parse_link_spec(hoedown_document *doc, link_ref *ref, const uint8_t *data, size_t start, size_t size, size_t text_start, size_t text_end) {
  size_t result;

  // Try to match different spec types
  if ((result = parse_link_spec__inline(doc, ref, data, start, size, text_start, text_end)))
    return result;
  if ((result = parse_link_spec__collapsed(doc, ref, data, start, size, text_start, text_end)))
    return result;
  if ((result = parse_link_spec__full(doc, ref, data, start, size, text_start, text_end)))
    return result;

  // This is a shortcut link, try to match link text
  hoedown_buffer *label = hoedown_pool_get(&doc->inline_buffers);
  label->size = 0;
  collapse_spacing(label, data + text_start, text_end - text_start);
  ref->id = hash_string(label->data, label->size);
  hoedown_pool_pop(&doc->inline_buffers, label);

  link_ref *oref = find_link_ref(doc, ref->id);
  if (oref) {
    set_buffer_data(&ref->dest_src, oref->dest_src.data, 0, oref->dest_src.size);
    hoedown_buffer_set(ref->dest, oref->dest->data, oref->dest->size);
    ref->has_title = oref->has_title;
    if (oref->has_title) {
      set_buffer_data(&ref->title_src, oref->title_src.data, 0, oref->title_src.size);
      hoedown_buffer_set(ref->title, oref->title->data, oref->title->size);
    }
  } else {
    // Indicate we couldn't match
    ref->id = 0;
  }

  return start;
}

// data[start] is assumed to be '['
static size_t parse_link(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1, mark, content_start, content_end;
  inline_data *inline_data = doc->inline_data;
  int current_inside_link = doc->inside_link, current_forbidden = doc->plain_links_forbidden;
  int is_image = 0;
  void *content;
  link_ref *ref;

  if (start < inline_data->link_not_found) return 0;

  // Is this an image link?
  if (doc->ft & HOEDOWN_FT_LINK_IMAGE && start > parsed && data[start-1] == '!') {
    start--;
    is_image = 1;
  }

  // Prepare the environment, parse the content
  inline_data->link_not_found = size;
  doc->inside_link = 1;
  if (!is_image) doc->plain_links_forbidden = 1;

  mark = i;
  content = doc->rndr.object_get(1, &doc->data);
  i += parse_inline(doc, content, data + i, size - i, ']');

  doc->inside_link = current_inside_link;
  doc->plain_links_forbidden = current_forbidden;
  if (!inline_data->link_not_found && inline_data->previous)
    inline_data->previous->link_not_found = 0;

  // No closing bracket found?
  if (i >= size) {
    doc->rndr.object_pop(content, 1, &doc->data);
    return (doc->plain_links_forbidden) ? size : 0;
  }

  assert(data[i] == ']');
  content_start = mark;
  content_end = i;
  inline_data->link_not_found = i;

  // Try to parse a link spec
  i++;
  ref = hoedown_pool_get(&doc->link_refs__pool);
  i = parse_link_spec(doc, ref, data, i, size, content_start, content_end);

  if (!ref->id) {
    hoedown_pool_pop(&doc->link_refs__pool, ref);
    doc->rndr.object_pop(content, 1, &doc->data);
    return 0;
  }

  // Valid link!
  if (inline_data->previous) inline_data->previous->link_not_found = 0;

  if (!is_image && doc->plain_links_forbidden) {
    hoedown_pool_pop(&doc->link_refs__pool, ref);
    doc->rndr.object_pop(content, 1, &doc->data);
    return size;
  }

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  set_buffer_data(&doc->data.src[0], data, start, i);
  set_buffer_data(&doc->data.src[1], data, content_start, content_end);
  set_buffer_data(&doc->data.src[2], ref->dest_src.data, 0, ref->dest_src.size);
  if (ref->has_title)
    set_buffer_data(&doc->data.src[3], ref->title_src.data, 0, ref->title_src.size);
  doc->rndr.link(target, content, ref->dest, ref->has_title ? ref->title : NULL, is_image, &doc->data);
  hoedown_pool_pop(&doc->link_refs__pool, ref);
  doc->rndr.object_pop(content, 1, &doc->data);
  return i;
}

// data[start] is assumed to be '[' or ']'
static size_t parse_brackets(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  // This is only activated in certain contexts, such as inside of
  // a link (both plain and image). It's there to prevent the link
  // of containing unbalanced square brackets.
  if (!doc->inside_link) return 0;

  if (data[start] == '[') {
    // Opening bracket
    doc->inline_data->unbalanced_brackets++;
  } else {
    // Closing bracket
    if (doc->inline_data->unbalanced_brackets == 0) return 0;
    doc->inline_data->unbalanced_brackets--;
  }

  parse_string(doc, target, data + parsed, start+1 - parsed);
  return start+1;
}

// data[0] is assumed to be '<'
static inline size_t parse_cdata(hoedown_document *doc, const uint8_t *data, size_t size) {
  size_t i = 0;
  if (doc->inline_data->cdata_not_found) return 0;

  // Starting prefix
  if (size >= 9 && memcmp(data, "<![CDATA[", 9) == 0) i += 9;
  else return 0;

  // Content
  size -= 2;
  while (i < size && !(data[i] == ']' && data[i+1] == ']' && data[i+2] == '>')) i++;

  if (i < size) return i + 3;
  doc->inline_data->cdata_not_found = 1;
  return 0;
}

// data[0] is assumed to be '<'
static inline size_t parse_processing_instruction(hoedown_document *doc, const uint8_t *data, size_t size) {
  size_t i = 1;
  if (doc->inline_data->instruction_not_found) return 0;

  // Starting prefix
  if (i < size && data[i] == '?') i++;
  else return 0;

  // Content
  size -= 1;
  while (i < size && !(data[i] == '?' && data[i+1] == '>')) i++;

  if (i < size) return i + 2;
  doc->inline_data->instruction_not_found = 1;
  return 0;
}

// data[0] is assumed to be '<'
static inline size_t parse_declaration(hoedown_document *doc, const uint8_t *data, size_t size) {
  size_t i = 1, mark;
  if (doc->inline_data->declaration_not_found) return 0;

  // Starting prefix
  if (i < size && data[i] == '!') i++;
  else return 0;

  // Name
  mark = i;
  while (i < size && is_upper_ascii(data[i])) i++;
  if (mark == i) return 0;

  // Spacing
  mark = i;
  while (i < size && is_space(data[i])) i++;
  if (mark == i) return 0;

  // Rest of tag
  while (i < size && data[i] != '>') i++;

  if (i < size) return i + 1;
  doc->inline_data->declaration_not_found = 1;
  return 0;
}

// data[start] is assumed to be '<'
static size_t parse_html(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t result;

  const uint8_t *cur_data = data + start;
  size_t cur_size = size - start;

  if (
    (result = html_parse_start_tag(NULL, cur_data, cur_size)) ||
    (result = html_parse_end_tag(NULL, cur_data, cur_size)) ||
    (result = html_parse_comment(cur_data, cur_size)) ||
    (result = parse_cdata(doc, cur_data, cur_size)) ||
    (result = parse_processing_instruction(doc, cur_data, cur_size)) ||
    (result = parse_declaration(doc, cur_data, cur_size))
  ) {
    parse_string(doc, target, data + parsed, start - parsed);

    hoedown_buffer html = {(uint8_t *)cur_data, result, 0, 0, NULL, NULL};
    set_buffer_data(&doc->data.src[0], html.data, 0, html.size);
    set_buffer_data(&doc->data.src[1], html.data, 0, html.size);
    doc->rndr.html(target, &html, &doc->data);
    return start + result;
  }

  return 0;
}

// data[start] is assumed to be '&'
static size_t parse_entity(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1;

  hoedown_buffer *character = hoedown_pool_get(&doc->inline_buffers);
  character->size = 0;
  i += hoedown_unescape_entity(character, data + i, size - i);

  if (i > start + 1) {
    parse_string(doc, target, data + parsed, start - parsed);

    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], data, start+1, i);
    doc->rndr.entity(target, character, &doc->data);
    hoedown_pool_pop(&doc->inline_buffers, character);
    return i;
  }

  hoedown_pool_pop(&doc->inline_buffers, character);
  return 0;
}

// data[start] is assumed to be '*' or '_'
static size_t parse_emphasis(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  uint8_t delimiter = data[start];
  size_t i = start + 1, mark = start, width;
  int no_intra = !(doc->ft & HOEDOWN_FT_INTRA_EMPHASIS);
  int can_open, can_close;
  inline_nesting *entry;

  // Refuse to process the same delimiter again, otherwise advance
  if (start > parsed && data[start-1] == delimiter) return 0;
  while (i < size && data[i] == delimiter) i++;

  if (i - mark > 2 * doc->max_nesting) return 0;
  can_open = is_left_flanking(data, start, i, size);
  can_close = is_right_flanking(data, start, i, size);
  if (delimiter == '_' && no_intra && can_open && can_close) return 0;

  // Try to close as many emphasis as possible with this delimiter
  if (can_close) {
    for (entry = doc->inline_data->nesting; entry && mark < i; entry = entry->previous) {
      if (entry->ft != HOEDOWN_FT_EMPHASIS || data[entry->start] != delimiter) continue;

      // Found a valid entry to close! Yay!
      discard_nestings(doc, entry);
      width = entry->end - entry->start;
      if (width > i - mark) width = i - mark;
      parse_string(doc, doc->inline_data->target, data + parsed, mark - parsed);

      set_buffer_data(&doc->data.src[0], data, entry->end - width, mark + width);
      set_buffer_data(&doc->data.src[1], data, entry->end, mark);
      mark += width;
      parsed = mark;

      // Render emphasis and [partially] close the nesting
      if (width < entry->end - entry->start) {
        void *intermediate = doc->rndr.object_get(1, &doc->data);
        doc->rndr.emphasis(intermediate, doc->inline_data->target, width, &doc->data);
        doc->rndr.object_pop(doc->inline_data->target, 1, &doc->data);
        doc->inline_data->target = intermediate;
        entry->end -= width;
      } else {
        parse_string(doc, entry->parent, data + entry->parsed, entry->start - entry->parsed);
        doc->rndr.emphasis(entry->parent, doc->inline_data->target, width, &doc->data);
        close_nesting(doc, entry);
      }
    }
  }

  // Open nesting entry for this emphasis
  if (can_open && mark < i && doc->current_nesting < doc->max_nesting) {
    entry = open_nesting(doc, HOEDOWN_FT_EMPHASIS, parsed, mark, i);
    parsed = mark = i;
  }

  return mark > start ? mark : 0;
}

// Should only be called from parse_math. `end` is assumed to be uninitialized.
// `is_inline` may not be updated if the type is to be autodetected.
// `data` is assumed to start with `$` or `\\`.
static inline size_t parse_math__delimiter(hoedown_document *doc, const uint8_t *data, size_t size, hoedown_buffer *end, int *is_inline, int **not_found) {
  if (data[0] == '\\') {
    if (size < 3 || data[1] != '\\') return 0;

    // LaTeX style
    if (data[2] == '(') {
      end->data = (uint8_t *) "\\\\)";
      end->size = 3;
      *is_inline = 1;
      *not_found = &doc->inline_data->latex_inline_math_not_found;
      return 3;
    }
    if (data[2] == '[') {
      end->data = (uint8_t *) "\\\\]";
      end->size = 3;
      *is_inline = 0;
      *not_found = &doc->inline_data->latex_block_math_not_found;
      return 3;
    }
    return 0;
  }

  if (data[1] == '$') {
    // TeX style, two dollars
    end->data = (uint8_t *) "$$";
    end->size = 2;
    if (doc->ft & HOEDOWN_FT_MATH_EXPLICIT) *is_inline = 0;
    *not_found = &doc->inline_data->tex_block_math_not_found;
    return 2;
  }

  // TeX style, one dollar
  if (!(doc->ft & HOEDOWN_FT_MATH_EXPLICIT)) return 0;
  end->data = (uint8_t *) "$";
  end->size = 1;
  *is_inline = 1;
  *not_found = &doc->inline_data->tex_inline_math_not_found;
  return 1;
}

// data[start] is assumed to be '\\' or '$'
static size_t parse_math(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark;
  hoedown_buffer end = {NULL, 0, 0, 0, NULL, NULL};
  int is_inline = 9, *not_found = NULL;

  // Parse starting delimiter, determine span type and ending
  i += parse_math__delimiter(doc, data + i, size - i, &end, &is_inline, &not_found);
  if (i == start) return 0;

  // Search for ending delimiter
  if (*not_found) return 0;
  mark = i;
  while (1) {
    while (i < size && data[i] != end.data[0]) i++;
    if (i >= size) {
      *not_found = 1;
      return 0;
    }

    if (i + end.size <= size && !is_escaped(data, i) &&
        memcmp(data + i, end.data, end.size) == 0) break;
    i++;
  }

  // Autodetect type of span, if needed
  if (is_inline == 9)
    is_inline = (start > 0) || (i + end.size < size);

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  hoedown_buffer math = {(uint8_t *)data + mark, i - mark, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, i + end.size);
  set_buffer_data(&doc->data.src[1], math.data, 0, math.size);
  doc->rndr.math(target, &math, is_inline, &doc->data);
  return i + end.size;
}

// data[start] is assumed to be '^'
static size_t parse_superscript(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1, mark;

  // If the superscript is nesting, open nesting and return
  if (i < size && data[i] == '(') {
    if (doc->current_nesting >= doc->max_nesting) return 0;
    open_nesting(doc, HOEDOWN_FT_SUPERSCRIPT, parsed, start, i + 1);
    return i + 1;
  }

  // Collect next "word"
  //FIXME: Unicode support
  mark = i;
  while (i < size && !is_space(data[i]) && !is_punct_ascii(data[i])) i++;
  if (mark == i) return 0;

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  void *content = doc->rndr.object_get(1, &doc->data);
  parse_string(doc, content, data + mark, i - mark);
  set_buffer_data(&doc->data.src[0], data, start, i);
  set_buffer_data(&doc->data.src[1], data, mark, i);
  doc->rndr.superscript(target, content, &doc->data);
  doc->rndr.object_pop(content, 1, &doc->data);
  return i;
}

// data[start] is assumed to be '~'
static size_t parse_strikethrough(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 2;
  if (end > size || data[start+1] != '~') return 0;

  // Try to close stikethrough nesting
  inline_nesting *entry;
  for (entry = doc->inline_data->nesting; entry; entry = entry->previous) {
    if (entry->ft == HOEDOWN_FT_STRIKETHROUGH) {
      discard_nestings(doc, entry);
      parse_string(doc, target, data + parsed, start - parsed);

      parse_string(doc, entry->parent, data + entry->parsed, entry->start - entry->parsed);
      set_buffer_data(&doc->data.src[0], data, entry->start, end);
      set_buffer_data(&doc->data.src[1], data, entry->end, start);
      doc->rndr.strikethrough(entry->parent, target, &doc->data);
      close_nesting(doc, entry);
      return end;
    }
  }

  // Otherwise open nesting
  open_nesting(doc, HOEDOWN_FT_STRIKETHROUGH, parsed, start, end);
  return end;
}

// data[start] is assumed to be '='
static size_t parse_highlight(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 2;
  if (end > size || data[start+1] != '=') return 0;

  // Try to close stikethrough nesting
  inline_nesting *entry;
  for (entry = doc->inline_data->nesting; entry; entry = entry->previous) {
    if (entry->ft == HOEDOWN_FT_HIGHLIGHT) {
      discard_nestings(doc, entry);
      parse_string(doc, target, data + parsed, start - parsed);

      parse_string(doc, entry->parent, data + entry->parsed, entry->start - entry->parsed);
      set_buffer_data(&doc->data.src[0], data, entry->start, end);
      set_buffer_data(&doc->data.src[1], data, entry->end, start);
      doc->rndr.highlight(entry->parent, target, &doc->data);
      close_nesting(doc, entry);
      return end;
    }
  }

  // Otherwise open nesting
  open_nesting(doc, HOEDOWN_FT_HIGHLIGHT, parsed, start, end);
  return end;
}

// data[start] is assumed to be ')'
static size_t parse_parenthesis(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  inline_nesting *entry;
  for (entry = doc->inline_data->nesting; entry; entry = entry->previous) {
    if (entry->ft == HOEDOWN_FT_SUPERSCRIPT) {
      discard_nestings(doc, entry);
      parse_string(doc, target, data + parsed, start - parsed);

      parse_string(doc, entry->parent, data + entry->parsed, entry->start - entry->parsed);
      set_buffer_data(&doc->data.src[0], data, entry->start, start+1);
      set_buffer_data(&doc->data.src[1], data, entry->end, start);
      doc->rndr.superscript(entry->parent, target, &doc->data);
      close_nesting(doc, entry);
      return start + 1;
    }
  }
  return 0;
}

// data[start] is assumed to be '^'
static size_t parse_sidenote(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 2;
  if (end > size || data[start+1] != '[') return 0;

  // Sidenotes and footnotes cannot contain sidenotes
  if (doc->inside_footnote) return 0;
  inline_nesting *entry;
  for (entry = doc->inline_data->nesting; entry; entry = entry->previous) {
    if (entry->ft == HOEDOWN_FT_SIDENOTE) return 0;
  }

  // Open nesting entry
  open_nesting(doc, HOEDOWN_FT_SIDENOTE, parsed, start, end);
  return end;
}

// data[start] is assumed to be ']'
static size_t parse_bracket(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  inline_nesting *entry;
  for (entry = doc->inline_data->nesting; entry; entry = entry->previous) {
    if (entry->ft == HOEDOWN_FT_SIDENOTE) {
      discard_nestings(doc, entry);
      parse_string(doc, target, data + parsed, start - parsed);

      parse_string(doc, entry->parent, data + entry->parsed, entry->start - entry->parsed);
      set_buffer_data(&doc->data.src[0], data, entry->start, start+1);
      set_buffer_data(&doc->data.src[1], data, entry->end, start);
      doc->rndr.sidenote(entry->parent, target, &doc->data);
      close_nesting(doc, entry);
      return start + 1;
    }
  }
  return 0;
}

static inline int is_emoji_name(uint8_t c) {
  return is_lower_ascii(c) || c == '_' || c == '-' || is_digit_ascii(c);
}

// data[start] is assumed to be ':'
static size_t parse_emoji(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start + 1, mark;

  mark = i;
  while (i < size && is_emoji_name(data[i])) i++;
  if (mark == i || i >= size || data[i] != ':') return 0;

  // Render!
  parse_string(doc, target, data + parsed, start - parsed);

  hoedown_buffer name = {(uint8_t *)data + mark, i - mark, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, i+1);
  set_buffer_data(&doc->data.src[1], data, mark, i);
  doc->rndr.emoji(target, &name, &doc->data);
  return i + 1;
}

// data[start] is assumed to be '"' or '-'
static size_t parse_typography_quote(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 1;
  int left_flanking = is_left_flanking(data, start, end, size);
  int right_flanking = is_right_flanking(data, start, end, size);
  if (left_flanking == right_flanking) return 0;

  parse_string(doc, target, data + parsed, start - parsed);
  hoedown_buffer character = {
    (uint8_t *)((data[start] == '"')
      ? (left_flanking ? "“" : "”")
      : (left_flanking ? "‘" : "’")
    ), 3, 0, 0, NULL, NULL
  };
  set_buffer_data(&doc->data.src[0], data, start, end);
  set_buffer_data(&doc->data.src[1], data, start, end);
  doc->rndr.typography(target, &character, &doc->data);
  return end;
}

// data[start] is assumed to be '-'
static size_t parse_typography_dash(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 2;
  if (end > size || data[start+1] != '-') return 0;

  parse_string(doc, target, data + parsed, start - parsed);
  hoedown_buffer character = {(uint8_t *) "–", 3, 0, 0, NULL, NULL};
  if (end < size && data[end] == '-') {
    character.data = (uint8_t *) "—";
    end++;
  }
  set_buffer_data(&doc->data.src[0], data, start, end);
  set_buffer_data(&doc->data.src[1], data, start, end);
  doc->rndr.typography(target, &character, &doc->data);
  return end;
}

// data[start] is assumed to be '.'
static size_t parse_typography_ellipsis(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 3;
  if (end > size || data[start+1] != '.' || data[start+2] != '.') return 0;

  parse_string(doc, target, data + parsed, start - parsed);
  hoedown_buffer character = {(uint8_t *) "…", 3, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, end);
  set_buffer_data(&doc->data.src[1], data, start, end);
  doc->rndr.typography(target, &character, &doc->data);
  return end;
}

// data[start] is assumed to be '('
static size_t parse_typography_copyright(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t end = start + 3;
  if (end > size || data[start+2] != ')' || (data[start+1] != 'c' && data[start+1] != 'C')) return 0;

  parse_string(doc, target, data + parsed, start - parsed);
  hoedown_buffer character = {(uint8_t *) "©", 3, 0, 0, NULL, NULL};
  set_buffer_data(&doc->data.src[0], data, start, end);
  set_buffer_data(&doc->data.src[1], data, start, end);
  doc->rndr.typography(target, &character, &doc->data);
  return end;
}


// BLOCK PARSING

// This is the fallback parsing function for block parsing.
static inline void parse_paragraph(hoedown_document *doc, void *target, const uint8_t *data, size_t size) {
  if (size == 0 || doc->mode != NORMAL_PARSING) return;

  size_t content_start = 0, content_end = size;
  while (content_start < size && is_space(data[content_start])) content_start++;
  while (content_end > content_start && is_space(data[content_end-1])) content_end--;

  void *content = doc->rndr.object_get(1, &doc->data);
  parse_inline(doc, content, data + content_start, content_end - content_start, 0);
  set_buffer_data(&doc->data.src[0], data, 0, size);
  set_buffer_data(&doc->data.src[1], data, content_start, content_end);
  doc->rndr.paragraph(target, content, doc->is_tight == doc->current_nesting, &doc->data);
  doc->rndr.object_pop(content, 1, &doc->data);
}

static inline size_t parse_atx_header_end(const uint8_t *data, size_t size) {
  size_t i = size, mark;

  // Forget about trailing spaces
  while (i > 0 && data[i-1] == ' ') i--;
  size = i;

  // Retract to skip trailing hashes
  mark = i;
  while (i > 0 && data[i-1] == '#') i--;
  if (mark == i) return size;

  // Check that they're present, and not escaped
  if (is_escaped(data, i)) return size;

  // Retract again to skip spaces between content and hashes
  mark = i;
  while (i > 0 && data[i-1] == ' ') i--;
  if (mark == i && i > 0) return size;

  return i;
}

static size_t parse_atx_header(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark, content_start;
  size_t width;

  // Initial spaces
  mark = i;
  while (i < size && data[i] == ' ') i++;
  if ((i - mark) > 3) return 0;

  // Hashes
  mark = i;
  while (i < size && data[i] == '#') i++;
  width = i - mark;
  if (width == 0 || width > 6) return 0;

  // Mandatory spaces
  mark = i;
  while (i < size && data[i] == ' ') i++;
  if (mark == i && data[i] != '\n') return 0;

  content_start = i;

  // Skip until end of line, determine end of content
  while (i < size && data[i] != '\n') i++;
  mark = content_start + parse_atx_header_end(data + content_start, i - content_start);

  // Skip past newline
  if (i < size) i++;

  // Render!
  if (doc->mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);

    void *content = doc->rndr.object_get(1, &doc->data);
    parse_inline(doc, content, data + content_start, mark - content_start, 0);
    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], data, content_start, mark);
    doc->rndr.atx_header(target, content, width, &doc->data);
    doc->rndr.object_pop(content, 1, &doc->data);
  }

  return i;
}

// Beware! This has to be called at the start of the setext rule (the header's
// text should be at the line just before start, which shouldn't be included
// in `parsed`).
static size_t parse_setext_header(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark, content_start, content_end;
  uint8_t character;

  // Skip indentation
  mark = i;
  while (i < size && data[i] == ' ') i++;
  if (i >= size || i - mark > 3) return 0;

  // Collect first character
  character = data[i];
  if (character == '-' || character == '=') i++;
  else return 0;

  // Collect rest of the characters, then trailing spaces
  while (i < size && data[i] == character) i++;
  while (i < size && data[i] == ' ') i++;

  // Line should end here
  if (i < size && data[i] != '\n') return 0;
  i++;


  // Make sure there's a newline just before start
  if (start > parsed && data[start-1] == '\n') start--;
  else return 0;

  // Rewind to trim trailing spaces
  while (start > parsed && data[start-1] == ' ') start--;
  content_end = start;

  // Rewind until we have the whole line caught
  while (start > parsed && data[start-1] != '\n') start--;
  content_start = start;

  // Check that this is the only unparsed line; advance to skip leading spaces
  if (content_start == content_end || start > parsed) return 0;
  while (content_start < content_end && data[content_start] == ' ') content_start++;


  // Render!
  if (doc->mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);

    void *content = doc->rndr.object_get(1, &doc->data);
    parse_inline(doc, content, data + content_start, content_end - content_start, 0);
    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], data, content_start, content_end);
    doc->rndr.setext_header(target, content, character == '=', &doc->data);
    doc->rndr.object_pop(content, 1, &doc->data);
  }

  return i;
}

static size_t parse_horizontal_rule(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start;
  uint8_t character;
  size_t count;

  // Skip three optional spaces
  if (unlikely(i + 3 > size)) return 0;

  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  }}}

  // Collect valid character
  if (unlikely(i >= size)) return 0;

  character = data[i];
  if (character == '*' || character == '-' || character == '_') i++;
  else return 0;

  count = 1;

  // Collect rest of characters until end of line
  while (1) {
    while (i < size && data[i] == ' ') i++;

    if (i < size && data[i] == character) {
      i++;
      count++;
    } else break;
  }

  // Verify there's at least three characters, and end of line
  if (count < 3) return 0;

  if (likely(i < size)) {
    if (data[i] == '\n') i++;
    else return 0;
  }

  // Render!
  if (doc->mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);

    set_buffer_data(&doc->data.src[0], data, start, i);
    doc->rndr.horizontal_rule(target, &doc->data);
  }

  return i;
}

static size_t parse_indented_code_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark;
  size_t last_non_empty_line = 0;
  hoedown_buffer *code = get_mapped_buffer(doc, &doc->block_buffers);
  code->size = 0;

  while (1) {
    // Parse initial spaces
    mark = i;
    while (i < size && data[i] == ' ') i++;

    if (i >= size) break;
    if (i < mark + 4 && data[i] != '\n') break;

    // If line is non-empty, set the length later
    if (data[i] != '\n') last_non_empty_line = 0;

    // Add rest of line to working buffer
    mark += 4;
    if (i < mark) mark = i;

    while (i < size && data[i] != '\n') i++;
    if (i < size) i++;
    mapped_buffer_put(doc, code, data + mark, i - mark);
    if (!last_non_empty_line) last_non_empty_line = code->size;
  }

  // Rewind i to the line start
  i = mark;

  // Rewind the work buffer to cut empty lines at the end
  mapped_buffer_rewind(doc, code, last_non_empty_line);

  // Render!
  if (doc->mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);

    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], code->data, 0, code->size);
    doc->rndr.indented_code_block(target, code, &doc->data);
  }

  pop_mapped_buffer(doc, &doc->block_buffers, code);
  return i;
}

static inline size_t parse_code_fence(const uint8_t *data, size_t size, uint8_t *character, size_t *width) {
  size_t i = 0, mark;

  // Skip three optional spaces
  if (unlikely(i + 3 > size)) return 0;

  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  }}}

  // Process first character
  if (i >= size) return 0;
  *character = data[i];
  mark = i;
  if (*character == '~' || *character == '`') i++;
  else return 0;

  // Process rest of fence
  while (i < size && data[i] == *character) i++;
  if ((*width = i - mark) < 3) return 0;

  return i;
}

static size_t parse_fenced_code_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark;
  size_t indentation, start_width, end_width;
  uint8_t start_character, end_character;

  // Parse initial fence
  mark = i;
  i += parse_code_fence(data + i, size - i, &start_character, &start_width);
  if (mark == i) return 0;
  indentation = (i - mark) - start_width;

  // Parse optional info string
  while (i < size && data[i] == ' ') i++;

  mark = i;
  while (i < size && data[i] != '`' && data[i] != '\n') i++;
  if (unlikely(i < size && data[i] == '`')) return 0;

  hoedown_buffer *info = NULL;

  if (doc->mode == NORMAL_PARSING) {
    info = hoedown_pool_get(&doc->inline_buffers);
    info->size = 0;
    unescape_both(doc, info, data + mark, i - mark);
    while (info->size > 0 && info->data[info->size-1] == ' ') info->size--;
  }

  if (i < size) i++;

  // Parse the content
  if (unlikely(indentation) && doc->mode == NORMAL_PARSING) {
    hoedown_buffer *code = get_mapped_buffer(doc, &doc->block_buffers);
    code->size = 0;

    size_t line_start;
    while (i < size) {
      // Advance until end of line
      line_start = i;
      while (i < size && data[i] != '\n') i++;
      if (i < size) i++;

      // Check if there's an ending fence here
      mark = parse_code_fence(data + line_start, i - line_start, &end_character, &end_width);

      if (mark && start_character == end_character && start_width <= end_width &&
          is_empty(data + line_start + mark, i - line_start - mark))
        break;

      // Skip optional indentation
      mark = line_start;
      while (mark < size && data[mark] == ' ' && mark - line_start < indentation) mark++;

      // Copy line into work buffer
      mapped_buffer_put(doc, code, data + mark, i - mark);
    }

    // Render!
    parse_paragraph(doc, target, data + parsed, start - parsed);

    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], code->data, 0, code->size);
    // already set
    doc->rndr.fenced_code_block(target, code, info->size ? info : NULL, &doc->data);
    pop_mapped_buffer(doc, &doc->block_buffers, code);
  } else {
    // Optimization: When indentation is 0 we don't need intermediate buffers.
    size_t text_start = i, line_start;
    while (1) {
      line_start = i;
      if (i >= size) break;

      // Advance until end of line
      while (i < size && data[i] != '\n') i++;
      if (i < size) i++;

      // Check if there's an ending fence here
      mark = parse_code_fence(data + line_start, i - line_start, &end_character, &end_width);

      if (mark && start_character == end_character && start_width <= end_width &&
          is_empty(data + line_start + mark, i - line_start - mark))
        break;
    }

    // Render!
    if (doc->mode == NORMAL_PARSING) {
      parse_paragraph(doc, target, data + parsed, start - parsed);

      hoedown_buffer code = {(uint8_t *)data + text_start, line_start - text_start, 0, 0, NULL, NULL};
      set_buffer_data(&doc->data.src[0], data, start, i);
      set_buffer_data(&doc->data.src[1], code.data, 0, code.size);
      // already set
      doc->rndr.fenced_code_block(target, &code, info->size ? info : NULL, &doc->data);
    }
  }

  if (doc->mode == NORMAL_PARSING)
    hoedown_pool_pop(&doc->inline_buffers, info);

  return i;
}

static size_t parse_html_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, content_start, mark;
  // FIXME: Right now this doesn't implement what is in the spec,
  // because of some inconsistencies with the reference parsers.
  // There's an issue tracking this (jgm/CommonMark#177), and
  // this function should be rewritten when the problem is resolved.

  // Skip three optional spaces
  if (unlikely(i + 3 > size)) return 0;

  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  }}}

  // Check for initial '<'
  if (data[i] != '<') return 0;
  content_start = i;

  // Advance until empty line
  while (1) {
    mark = i;
    while (i < size && data[i] == ' ') i++;

    if (i < size && data[i] != '\n') i++;
    else break;

    while (i < size && data[i] != '\n') i++;
    if (i < size) i++;
  }

  // Try to parse various constructs with a mega-if
  const uint8_t *html_data = data + content_start;
  size_t html_size = mark - content_start;
  hoedown_buffer *name = hoedown_pool_get(&doc->inline_buffers);

  if (
    // HTML start / end tag
    (
      (html_parse_start_tag(name, html_data, html_size) ||
       html_parse_end_tag(name, html_data, html_size))
      && hoedown_find_block_tag((const char *)name->data, name->size)
    )

    // HTML comment
    || html_parse_comment(html_data, html_size)

    // CDATA section
    //|| parse_cdata(html_data, html_size)

    // Processing instruction
    //|| parse_processing_instruction(html_data, html_size)

    // Declaration
    //|| parse_declaration(html_data, html_size)
  ) {
    hoedown_pool_pop(&doc->inline_buffers, name);

    // Render!
    if (doc->mode == NORMAL_PARSING) {
      parse_paragraph(doc, target, data + parsed, start - parsed);

      hoedown_buffer html = {(uint8_t *)data + start, mark - start, 0, 0, NULL, NULL};
      set_buffer_data(&doc->data.src[0], html.data, 0, html.size);
      set_buffer_data(&doc->data.src[1], html.data, 0, html.size);
      doc->rndr.html_block(target, &html, &doc->data);
    }

    return i;
  }

  hoedown_pool_pop(&doc->inline_buffers, name);
  return 0;
}

static inline size_t parse_link_reference_title(hoedown_document *doc, hoedown_buffer *title, hoedown_buffer *title_src, const uint8_t *data, size_t size) {
  size_t i = 0, mark;

  // Mandatory spacing, up to one newline
  mark = i;
  while (i < size && data[i] == ' ') i++;
  if (i < size && data[i] == '\n') {
    i++;
    while (i < size && data[i] == ' ') i++;
  }
  if (mark == i) return 0;

  // Title!
  mark = i;
  i += parse_link_title(doc, title, title_src, data + i, size - i);
  if (mark == i) return 0;

  return i;
}

static inline size_t parse_link_reference_content(hoedown_document *doc, link_ref *ref, const uint8_t *data, size_t size) {
  size_t i = 0, mark;

  // Optional spacing, up to one newline
  while (i < size && data[i] == ' ') i++;
  if (i < size && data[i] == '\n') {
    i++;
    while (i < size && data[i] == ' ') i++;
  }

  // Destination!
  mark = i;
  i += parse_link_destination(doc, ref->dest, &ref->dest_src, data + i, size - i);
  if (mark == i) return 0;

  // Optional whitespace and title
  mark = i;
  i += parse_link_reference_title(doc, ref->title, &ref->title_src, data + i, size - i);
  ref->has_title = (mark < i);

  // Optional spaces
  while (i < size && data[i] == ' ') i++;

  return i;
}

// Marker parsing method.
static size_t parse_link_reference(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark;
  hoedown_buffer *label;
  link_ref *ref;

  // Skip three optional spaces
  if (unlikely(i + 3 > size)) return 0;

  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  if (data[i] == ' ') { i++;
  }}}

  // Link label and colon
  label = hoedown_pool_get(&doc->inline_buffers);
  mark = i;
  i += parse_link_label(label, data + i, size - i);

  if (i > mark && i < size && data[i] == ':') i++;
  else {
    hoedown_pool_pop(&doc->inline_buffers, label);
    return 0;
  }

  // Contents (destination and title) and newline or EOF
  ref = hoedown_pool_get(&doc->link_refs__pool);
  mark = i;
  i += parse_link_reference_content(doc, ref, data + i, size - i);

  if (i > mark && (i >= size || data[i] == '\n')) i++;
  else {
    hoedown_pool_pop(&doc->inline_buffers, label);
    hoedown_pool_pop(&doc->link_refs__pool, ref);
    return 0;
  }

  // Store!
  if (doc->mode == MARKER_PARSING) {
    ref->id = hash_string(label->data, label->size);
    hoedown_pool_pop(&doc->inline_buffers, label);

    if (find_link_ref(doc, ref->id))
      hoedown_pool_pop(&doc->link_refs__pool, ref);
    else
      add_link_ref(doc, ref);
  } else if (doc->mode == NORMAL_PARSING) {
    hoedown_pool_pop(&doc->inline_buffers, label);
    hoedown_pool_pop(&doc->link_refs__pool, ref);
    parse_paragraph(doc, target, data + parsed, start - parsed);
  }
  return i;
}

static inline size_t parse_quote_block_prefix(const uint8_t *data, size_t size) {
  size_t i = 0;

  // Skip three optional spaces
  while (i < size && data[i] == ' ') i++;
  if (i >= 4) return 0;

  // Angle bracket
  if (i < size && data[i] == '>') i++;
  else return 0;

  // Optional space
  if (i < size && data[i] == ' ') i++;

  return i;
}

// The strategy here is: enter dumb parsing mode and collect the quote block
// content to `work`. When an empty line is reached, we break the loop.
// When we have finished collecting content, restore parsing mode and parse it.
//
// If we find a lazy line, we [dumb-]parse the content (starting from
// `parsed`) with these lazy lines to see if they are valid lazy lines;
// we break if they aren't.
static inline size_t parse_quote_block_content(hoedown_document *doc, void *content, hoedown_buffer *work, const uint8_t *data, size_t size) {
  size_t i = 0, mark;
  size_t parsed = 0, current_size;
  enum parsing_mode original_mode = doc->mode;
  doc->mode = DUMB_PARSING;
  //FIXME: refactor

  while (1) {
    mark = i;
    while (i < size && data[i] == ' ') i++;

    // Empty line always ends the quote block
    if (i >= size || data[i] == '\n') break;

    // A prefixed line continues the block
    if (i - mark < 4 && data[i] == '>') {
      i++;
      if (i < size && data[i] == ' ') i++;

      // Put line into working buffer
      mark = i;
      while (i < size && data[i] != '\n') i++;
      if (i < size) i++;

      mapped_buffer_put(doc, work, data + mark, i - mark);
      continue;
    }

    // Possible lazy line, collect this and following possible lazy lines
    i = mark;
    current_size = work->size;
    while (1) {
      if (parse_single_block(doc, NULL, data, i, i, size, size, 0)) break;

      while (i < size && data[i] != '\n') i++;
      if (i < size) i++;

      if (i >= size || next_line_empty(data + i, size - i) || parse_quote_block_prefix(data + i, size - i))
        break;
    }
    mapped_buffer_put(doc, work, data + mark, i - mark);

    // If the next line is prefixed, the block could continue.
    // Otherwise, we can be sure it ends here and return immediately.
    if (parse_quote_block_prefix(data + i, size - i)) {
      // Can these lines continue the block?
      parsed += parse_block(doc, NULL, work->data + parsed, current_size - parsed, work->size - parsed, HOEDOWN_FT_QUOTE_BLOCK | HOEDOWN_FT_LIST);
      if (parsed == current_size) {
        work->size = current_size;
        break;
      }
      parsed = work->size;
    } else {
      // Definitively parse and return
      doc->mode = original_mode;
      parsed = parse_block(doc, content, work->data, current_size, work->size, HOEDOWN_FT_QUOTE_BLOCK | HOEDOWN_FT_LIST);
      if (parsed == current_size) {
        work->size = current_size;
        return mark;
      }

      // These are valid lazy lines, parse reminder if present and return
      if (parsed < current_size) parse_block(doc, content, work->data + parsed, work->size - parsed, work->size - parsed, 0);
      return i;
    }
  }

  // Actually parse collected content
  doc->mode = original_mode;
  parse_block(doc, content, work->data, work->size, work->size, 0);
  return mark;
}

// This block construct is a container.
static size_t parse_quote_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, mark;
  hoedown_buffer *work = NULL;
  void *content = NULL;

  // We should have a quote block prefix here
  mark = i;
  i += parse_quote_block_prefix(data + i, size - i);
  if (mark == i) return 0;

  // Get working buffer & content
  work = get_mapped_buffer(doc, &doc->block_buffers);
  work->size = 0;

  if (doc->mode == NORMAL_PARSING)
    content = doc->rndr.object_get(0, &doc->data);

  // Collect first line
  mark = i;
  while (i < size && data[i] != '\n') i++;
  if (i < size) i++;

  mapped_buffer_put(doc, work, data + mark, i - mark);

  // Parse rest of content
  i += parse_quote_block_content(doc, content, work, data + i, size - i);

  // Render!
  if (doc->mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);
    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], work->data, 0, work->size);
    doc->rndr.quote_block(target, content, &doc->data);
    doc->rndr.object_pop(content, 0, &doc->data);
  }

  pop_mapped_buffer(doc, &doc->block_buffers, work);
  return i;
}

static inline size_t parse_list_marker(const uint8_t *data, size_t size, int *is_ordered, int *number, uint8_t *character) {
  if (size < 1) return 0;

  if (data[0] == '-' || data[0] == '*' || data[0] == '+') {
    *is_ordered = 0;
    *character = data[0];

    if (1 < size && !is_space(data[1])) return 0;
    return 1;
  }

  if (is_digit_ascii(data[0])) {
    size_t i = 1;
    while (i < size && is_digit_ascii(data[i])) i++;

    if (i < size && (data[i] == '.' || data[i] == ')')) {
      // Parse the integer
      if (number) {
        *number = 0;
        for (size_t a = 0; a < i; a++)
          *number = (*number * 10) + (data[a] - '0');
      }

      *is_ordered = 1;
      *character = data[i];

      i++;
      if (i < size && !is_space(data[i])) return 0;
      return i;
    }
  }

  return 0;
}

static size_t collect_list_items__lines(hoedown_document *doc, const uint8_t *data, size_t size, int is_ordered, uint8_t character, size_t indentation, int *is_loose, hoedown_buffer *work, size_t *marker_result, size_t parsed) {
  size_t i = 0, mark = 0, result, last_position = 0, last_size = work->size;
  int double_empty = 0, is_ordered2;
  uint8_t character2;

  while (i < size) {
    mark = i;
    while (i < size && data[i] == ' ') i++;

    if (i >= size || data[i] == '\n') {
      // EMPTY LINE
      if (last_position < mark) double_empty = 1;
      if (i < size) i++;
      mapped_buffer_put(doc, work, data + mark, i - mark);
      continue;
    }

    if (i - mark < indentation && i - mark < 4 &&
        (*marker_result = parse_list_marker(data + i, size - i, &is_ordered2, NULL, &character2))) {
      // PREFIXED LINE
      if (is_ordered2 == is_ordered && character2 == character &&
          !parse_horizontal_rule(doc, NULL, data, i, i, size))
        *marker_result += i - mark;
      else
        *marker_result = 0;
      break;
    }

    if (i - mark >= indentation) {
      // INDENTED LINE
      mark += indentation;
      if (mark > i) mark = i;
      while (i < size && data[i] != '\n') i++;
      if (i < size) i++;
      mapped_buffer_put(doc, work, data + mark, i - mark);

      if ((!*is_loose && last_size + (i - mark) < work->size) || double_empty) {
        // We check if this line and the empty lines before belong to a fenced
        // code block, this determines if the list can continue or it's loose
        result = parse_block(doc, NULL, work->data + parsed, last_size - parsed, work->size - parsed, HOEDOWN_FT_FENCED_CODE_BLOCK | HOEDOWN_FT_LIST);
        if (double_empty && result < work->size - parsed) break;
        if (result <= last_size - parsed) *is_loose = 1;
      }

      last_position = i;
      last_size = work->size;
      double_empty = 0;
      continue;
    }

    // LAZY LINE
    if (last_position < mark) break;

    mark = i;
    while (i < size && data[i] != '\n') i++;
    if (i < size) i++;
    mapped_buffer_put(doc, work, data + mark, i - mark);

    result = parse_block(doc, NULL, work->data + parsed, last_size - parsed, work->size - parsed, HOEDOWN_FT_LIST | HOEDOWN_FT_QUOTE_BLOCK);
    if (result == last_size - parsed) break;
    if (result < last_size - parsed && parse_single_block(doc, NULL, work->data, parsed, last_size, work->size, work->size, 0)) break;
    last_position = i;
    last_size = work->size;
    double_empty = 0;
  }


  if (*marker_result && last_position < mark) {
    // Before return, check if the empty lines end the list or make it loose.
    if (double_empty) {
      result = parse_block(doc, NULL, work->data + parsed, last_size - parsed, work->size - parsed, HOEDOWN_FT_FENCED_CODE_BLOCK | HOEDOWN_FT_LIST);
      if (result < work->size - parsed) *marker_result = 0;
    } else *is_loose = 1;
  }

  mapped_buffer_rewind(doc, work, last_size);
  return (*marker_result) ? mark : last_position;
}

// **Important:** This assumes it's being called with DUMB_PARSING mode set.
//
// List parsing works as follows: `collect_list_items` and its associate
// functions verify at least one valid list item, and advance through all
// subsequent items of the same type, collecting and concatenating their
// contents into the `work` buffer. At the same time, it saves the offsets
// at which every item's contents ends in a second buffer, `slices`.
//
// Here is where most of the magic happens; lines are tested for lazyness,
// double empty lines are resolved, etc. As always, if the function returns
// zero it means no list is started at this position in the input,
// otherwise the end of the list is returned.
static size_t collect_list_items(hoedown_document *doc, const uint8_t *data, size_t size, int *is_ordered, int *is_loose, int *number, hoedown_buffer *work, hoedown_buffer *slices) {
  uint8_t character;
  size_t i = 0, mark, result, indentation, parsed;

  // Parse first line marker
  while (i < size && data[i] == ' ') i++;
  if (i > 3) return 0;
  result = parse_list_marker(data + i, size - i, is_ordered, number, &character);
  if (!result) return 0;
  result += i;
  i = 0;

  while (i < size && result) {
    // Start new item!
    parsed = work->size;
    i += result;

    mark = i;
    while (i < size && data[i] == ' ') i++;
    if (i - mark >= 5) i = mark + 1;

    indentation = result + (i - mark);
    if (mark == i) indentation++;

    mark = i;
    while (i < size && data[i] != '\n') i++;
    if (i < size) i++;
    mapped_buffer_put(doc, work, data + mark, i - mark);

    // Collect rest of the lines
    result = 0;
    i += collect_list_items__lines(doc, data + i, size - i, *is_ordered, character, indentation, is_loose, work, &result, parsed);

    // End the list item
    hoedown_buffer_put(slices, (const uint8_t *)&work->size, sizeof(size_t));
    hoedown_buffer_put(slices, (const uint8_t *)&i, sizeof(size_t));
  }

  return i;
}

// This is the main entry point for list parsing. It uses `collect_list_items`
// above to parse the list items and collect their contents, and then iterates
// through `work` and `slices` and renders each individual item, then the list
// itself.
static size_t parse_list(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t i = start, slice, current_is_tight = doc->is_tight;
  enum parsing_mode current_mode = doc->mode;
  int is_ordered, is_loose = (current_mode == NORMAL_PARSING) ? 0 : 1, number = 0;
  void *content;

  hoedown_buffer *work = get_mapped_buffer(doc, &doc->block_buffers);
  hoedown_buffer *slices = hoedown_pool_get(&doc->inline_buffers);
  work->size = slices->size = 0;

  // 1. Collect list items
  doc->mode = DUMB_PARSING;
  i += collect_list_items(doc, data + i, size - i, &is_ordered, &is_loose, &number, work, slices);
  doc->mode = current_mode;

  if (i == start) {
    hoedown_pool_pop(&doc->inline_buffers, slices);
    pop_mapped_buffer(doc, &doc->block_buffers, work);
    return 0;
  }

  if (current_mode == DUMB_PARSING) {
    hoedown_pool_pop(&doc->inline_buffers, slices);
    pop_mapped_buffer(doc, &doc->block_buffers, work);
    return i;
  }


  // 2. Parse / render list items
  if (current_mode == NORMAL_PARSING)
    content = doc->rndr.object_get(0, &doc->data);
  if (!is_loose)
    doc->is_tight = doc->current_nesting + 1;

  size_t offset = 0, source = 0;
  for (slice = 0; slice < slices->size; slice += 2*sizeof(size_t)) {
    size_t new_offset = *((size_t *) &slices->data[slice]);
    size_t new_source = *((size_t *) &slices->data[slice+sizeof(size_t)]);
    if (current_mode == NORMAL_PARSING) {
      void *item_content = doc->rndr.object_get(0, &doc->data);
      parse_block(doc, item_content, work->data + offset, new_offset - offset, new_offset - offset, 0);
      set_buffer_data(&doc->data.src[0], data + start, source, new_source);
      set_buffer_data(&doc->data.src[1], work->data, offset, new_offset);
      doc->rndr.list_item(content, item_content, is_ordered, !is_loose, &doc->data);
      doc->rndr.object_pop(item_content, 0, &doc->data);
    } else {
      parse_block(doc, NULL, work->data + offset, new_offset - offset, new_offset - offset, 0);
    }
    offset = new_offset;
    source = new_source;
  }

  doc->is_tight = current_is_tight;


  // 3. Render the list itself
  if (current_mode == NORMAL_PARSING) {
    parse_paragraph(doc, target, data + parsed, start - parsed);
    set_buffer_data(&doc->data.src[0], data, start, i);
    set_buffer_data(&doc->data.src[1], data, start, i);
    doc->rndr.list(target, content, is_ordered, !is_loose, number, &doc->data);
    doc->rndr.object_pop(content, 0, &doc->data);
  }

  hoedown_pool_pop(&doc->inline_buffers, slices);
  pop_mapped_buffer(doc, &doc->block_buffers, work);
  return i;
}



// BLOCK PARSING
// =============
//
// Here's the implementation of `parse_block`, the entry point for block
// parsing. This will skip (at most) the first three spaces of a line.
// It'll look up the char triggers associated with the next character in the
// line, and will call them in order.
//
// If one of the char triggers succeeds parsing, no more char triggers will
// be called, and `parse_block` will advance to the returned position.
// If all of the char triggers returned `0`, or there were no matching
// char triggers to call, it'll skip to the next line.
//
// `set_block_chars` is also implemented, which associates block char triggers
// to their characters depending on the enabled features. It's called by the
// main constructor.
//
// ### Nesting
//
// Block parsing functions can call `parse_block` or `parse_inline` to parse
// child content.
//
// `doc->current_nesting` is incremented every time `parse_block()` enters,
// and decremented every time it exits.
//
// If incrementing `doc->current_nesting` would make it greater than
// `doc->max_nesting`, then `parse_block()` will refuse to parse and return
// immediately.
//
// ### Lazy content
//
// For regular block parsing, `parse_block` should be called with the
// same value in `size` and `lazy_size`.
//
// If the caller wants to test for valid lazy lines at the end of the data,
// it should set `size` to the size of the data excluding lazy lines, and
// `lazy_size` to the size including lazy lines. `lazy_ft` should be set to
// indicate which constructs are allowed to take the lazy lines.
//
//  - If the returned value equals `size`, there were no valid lazy lines.
//
//  - If the returned value is greater than `size`, there were some (but not
//    necessarily all) valid lazy lines, that were parsed as part of one of the
//    indicated constructs in `lazy_ft`.
//
//  - If the returned value is less than `size`, (some of the) lazy lines
//    continuate a paragraph just at the end of the non-lazy input. Neither
//    the paragraph nor the lazy lines were parsed.
//
// ### Parsing modes
//
// `parse_block` and block parsing functions are affected by the setting of
// `doc->mode`. This is called the "parsing mode", and can be one of:
//
//  - Dumb parsing (`DUMB_PARSING`). In this mode, blocks are parsed, but
//    their content is not. Anything is rendered, so the target can be `NULL`
//    or any value. This mode is useful to detect the type of block present or
//    test for lazy lines without affecting output.
//
//  - Marker parsing (`MARKER_PARSING`). In this mode, blocks and block content
//    are parsed, but only markers have their inline content parsed and
//    rendered (the marker itself needs no rendering). This mode should *never*
//    set directly by any parsing function, it's set only once by
//    `hoedown_document_render` to do a first pass on the input.
//
//  - Normal parsing (`NORMAL_PARSING`). This the mode where most parsing
//    happens. Blocks, block content and inline content are parsed and
//    rendered, except markers, which are treated as if `DUMB_PARSING` was set.
//
// When rendering block input, `hoedown_document_render` will call
// `parse_block` with `MARKER_PARSING` first, then with `NORMAL_PARSING`.

// If you use this, make sure you don't give it empty lines at start position.
static inline size_t parse_single_block(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size, size_t lazy_size, hoedown_features lazy_ft) {
  size_t i = start, result;
  block_char_entry *entry;

  // Is this an empty line? If so, flush accumulated input and return.
  while (i < size && data[i] == ' ') i++;
  if (i >= size || data[i] == '\n') {
    parse_paragraph(doc, target, data + parsed, start - parsed);
    return i + 1;
  }

  // Good, line is not empty. Advance past the first three spaces at most.
  if (i - start > 3) i = start + 3;

  // Call char triggers in order
  for (entry = doc->block_chars[data[i]]; entry; entry = entry->next) {
    if (parsed < start && !entry->can_interrupt) continue;
    result = entry->trigger(doc, target, data, parsed, start,
    (lazy_ft & entry->lazy_ft) ? lazy_size : size);
    if (result) return result;
  }

  return 0;
}

static size_t parse_block(hoedown_document *doc, void *target, const uint8_t *data, size_t size, size_t lazy_size, hoedown_features lazy_ft) {
  if (doc->current_nesting >= doc->max_nesting) return size;
  doc->current_nesting++;

  size_t i = 0, parsed = 0, result;

  while (i < size) {
    // Try to parse a construct (or empty line) here
    if ((result = parse_single_block(doc, target, data, parsed, i, size, lazy_size, lazy_ft))) {
      i = parsed = result;
      continue;
    }

    // Nothing could be parsed on this line, skip to the next
    while (i < size && data[i] != '\n') i++;
    if (i < size) i++;
  }

  // Parse rest of the content as paragraph
  if (lazy_size == size && parsed < i) {
    parse_paragraph(doc, target, data + parsed, i - parsed);
    parsed = i;
  }

  doc->current_nesting--;
  return parsed;
}

static void set_block_chars(hoedown_document *doc, hoedown_features ft) {
  if (doc->ft & HOEDOWN_FT_SETEXT_HEADER)
    register_block_chars(doc, "-=", parse_setext_header, 1, HOEDOWN_FT_SETEXT_HEADER);

  if (doc->ft & HOEDOWN_FT_INDENTED_CODE_BLOCK)
    register_block_chars(doc, " ", parse_indented_code_block, 0, HOEDOWN_FT_INDENTED_CODE_BLOCK);

  if (doc->ft & HOEDOWN_FT_FENCED_CODE_BLOCK)
    register_block_chars(doc, "`~", parse_fenced_code_block, 1, HOEDOWN_FT_FENCED_CODE_BLOCK);

  if (doc->ft & HOEDOWN_FT_HORIZONTAL_RULE)
    register_block_chars(doc, "*-_", parse_horizontal_rule, 1, HOEDOWN_FT_HORIZONTAL_RULE);

  if (doc->ft & HOEDOWN_FT_QUOTE_BLOCK)
    register_block_chars(doc, ">", parse_quote_block, 1, HOEDOWN_FT_QUOTE_BLOCK);

  if (doc->ft & HOEDOWN_FT_LIST)
    register_block_chars(doc, "-*+0123456789", parse_list, 1, HOEDOWN_FT_LIST);

  if (doc->ft & HOEDOWN_FT_ATX_HEADER)
    register_block_chars(doc, "#", parse_atx_header, 1, HOEDOWN_FT_ATX_HEADER);

  if (doc->ft & HOEDOWN_FT_HTML_BLOCK)
    register_block_chars(doc, "<", parse_html_block, 1, HOEDOWN_FT_HTML_BLOCK);

  if (doc->ft & HOEDOWN_FT_LINK)
    register_block_chars(doc, "[", parse_link_reference, 0, HOEDOWN_FT_LINK);
}



// INLINE PARSING
// ==============
//
// Here's the implementation of `parse_inline`, the entry point for inline
// parsing. It calls char triggers as explained in the Parsing section.
//
// Just as `parse_block`, `parse_inline` will increment and decrement
// `doc->current_nesting` when it enters and exits, respectively, and will
// refuse to parse if `doc->current_nesting` would exceed `doc->max_nesting`.
//
// `parse_inline` will create and initialize `doc->inline_data` when it enters,
// and remove it when it exits, by replacing it with the previous one, so that
// eventually `doc->inline_data` will be `NULL` again.
//
// The `inline_data` structure is the ideal place to store state fields only
// belonging to a single call of `parse_inline`, as opposed to the
// `hoedown_document` struct whose fields are inherited when the parser recurses.
// `inline_data` stores the nesting stack (more on that later) and some other
// flags needed to prevent abuse through malicious input.
//
// `set_inline_chars` is also implemented, which associates inline char
// triggers to their characters depending on the enabled features. It's called
// by the main constructor.
//
// ### Nesting
//
// There are two ways to implement nesting in inline parsing. The first one
// is to use the so called "nesting stack", which allows for lowest-priority
// lightweight nesting that never backtracks (example below).
//
// The second way is to call `parse_inline` and optionally pass a delimiter
// at which `parse_inline` should stop parsing if it's not consumed by any
// char trigger. Because you'll probably backtrack if the delimiter is not
// found, you need to take several measures or the parser will block with
// malicious input that abuses nesting. See for example `parse_link`.
//
// #### Nesting stack example
//
// Instead of recursing when a nesting construct (such as emphasis) is being
// parsed, the approach is to add an entry to the so called "nesting stack",
// and replace `doc->inline_data->target` with a brand new target, where the
// next content will be parsed.
//
// As an example, imagine this input:
//
//     `code`*`inside`*`more`
//
// This would be the state of `doc->inline_data` before calling
// `parse_emphasis` to parse the opening bracket:
//
//     target: <target A> (code span "code")
//
//     nesting: NULL
//
// `doc->inline_data` after parsing the first `*`:
//
//     target: <target B> (no nodes)
//
//     nesting:
//       parent: <target A> (code span "code")
//       ft: HOEDOWN_FT_EMPHASIS
//       start: 8
//       end: 9
//
//       previous: NULL
//
// Then, before calling `parse_emphasis` to parse the second `*`:
//
//     target: <target B> (code span "inside")
//
//     nesting:
//       parent: <target A> (code span "code")
//       ft: HOEDOWN_FT_EMPHASIS
//       start: 8
//       end: 9
//
//       previous: NULL
//
// `parse_emphasis` will then render the emphasis using <target B> as contents.
// This will be `doc->inline_data` after `parse_emphasis` finishes:
//
//     target: <target A> (code span "inside"; emphasis)
//
//     nesting: NULL
//
// Just after exiting, `doc->inline_data` would look like:
//
//     target: <target A> (code span "inside"; emphasis; code span "more")
//
//     nesting: NULL
//
// In case a closing `*` wasn't found, nesting entries would be *discarded*
// by merging the current target (target B in this case) into the entry
// `parent` target (target A), after rendering the data `start..end` range
// as a string.

static inline size_t parse_single_inline(hoedown_document *doc, void *target, const uint8_t *data, size_t parsed, size_t start, size_t size) {
  size_t result;
  inline_char_entry *entry;

  // Call char triggers in order
  for (entry = doc->inline_chars[data[start]]; entry; entry = entry->next) {
    result = entry->trigger(doc, target, data, parsed, start, size);
    if (result) return result;
  }

  return 0;
}

static size_t parse_inline(hoedown_document *doc, void *target, const uint8_t *data, size_t size, uint8_t delimiter) {
  if (doc->current_nesting >= doc->max_nesting) return size;
  doc->current_nesting++;

  size_t i = 0, parsed = 0, result;
  inline_char_entry **inline_chars = doc->inline_chars;
  open_inline_data(doc, target, data);

  while (i < size) {
    // Optimization: Skip any chars we're not interested in
    if (delimiter)
      while (i < size && inline_chars[data[i]] == NULL && data[i] != delimiter) i++;
    else
      while (i < size && inline_chars[data[i]] == NULL) i++;
    if (i >= size) break;

    // Try to parse a construct here
    if ((result = parse_single_inline(doc, doc->inline_data->target, data, parsed, i, size))) {
      i = parsed = result;
      continue;
    }

    // Is this the delimiter we were looking for?
    if (delimiter && data[i] == delimiter) break;

    // Nothing could be parsed here, skip to the next char
    i++;
  }

  // Parse rest of content as string
  discard_nestings(doc, NULL);
  parse_string(doc, target, data + parsed, i - parsed);

  close_inline_data(doc, target);
  doc->current_nesting--;
  return i;
}

static void set_inline_chars(hoedown_document *doc, hoedown_features ft) {
  if (ft & HOEDOWN_FT_MATH)
    register_inline_chars(doc, "\\$", parse_math);

  if (ft & HOEDOWN_FT_ESCAPE)
    register_inline_chars(doc, "\\", parse_escape);

  if (ft & HOEDOWN_FT_LINEBREAK)
    register_inline_chars(doc, "\n", parse_linebreak);

  if (ft & HOEDOWN_FT_HTML)
    register_inline_chars(doc, "<", parse_html);

  if (ft & HOEDOWN_FT_URI_AUTOLINK)
    register_inline_chars(doc, "<", parse_uri_autolink);

  if (ft & HOEDOWN_FT_EMAIL_AUTOLINK)
    register_inline_chars(doc, "<", parse_email_autolink);

  if (ft & HOEDOWN_FT_CODE)
    register_inline_chars(doc, "`", parse_code);

  if (ft & HOEDOWN_FT_ENTITY)
    register_inline_chars(doc, "&", parse_entity);

  if (ft & HOEDOWN_FT_EMPHASIS)
    register_inline_chars(doc, "*_", parse_emphasis);

  if (ft & HOEDOWN_FT_LINK)
    register_inline_chars(doc, "[", parse_link);

  if (ft & HOEDOWN_FT_SIDENOTE)
    register_inline_chars(doc, "^", parse_sidenote);

  if (ft & HOEDOWN_FT_SUPERSCRIPT)
    register_inline_chars(doc, "^", parse_superscript);

  if (ft & HOEDOWN_FT_STRIKETHROUGH)
    register_inline_chars(doc, "~", parse_strikethrough);

  if (ft & HOEDOWN_FT_HIGHLIGHT)
    register_inline_chars(doc, "=", parse_highlight);

  if (ft & HOEDOWN_FT_EMOJI)
    register_inline_chars(doc, ":", parse_emoji);

  if (ft & HOEDOWN_FT_TYPOGRAPHY) {
    register_inline_chars(doc, "'\"", parse_typography_quote);
    register_inline_chars(doc, "-", parse_typography_dash);
    register_inline_chars(doc, ".", parse_typography_ellipsis);
    register_inline_chars(doc, "(", parse_typography_copyright);
  }


  // LOW-PRIORITY FUNCTIONS (register at the very end)

  if (ft & HOEDOWN_FT_SIDENOTE)
    register_inline_chars(doc, "]", parse_bracket);

  if (ft & HOEDOWN_FT_SUPERSCRIPT)
    register_inline_chars(doc, ")", parse_parenthesis);

  if (ft & HOEDOWN_FT_LINK)
    register_inline_chars(doc, "[]", parse_brackets);
}


// Discard inline nestings up to, but *not including*, the passed nesting.
// The passed nesting MUST belong to the nesting stack when this is called.
static void discard_nestings(hoedown_document *doc, inline_nesting *top) {
  inline_data *data = doc->inline_data;
  inline_nesting *entry;

  // Discard entries until `top` is reached
  for (entry = data->nesting; entry != top; entry = entry->previous) {
    parse_string(doc, entry->parent, data->data + entry->parsed, entry->end - entry->parsed);
    doc->rndr.object_merge(entry->parent, data->target, 1, &doc->data);
    doc->rndr.object_pop(data->target, 1, &doc->data);
    data->target = entry->parent;
    doc->current_nesting--;
    hoedown_pool_pop(&doc->inline_nesting__pool, entry);
  }

  data->nesting = top;
}

// Switch parsing to a new nesting entry, and return it.
static inline_nesting *open_nesting(hoedown_document *doc, hoedown_features ft, size_t parsed, size_t start, size_t end) {
  inline_data *data = doc->inline_data;
  inline_nesting *entry = hoedown_pool_get(&doc->inline_nesting__pool);

  entry->parent = data->target;
  entry->ft = ft;
  entry->parsed = parsed;
  entry->start = start;
  entry->end = end;

  doc->inline_data->target = doc->rndr.object_get(1, &doc->data);
  assert(doc->current_nesting < doc->max_nesting);
  doc->current_nesting++;
  entry->previous = data->nesting;
  data->nesting = entry;
  return entry;
}

// Destroy a nesting entry and switch parsing to its parent.
static void close_nesting(hoedown_document *doc, inline_nesting *entry) {
  inline_data *data = doc->inline_data;
  assert(data->nesting == entry);

  doc->rndr.object_pop(data->target, 1, &doc->data);
  data->target = entry->parent;
  doc->current_nesting--;
  data->nesting = entry->previous;
  hoedown_pool_pop(&doc->inline_nesting__pool, entry);
}



// PUBLIC METHODS & INITIALIZATION
// ===============================
//
// There it is. The exposed API, which includes the constructor, destructor,
// preprocessing logic, and `hoedown_document_render`. The method prepares the
// `hoedown_document` struct for a render, calls `parse_block` or
// `parse_inline` as appropiate, then cleans everything up.
//
// Other things can be implemented here, such as exposed internal methods.

static inline void restrict_features(const hoedown_renderer *rndr, hoedown_features *ft) {
  hoedown_features not_present = 0;

  if (!rndr->indented_code_block)
    not_present |= HOEDOWN_FT_INDENTED_CODE_BLOCK;
  if (!rndr->fenced_code_block)
    not_present |= HOEDOWN_FT_FENCED_CODE_BLOCK;
  if (!rndr->horizontal_rule)
    not_present |= HOEDOWN_FT_HORIZONTAL_RULE;
  if (!rndr->atx_header)
    not_present |= HOEDOWN_FT_ATX_HEADER;
  if (!rndr->setext_header)
    not_present |= HOEDOWN_FT_SETEXT_HEADER;
  if (!rndr->list || !rndr->list_item)
    not_present |= HOEDOWN_FT_LIST;
  if (!rndr->quote_block)
    not_present |= HOEDOWN_FT_QUOTE_BLOCK;
  if (!rndr->html_block)
    not_present |= HOEDOWN_FT_HTML_BLOCK;

  if (!rndr->escape)
    not_present |= HOEDOWN_FT_ESCAPE;
  if (!rndr->linebreak)
    not_present |= HOEDOWN_FT_LINEBREAK;
  if (!rndr->uri_autolink)
    not_present |= HOEDOWN_FT_URI_AUTOLINK;
  if (!rndr->email_autolink)
    not_present |= HOEDOWN_FT_EMAIL_AUTOLINK;
  if (!rndr->html)
    not_present |= HOEDOWN_FT_HTML;
  if (!rndr->entity)
    not_present |= HOEDOWN_FT_ENTITY;
  if (!rndr->code)
    not_present |= HOEDOWN_FT_CODE;
  if (!rndr->emphasis)
    not_present |= HOEDOWN_FT_EMPHASIS;
  if (!rndr->link)
    not_present |= HOEDOWN_FT_LINK;
  if (!rndr->math)
    not_present |= HOEDOWN_FT_MATH;
  if (!rndr->superscript)
    not_present |= HOEDOWN_FT_SUPERSCRIPT;
  if (!rndr->strikethrough)
    not_present |= HOEDOWN_FT_STRIKETHROUGH;
  if (!rndr->highlight)
    not_present |= HOEDOWN_FT_HIGHLIGHT;
  if (!rndr->sidenote)
    not_present |= HOEDOWN_FT_SIDENOTE;
  if (!rndr->emoji)
    not_present |= HOEDOWN_FT_EMOJI;
  if (!rndr->typography)
    not_present |= HOEDOWN_FT_TYPOGRAPHY;

  // Remove not present features from *ft
  *ft &= ~not_present;
}

hoedown_document *hoedown_document_new(
  hoedown_renderer *renderer,
  hoedown_features features,
  size_t max_nesting
) {
  // Validate parameters
  restrict_features(renderer, &features);

  // Allocate struct
  hoedown_document *doc = hoedown_malloc(sizeof(hoedown_document));

  // Renderer stuff
  memcpy(&doc->rndr, renderer, sizeof(hoedown_renderer));
  doc->data.doc = (hoedown_internal *)doc;
  doc->data.opaque = renderer->opaque;
  memset(&doc->data.src, 0, sizeof(doc->data.src));

  // Common parsing
  doc->ft = features;
  doc->current_nesting = 0;
  doc->max_nesting = max_nesting;

  // Block parsing
  hoedown_buffer_pool_init(&doc->block_buffers, 4, 64);
  memset(&doc->block_chars, 0, sizeof(doc->block_chars));
  hoedown_pool_init(&doc->block_chars__pool, 4, _new_block_char_entry, _free_pool_item, NULL);
  set_block_chars(doc, features);
  doc->mode = NORMAL_PARSING;
  doc->is_tight = 0;
  doc->inside_footnote = 0;

  // Inline parsing
  hoedown_buffer_pool_init(&doc->inline_buffers, 8, 64);
  memset(&doc->inline_chars, 0, sizeof(doc->inline_chars));
  hoedown_pool_init(&doc->inline_chars__pool, 4, _new_inline_char_entry, _free_pool_item, NULL);
  set_inline_chars(doc, features);
  doc->inline_data = NULL;
  hoedown_pool_init(&doc->inline_data__pool, 2, _new_inline_data, _free_pool_item, NULL);
  hoedown_pool_init(&doc->inline_nesting__pool, 4, _new_inline_nesting, _free_pool_item, NULL);
  doc->inside_link = 0;
  doc->plain_links_forbidden = 0;

  // Marker parsing
  memset(&doc->link_refs, 0, sizeof(doc->link_refs));
  hoedown_pool_init(&doc->link_refs__pool, 8, _new_link_ref, _free_pool_item, NULL);

  // Other features: source mapping
  doc->mapping = NULL;
  hoedown_pool_init(&doc->mapping__pool, 8, _new_buffer_mapping, _free_buffer_mapping, NULL);

  return doc;
}

void hoedown_document_free(hoedown_document *doc) {
  if (!doc) return;

  // Reset the tables
  reset_block_chars(doc);
  reset_inline_chars(doc);

  // Free the pools
  hoedown_pool_uninit(&doc->block_buffers);
  hoedown_pool_uninit(&doc->block_chars__pool);
  hoedown_pool_uninit(&doc->inline_buffers);
  hoedown_pool_uninit(&doc->inline_chars__pool);
  hoedown_pool_uninit(&doc->inline_data__pool);
  hoedown_pool_uninit(&doc->inline_nesting__pool);
  hoedown_pool_uninit(&doc->link_refs__pool);
  hoedown_pool_uninit(&doc->mapping__pool);

  free(doc);
}


void *hoedown_document_render(
  hoedown_document *doc,
  const uint8_t *data, size_t size,
  int is_inline, void *request
) {
  // Add root mapping
  hoedown_buffer original = {(uint8_t *)data, size, 0, 0, NULL, NULL};
  hoedown_range root_range = {0, size, 0};
  buffer_mapping root_mapping = {NULL, &original, {&root_range,1,0,0}, size, 0,0};
  doc->mapping = &root_mapping;

  // Preprocess the input
  hoedown_buffer *text = NULL;
  if (doc->ft & HOEDOWN_FT_PREPROCESS) {
    text = get_mapped_buffer(doc, &doc->block_buffers);
    text->size = 0;
    normalize_spacing(doc, text, data, size);
    data = text->data;
    size = text->size;
  }

  // Prepare
  doc->data.request = request;
  set_buffer_data(&doc->data.src[0], original.data, 0, original.size);
  doc->rndr.render_start(is_inline, &doc->data);
  void *target = doc->rndr.object_get(is_inline, &doc->data);

  // Render!
  if (is_inline)
    parse_inline(doc, target, data, size, 0);
  else {
    doc->mode = MARKER_PARSING;
    parse_block(doc, NULL, data, size, size, 0);
    doc->mode = NORMAL_PARSING;
    parse_block(doc, target, data, size, size, 0);
  }

  // Finish & cleanup
  set_buffer_data(&doc->data.src[0], original.data, 0, original.size);
  void *result = doc->rndr.render_end(target, is_inline, &doc->data);

  if (text) pop_mapped_buffer(doc, &doc->block_buffers, text);
  assert(doc->current_nesting == 0);
  assert(doc->mode == NORMAL_PARSING);
  assert(doc->block_buffers.size == doc->block_buffers.isize);
  assert(!doc->is_tight);
  assert(!doc->inside_footnote);
  assert(doc->inline_buffers.size == doc->inline_buffers.isize);
  assert(doc->inline_data == NULL);
  assert(doc->inline_data__pool.size == doc->inline_data__pool.isize);
  assert(doc->inline_nesting__pool.size == doc->inline_nesting__pool.isize);
  assert(!doc->inside_link);
  assert(!doc->plain_links_forbidden);
  pop_link_refs(doc);
  memset(&doc->link_refs, 0, sizeof(doc->link_refs));
  assert(doc->link_refs__pool.size == doc->link_refs__pool.isize);
  assert(doc->mapping == &root_mapping);
  doc->mapping = NULL;
  assert(doc->mapping__pool.size == doc->mapping__pool.isize);
  return result;
}


// Exposed internals & utilities

void hoedown_preprocess(hoedown_buffer *ob, const uint8_t *data, size_t size) {
  normalize_spacing(NULL, ob, data, size);
}

int hoedown_document_locate(hoedown_internal *doc, hoedown_list *ranges, const uint8_t *data, size_t size) {
  return locate_ranges(ranges, data, size, ((hoedown_document *)doc)->mapping, NULL);
}

