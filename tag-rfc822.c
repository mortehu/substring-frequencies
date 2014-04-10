#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum tag {
  TAG_OTHER,
  TAG_SCRIPT,
  TAG_STYLE
};

enum mode {
  MODE_ANTICIPATING_HEADER,
  MODE_HEADER,
  MODE_HEADER_PAYLOAD_BOUNDARY,
  MODE_COMMENT,
  MODE_TAG,
  MODE_TAG_ATTRIBUTES,
  MODE_SCRIPT,
  MODE_CDATA,
  MODE_TEXT,
  MODE_DOCUMENT_BOUNDARY,
  MODE_INVALID
};

static char context[16];
static size_t context_offset = 0;

static int HasContext(const char *str) {
  size_t i, length;

  length = strlen(str);

  for (i = 0; i < length; ++i)
    if (context[(context_offset + 15 - i) & 15] != str[length - i - 1])
      return 0;

  return 1;
}

int main(int argc, char **argv) {
  int ch;
  enum mode m = MODE_ANTICIPATING_HEADER;
  enum tag tag = TAG_OTHER;

  while (EOF != (ch = getchar())) {
    enum mode next_mode = MODE_INVALID;
    unsigned int class;

    if (!ch) {
      m = MODE_DOCUMENT_BOUNDARY;
      next_mode = MODE_ANTICIPATING_HEADER;
    } else {
      switch (m) {
        case MODE_ANTICIPATING_HEADER:

          if (ch == '\n')
            m = MODE_HEADER_PAYLOAD_BOUNDARY;
          else
            m = MODE_HEADER;

          break;

        case MODE_HEADER:

          if (ch == '\n') next_mode = MODE_ANTICIPATING_HEADER;

          break;

        case MODE_HEADER_PAYLOAD_BOUNDARY:

          if (ch == '<') {
            m = MODE_TAG;
            tag = TAG_OTHER;
          } else
            m = MODE_TEXT;

          break;

        case MODE_COMMENT:

          if (ch == '>' && HasContext("--")) next_mode = MODE_TEXT;

          break;

        case MODE_SCRIPT:

          if (ch == '>' && HasContext("</script")) next_mode = MODE_TEXT;

          break;

        case MODE_TAG:

          if (ch == '>' || isspace(ch)) {
            if (HasContext("<script"))
              tag = TAG_SCRIPT;
            else if (HasContext("<style"))
              tag = TAG_STYLE;
          }

          if (ch == '>') {
            if (tag == TAG_SCRIPT)
              next_mode = MODE_SCRIPT;
            else
              next_mode = MODE_TEXT;
          } else if (ch == '[' && HasContext("<![CDATA"))
            next_mode = MODE_CDATA;
          else if (ch == '-' && HasContext("<!-"))
            next_mode = MODE_COMMENT;
          else if (isspace(ch))
            m = MODE_TAG_ATTRIBUTES;

          break;

        case MODE_TAG_ATTRIBUTES:

          if (ch == '>') {
            if (tag == TAG_SCRIPT)
              next_mode = MODE_SCRIPT;
            else
              next_mode = MODE_TEXT;
          }

          break;

        case MODE_CDATA:

          if (ch == '>' && HasContext("]]")) m = MODE_TEXT;

          break;

        case MODE_TEXT:

          if (ch == '<') {
            m = MODE_TAG;
            tag = TAG_OTHER;
          }

          break;

        case MODE_DOCUMENT_BOUNDARY:
          assert("!Got MODE_DOCUMENT_BOUNDARY");
        case MODE_INVALID:
          assert("!Got MODE_INVALID");
      }
    }

    switch (m) {
      case MODE_HEADER:
        class = 1;
        break;
      case MODE_CDATA:
        class = 2;
        break;
      case MODE_COMMENT:
        class = 3;
        break;
      case MODE_SCRIPT:
        class = 4;
        break;
      case MODE_TAG:
        class = 5;
        break;
      case MODE_TAG_ATTRIBUTES:
        class = 6;
        break;
      case MODE_TEXT:
        class = (tag == TAG_STYLE) ? 7 : 0;
        break;
      default:
        class = 0;
    }

    putchar('A' + class);
    putchar(ch);

    context[context_offset++ & 15] = ch;

    if (next_mode != MODE_INVALID) m = next_mode;
  }

  return EXIT_SUCCESS;
}
