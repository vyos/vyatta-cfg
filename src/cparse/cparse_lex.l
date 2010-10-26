/* flex -o cparse_lex.c cparse_lex.l */
/* definitions */
%x sComment
%x sID
%x sValue
%x sQStr
%option noyywrap

ID ([-[:alnum:]_]+)
SPACE ([[:space:]]{-}[\n])

%{
#include <string.h>
#include "cparse_def.h"
#include "cparse.h"

#define STR_BUF_INC 4096

int line_number = 0;
int node_deactivated = 0;
char *str_buf = NULL;
char *out_buf = NULL;
char *str_ptr = NULL;
size_t str_buf_len = 0;

void
append_str(char *text)
{
  size_t tlen = strlen(text);
  size_t slen = str_ptr - str_buf;
  if (!str_buf || (slen + tlen) >= str_buf_len) {
    str_buf_len += STR_BUF_INC;
    str_buf = realloc(str_buf, str_buf_len);
    out_buf = realloc(out_buf, str_buf_len);
    if (!str_buf || !out_buf) {
      printf("realloc failed\n");
      exit(1);
    }
    str_ptr = str_buf + slen;
  }
  strcpy(str_ptr, text);
  str_ptr += tlen;
}

void
set_ret_str()
{
  *str_ptr = 0;
  strcpy(out_buf, str_buf);
  str_ptr = str_buf;
}

%}

%%

<INITIAL>"/*" {
  BEGIN(sComment);
}

<sComment>[^*\n]* {
  append_str(yytext);
}

<sComment>\*[^/] {
  append_str(yytext);
}

<sComment>\n {
  append_str(yytext);
  ++line_number;
}

<sComment>"*/" {
  set_ret_str();
  yylval.str = strdup(out_buf);
  BEGIN(INITIAL);
  return COMMENT;
}

<INITIAL>! {
  node_deactivated = 1;
}

<INITIAL>[[:space:]]+ {
}

<INITIAL>\} {
  node_deactivated = 0;
  return RIGHTB;
}

<INITIAL>{ID} {
  yylval.str = strdup(yytext);
  yylval.deactivated = node_deactivated;
  node_deactivated = 0;
  BEGIN(sID);
  return NODE;
}

<sID>:?{SPACE}+[^{\n] {
  unput(yytext[yyleng - 1]);
  BEGIN(sValue);
}

<sID>{SPACE}+ {
}

<sID>\{ {
  BEGIN(INITIAL);
  return LEFTB;
}

<sID>\n {
  ++line_number;
  BEGIN(INITIAL);
}

<sValue>{SPACE}+ {
  /* ignore spaces */
}

<sValue>\" {
  /* quoted string */
  BEGIN(sQStr);
}

<sQStr>[^\"\\\n]+ {
  append_str(yytext);
}

<sQStr>\\. {
  char tmp[2] = { yytext[1], 0 };
  append_str(tmp);
}

<sQStr>\n {
  append_str(yytext);
  ++line_number;
}

<sQStr>\" {
  set_ret_str();
  yylval.str = strdup(out_buf);
  BEGIN(sValue);
  return VALUE;
}

<sValue>[^{"[:space:]][^{[:space:]]+ {
  /* unquoted string */
  yylval.str = strdup(yytext);
  return VALUE;
}

<sValue>\{ {
  BEGIN(INITIAL);
  return LEFTB;
}

<sValue>\n {
  ++line_number;
  BEGIN(INITIAL);
}

%%

/* code */

