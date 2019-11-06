#include "utils.h"
#include <stdlib.h>

Strings*
add_string(Strings  *head,
           char     *str)
{
  if (head == 0) {
    head = malloc(sizeof(Strings));
    head->size = 1;
    head->str = str;
    head->next = 0;
    return head;
  }
  head->size ++;
  if (head->next) {
    add_string(head->next, str);
  } else {
    Strings * next = malloc(sizeof(Strings));
    next->size = 1;
    next->str = str;
    next->next = 0;
    head->next = next;
  }
  return head;
}

void
free_strings (Strings* head)
{
  if (head)
  {
    if (head->str)
      free(head->str);
    free(head->next);
    free(head);
  }
}

void
remove_char(char *source,
            char  target)
{

  char* i = source;
  char* j = source;
  while(*j != 0)
  {
    *i = *j++;
    if(*i != target)
      i++;
  }
  *i = 0;
}

char*
clean_string (char    *string,
              size_t  size)
{
 char* i = string;
 char* j = string;
 size_t flag = 0;
 while(*j != 0)
 {
   *i = *j++;
   if (flag || *i != '\\'){
     i++;
     flag = 0;
   } else{
     flag = 1;
   }
 }
 *i = 0;
 return string;
}
