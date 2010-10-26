#ifndef _CPARSE_DEF_H_
#define _CPARSE_DEF_H_

typedef struct {
  char *str;
  int deactivated;
} lex_ret_t;

#define YYSTYPE lex_ret_t

#endif /* _CPARSE_DEF_H_ */

