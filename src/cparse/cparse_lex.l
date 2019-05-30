/* definitions */
%x sComment
%x sID
%x sValue
%x sQStr
%option noyywrap

ID ([-[:alnum:]_]+)
SPACE ([[:space:]]{-}[\n])

%{
/* get rid of compiler warning */
#define YY_NO_INPUT 1

#include <string.h>
#include "cparse_def.h"
#include "cparse.h"

#define STR_BUF_INC 4096

static int node_deactivated = 0;
static char *str_buf = NULL;
static char *out_buf = NULL;
static char *str_ptr = NULL;
static size_t str_buf_len = 0;

static void
prepare_buffers(size_t add_len)
{
  size_t slen = str_ptr - str_buf;
  if (str_buf && (slen + add_len) < str_buf_len) {
    // nothing to do
    return;
  }

  str_buf_len += STR_BUF_INC;
  str_buf = realloc(str_buf, str_buf_len);
  out_buf = realloc(out_buf, str_buf_len);
  if (!str_buf || !out_buf) {
    printf("realloc failed\n");
    exit(1);
  }
  str_ptr = str_buf + slen;
}

static void
append_str(char *text)
{
  size_t tlen = strlen(text);
  prepare_buffers(tlen);
  strcpy(str_ptr, text);
  str_ptr += tlen;
}

static void
set_ret_str()
{
  prepare_buffers(0);
  *str_ptr = 0;
  strcpy(out_buf, str_buf);
  str_ptr = str_buf;
}

static void
free_str()
{
  free(str_buf);
  free(out_buf);
  node_deactivated = 0;
  str_buf = NULL;
  out_buf = NULL;
  str_ptr = NULL;
  str_buf_len = 0;
}

%}

%%

<INITIAL>"/*" {
  BEGIN(sComment);
}

<sComment>[^*\n]* {
  append_str(cparse_text);
}

<sComment>\*[^/] {
  append_str(cparse_text);
}

<sComment>\n {
  append_str(cparse_text);
  ++cparse_lineno;
}

<sComment>"*/" {
  char *tmp;
  size_t tlen;
  set_ret_str();

  /* need to strip out leading or trailing space */
  tmp = out_buf;
  tlen = strlen(tmp);
  if (tlen > 0 && tmp[tlen - 1] == ' ') {
    tmp[tlen - 1] = 0;
    --tlen;
  }
  if (tlen > 0 && tmp[0] == ' ') {
    ++tmp;
  }
  cparse_lval.str = strdup(tmp);
  free_str();
  BEGIN(INITIAL);
  return COMMENT;
}

"//".* { /* Do nothing. */
}

<INITIAL>! {
  node_deactivated = 1;
}

<INITIAL>{SPACE}+ {
}

<INITIAL>\n {
  ++cparse_lineno;
}

<INITIAL>\} {
  node_deactivated = 0;
  return RIGHTB;
}

<INITIAL>{ID} {
  cparse_lval.str = strdup(cparse_text);
  cparse_lval.deactivated = node_deactivated;
  node_deactivated = 0;
  BEGIN(sID);
  return NODE;
}

<sID>:?{SPACE}+[^{\n] {
  unput(cparse_text[cparse_leng - 1]);
  BEGIN(sValue);
}

<sID>{SPACE}+ {
}

<sID>\{ {
  BEGIN(INITIAL);
  return LEFTB;
}

<sID>\n {
  ++cparse_lineno;
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
  append_str(cparse_text);
}

<sQStr>\\\"\n {
  append_str("\\");
  set_ret_str();
  cparse_lval.str = strdup(out_buf);
  free_str();
  ++cparse_lineno;
  BEGIN(INITIAL);
  return VALUE;
}

<sQStr>\\. {
  /* this will consume the \" sequence */
  append_str(cparse_text);
}

<sQStr>\n {
  append_str(cparse_text);
  ++cparse_lineno;
}

<sQStr>\" {
  set_ret_str();
  cparse_lval.str = strdup(out_buf);
  free_str();
  BEGIN(sValue);
  return VALUE;
}

<sValue>[^{"[:space:]][^{[:space:]]* {
  /* unquoted string */
  cparse_lval.str = strdup(cparse_text);
  return VALUE;
}

<sValue>\{ {
  BEGIN(INITIAL);
  return LEFTB;
}

<sValue>\n {
  ++cparse_lineno;
  BEGIN(INITIAL);
}

<*>. {
  return SYNTAX_ERROR;
}

%%

/* code */

