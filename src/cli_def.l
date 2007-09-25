%{
#include "cli_val.h"
#include "cli_parse.h"
FILE *yy_cli_def_in;
static void make_def_value(vtw_type_e type);
static int cli_last_nl_returned=0;
#define STR_DELTA 2
%}
%x str
%option noyywrap
%option nounput
%option never-interactive

/*
 * Regular expressions of IP and MAC addresses, URLs, etc.
 */

/*
 * IPv4 address representation.
 */
RE_IPV4_BYTE	25[0-5]|2[0-4][0-9]|[01][0-9][0-9]|([0-9]{1,2})
RE_IPV4		{RE_IPV4_BYTE}(\.{RE_IPV4_BYTE}){3}
RE_IPV4_PREFIXLEN (3[012]|[12][0-9]|[0-9]) 
RE_IPV4NET	{RE_IPV4}"/"{RE_IPV4_PREFIXLEN}

/*
 * IPv6 address representation in Augmented Backus-Naur Form (ABNF)
 * as defined in RFC-2234. 
 * IPv6 address representation taken from RFC-3986:
 *
 *  IPv6address   =                            6( h16 ":" ) ls32
 *                /                       "::" 5( h16 ":" ) ls32
 *                / [               h16 ] "::" 4( h16 ":" ) ls32
 *                / [ *1( h16 ":" ) h16 ] "::" 3( h16 ":" ) ls32
 *                / [ *2( h16 ":" ) h16 ] "::" 2( h16 ":" ) ls32
 *                / [ *3( h16 ":" ) h16 ] "::"    h16 ":"   ls32
 *                / [ *4( h16 ":" ) h16 ] "::"              ls32
 *                / [ *5( h16 ":" ) h16 ] "::"              h16
 *                / [ *6( h16 ":" ) h16 ] "::"
 *
 *  h16           = 1*4HEXDIG
 *  ls32          = ( h16 ":" h16 ) / IPv4address
 *  IPv4address   = dec-octet "." dec-octet "." dec-octet "." dec-octet
 *  dec-octet     = DIGIT                  ; 0-9
 *                 / %x31-39 DIGIT         ; 10-99
 *                 / "1" 2DIGIT            ; 100-199
 *                 / "2" %x30-34 DIGIT     ; 200-249
 *                 / "25" %x30-35          ; 250-255
 */

RE_H16		[a-fA-F0-9]{1,4}
RE_H16_COLON	{RE_H16}":"
RE_LS32		(({RE_H16}":"{RE_H16})|{RE_IPV4})
RE_IPV6_P1	{RE_H16_COLON}{6}{RE_LS32}
RE_IPV6_P2	"::"{RE_H16_COLON}{5}{RE_LS32}
RE_IPV6_P3	({RE_H16})?"::"{RE_H16_COLON}{4}{RE_LS32}
RE_IPV6_P4	({RE_H16_COLON}{0,1}{RE_H16})?"::"{RE_H16_COLON}{3}{RE_LS32}
RE_IPV6_P5	({RE_H16_COLON}{0,2}{RE_H16})?"::"{RE_H16_COLON}{2}{RE_LS32}
RE_IPV6_P6	({RE_H16_COLON}{0,3}{RE_H16})?"::"{RE_H16_COLON}{1}{RE_LS32}
RE_IPV6_P7	({RE_H16_COLON}{0,4}{RE_H16})?"::"{RE_LS32}
RE_IPV6_P8	({RE_H16_COLON}{0,5}{RE_H16})?"::"{RE_H16}
RE_IPV6_P9	({RE_H16_COLON}{0,6}{RE_H16})?"::"
RE_IPV6		{RE_IPV6_P1}|{RE_IPV6_P2}|{RE_IPV6_P3}|{RE_IPV6_P4}|{RE_IPV6_P5}|{RE_IPV6_P6}|{RE_IPV6_P7}|{RE_IPV6_P8}|{RE_IPV6_P9}
RE_IPV6_PREFIXLEN 12[0-8]|1[01][0-9]|[0-9][0-9]?
RE_IPV6NET	{RE_IPV6}"/"{RE_IPV6_PREFIXLEN}

/*
 * Ethernet MAC address representation.
 */
RE_MACADDR	[a-fA-F0-9]{1,2}(:[a-fA-F0-9]{1,2}){5}

/*
 * URL-related regular expressions.
 *
 * Implementation is based on the BNF-like specification from:
 * - RFC-1738: HTTP, FTP, FILE
 * - RFC-3617: TFTP
 * - RFC-3986: update of RFC-1738
 */
RE_URL		{RE_URL_FILE}|{RE_URL_FTP}|{RE_URL_HTTP}|{RE_URL_TFTP}

/*
 * URL schemeparts for IP based protocols.
 * Representation taken from RFC-1738, and some of it is updated by RFC-3986.
 *
 * login          = [ user [ ":" password ] "@" ] hostport
 * hostport       = host [ ":" port ]
 * host           = hostname | hostnumber
 * hostname       = *[ domainlabel "." ] toplabel
 * domainlabel    = alphadigit | alphadigit *[ alphadigit | "-" ] alphadigit
 * toplabel       = alpha | alpha *[ alphadigit | "-" ] alphadigit
 * alphadigit     = alpha | digit
 * hostnumber     = digits "." digits "." digits "." digits
 * port           = digits
 * user           = *[ uchar | ";" | "?" | "&" | "=" ]
 * password       = *[ uchar | ";" | "?" | "&" | "=" ]
 */
RE_URL_LOGIN	({RE_URL_USER}(":"{RE_URL_PASSWORD})?"@")?{RE_URL_HOSTPORT}
RE_URL_HOSTPORT	{RE_URL_HOST}(":"{RE_URL_PORT})?
RE_URL_HOST	{RE_URL_HOSTNAME}|{RE_IPV4}|{RE_URL_IP_LITERAL}
RE_URL_IP_LITERAL "["({RE_IPV6}|{RE_URL_IPV_FUTURE})"]"
RE_URL_IPV_FUTURE "v"({RE_URL_HEXDIG})+"."({RE_URL_UNRESERVED}|{RE_URL_SUBDELIMS}|":")+
RE_URL_HOSTNAME	({RE_URL_DOMAINLABEL}".")*{RE_URL_TOPLABEL}
RE_URL_DOMAINLABEL {RE_URL_ALPHADIGIT}|{RE_URL_ALPHADIGIT}({RE_URL_ALPHADIGIT}|"-")*{RE_URL_ALPHADIGIT}
RE_URL_TOPLABEL	{RE_URL_ALPHA}|{RE_URL_ALPHA}({RE_URL_ALPHADIGIT}|"-")*{RE_URL_ALPHADIGIT}
RE_URL_ALPHADIGIT {RE_URL_ALPHA}|{RE_URL_DIGIT}
RE_URL_HOSTNUMBER {RE_URL_DIGITS}"."{RE_URL_DIGITS}"."{RE_URL_DIGITS}"."{RE_URL_DIGITS}
RE_URL_PORT	{RE_URL_DIGITS}
RE_URL_USER	({RE_URL_UCHAR}|";"|"?"|"&"|"=")*
RE_URL_PASSWORD	({RE_URL_UCHAR}|";"|"?"|"&"|"=")*

/*
 * FILE URL regular expression.
 * Representation taken from RFC-1738.
 *
 * fileurl        = "file://" [ host | "localhost" ] "/" fpath
 */
RE_URL_FILE	"file://"({RE_URL_HOST}|"localhost")?"/"{RE_URL_FPATH}

/*
 * FTP URL regular expression.
 * Representation taken from RFC-1738.
 *
 * ftpurl         = "ftp://" login [ "/" fpath [ ";type=" ftptype ] ]
 * fpath          = fsegment *[ "/" fsegment ]
 * fsegment       = *[ uchar | "?" | ":" | "@" | "&" | "=" ]
 * ftptype        = "A" | "I" | "D" | "a" | "i" | "d"
 */
RE_URL_FTP	"ftp://"{RE_URL_LOGIN}("/"{RE_URL_FPATH}(";type="{RE_URL_FTPTYPE})?)?
RE_URL_FPATH	{RE_URL_FSEGMENT}("/"{RE_URL_FSEGMENT})*
RE_URL_FSEGMENT	({RE_URL_UCHAR}|"?"|":"|"@"|"&"|"=")*
RE_URL_FTPTYPE	"A"|"I"|"D"|"a"|"i"|"d"

/*
 * HTTP URL regular expression.
 * Representation taken from RFC-1738.
 *
 * httpurl        = "http://" hostport [ "/" hpath [ "?" search ] ]
 * hpath          = hsegment *[ "/" hsegment ]
 * hsegment       = *[ uchar | ";" | ":" | "@" | "&" | "=" ]
 * search         = *[ uchar | ";" | ":" | "@" | "&" | "=" ]
 */
RE_URL_HTTP	"http://"{RE_URL_HOSTPORT}("/"{RE_URL_HPATH}("?"{RE_URL_SEARCH})?)?
RE_URL_HPATH	{RE_URL_HSEGMENT}("/"{RE_URL_HSEGMENT})*
RE_URL_HSEGMENT	({RE_URL_UCHAR}|";"|":"|"@"|"&"|"=")*
RE_URL_SEARCH	({RE_URL_UCHAR}|";"|":"|"@"|"&"|"=")*

/*
 * TFTP URL regular expression.
 * Representation taken from RFC-3617.
 *
 * tftpURI         = "tftp://" host "/" file [ mode ]
 * mode            = ";"  "mode=" ( "netascii" / "octet" )
 * file            = *( unreserved / escaped )
 * host            = <as specified by RFC 2732 [3]>
 * unreserved      = <as specified in RFC 2396 [4]>
 * escaped         = <as specified in RFC 2396>
 */
RE_URL_TFTP	"tftp://"{RE_URL_HOST}"/"{RE_URL_TFTP_FILE}({RE_URL_TFTP_MODE})?
RE_URL_TFTP_MODE ";""mode="("netascii"|"octet")
RE_URL_TFTP_FILE ({RE_URL_UNRESERVED}|{RE_URL_ESCAPE})*

/*
 * URL-related miscellaneous definitions.
 * Representation taken from RFC-1738 and from RFC-3986.
 *
 * lowalpha       = "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" |
 *                  "i" | "j" | "k" | "l" | "m" | "n" | "o" | "p" |
 *                  "q" | "r" | "s" | "t" | "u" | "v" | "w" | "x" |
 *                  "y" | "z"
 * hialpha        = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" |
 *                  "J" | "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" |
 *                  "S" | "T" | "U" | "V" | "W" | "X" | "Y" | "Z"
 * alpha          = lowalpha | hialpha
 * digit          = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" |
 *                  "8" | "9"
 * safe           = "$" | "-" | "_" | "." | "+"
 * extra          = "!" | "*" | "'" | "(" | ")" | ","
 * national       = "{" | "}" | "|" | "\" | "^" | "~" | "[" | "]" | "`"
 * punctuation    = "<" | ">" | "#" | "%" | <">
 *
 *
 * reserved       = ";" | "/" | "?" | ":" | "@" | "&" | "="
 * hex            = digit | "A" | "B" | "C" | "D" | "E" | "F" |
 *                  "a" | "b" | "c" | "d" | "e" | "f"
 * escape         = "%" hex hex
 *
 * unreserved     = alpha | digit | safe | extra
 * uchar          = unreserved | escape
 * xchar          = unreserved | reserved | escape
 * digits         = 1*digit
 *
 * sub-delims     = "!" / "$" / "&" / "'" / "(" / ")"
 *                / "*" / "+" / "," / ";" / "="
 */
RE_URL_LOWALPHA	[a-z]
RE_URL_HIALPHA	[A-Z]
RE_URL_ALPHA	{RE_URL_LOWALPHA}|{RE_URL_HIALPHA}
RE_URL_DIGIT	[0-9]
RE_URL_SAFE	"$"|"-"|"_"|"."|"+"
RE_URL_EXTRA	"!"|"*"|"'"|"("|")"|","
RE_URL_NATIONAL	"{"|"}"|"|"|"\"|"^"|"~"|"["|"]"|"`"
RE_URL_PUNCTUATION "<"|">"|"#"|"%"|<">
RE_URL_RESERVED	";"|"/"|"?"|":"|"@"|"&"|"="
RE_URL_HEXDIG	{RE_URL_DIGIT}|[A-F]|[a-f]
RE_URL_ESCAPE	"%"{RE_URL_HEXDIG}{RE_URL_HEXDIG}
RE_URL_UNRESERVED {RE_URL_ALPHA}|{RE_URL_DIGIT}|{RE_URL_SAFE}|{RE_URL_EXTRA}
RE_URL_UCHAR	{RE_URL_UNRESERVED}|{RE_URL_ESCAPE}
RE_URL_XCHAR	{RE_URL_UNRESERVED}|{RE_URL_RESERVED}|{RE_URL_ESCAPE}
RE_URL_DIGITS	{RE_URL_DIGIT}{1,}
RE_URL_SUBDELIMS "!"|"$"|"&"|"'"|"("|")"|"*"|"+"|","|";"|"="


%%
  char *string_buf=NULL;
  char *string_buf_ptr=NULL;
  int   string_len=0, string_buf_len=0;
  char  str_delim=0;

#[^\n]*(\\[ \t]*\n[^\n]*)*\n  { /* comment */ }
\n        {yy_cli_def_lineno++;return EOL;}
<<EOF>>   { if(!cli_last_nl_returned) {cli_last_nl_returned=1;return EOL;} yyterminate(); }
[\`\"]  {
	  BEGIN(str);
          str_delim = yy_cli_def_text[0];
          string_buf_ptr = string_buf = my_malloc(STR_DELTA + 1, ""); 
          string_len = STR_DELTA;
          string_buf_len = STR_DELTA;
	}
<str>[\"\`] {
            /* maybe a closing quote - all done */
            if(str_delim == yy_cli_def_text[0]) {
              BEGIN(INITIAL);
              *string_buf_ptr = 0;
              /* return string constant token type and
               * value to parser
              */
              yy_cli_parse_lval.strp = string_buf;
              return str_delim == '"'?STRING:EX_STRING;
            }else{
               *string_buf_ptr++ = yy_cli_def_text[0];
               goto string_too_long;
            } 
        }

<str>\n {
        /* error - unterminated string constant */
        yy_cli_def_lineno++;
	}

<str>\\n  {*string_buf_ptr++ = '\n';goto string_too_long;}
<str>\\t  {*string_buf_ptr++ = '\t';goto string_too_long;}
<str>\\r  {*string_buf_ptr++ = '\r';goto string_too_long;}
<str>\\b  {*string_buf_ptr++ = '\b';goto string_too_long;}
<str>\\f  {*string_buf_ptr++ = '\f';goto string_too_long;}
<str>\\\n { yy_cli_def_lineno++; /* continuation => ignore */ }

<str>\\. { 
    *string_buf_ptr++ = yy_cli_def_text[1];
string_too_long:
    *string_buf_ptr = 0;
    /* printf("Cur string |%s|\n", string_buf); */
    if (!--string_len) {
      string_buf = my_realloc(string_buf, 
        string_buf_len + STR_DELTA + 1, 
        "cli_def STRING");
      string_buf_ptr = string_buf + string_buf_len;
      string_buf_len += STR_DELTA;
      string_len = STR_DELTA;
    }
  }
<str>[^\\\n\"\`]+        {
        char *yptr = yy_cli_def_text;
        
        while ( *yptr ){
          *string_buf_ptr++ = *yptr++;
          if (!--string_len) {
             string_buf = my_realloc(string_buf, 
                string_buf_len + STR_DELTA + 1, 
                "cli_def STRING");
             string_buf_ptr = string_buf + string_buf_len;
             string_buf_len += STR_DELTA;
             string_len = STR_DELTA;
          }
        }
        *string_buf_ptr = 0;
    }

default: return DEFAULT;
tag:	return TAG;
type:	return TYPE;
help:	return HELP;
syntax: return SYNTAX;
commit: return COMMIT;
check:  yy_cli_parse_lval.action = syntax_act; return ACTION;
delete:  yy_cli_parse_lval.action = delete_act; return ACTION;
update:  yy_cli_parse_lval.action = update_act; return ACTION;
activate:  yy_cli_parse_lval.action = activate_act; return ACTION;
create:  yy_cli_parse_lval.action = create_act; return ACTION;
begin:   yy_cli_parse_lval.action = begin_act; return ACTION;
end:     yy_cli_parse_lval.action = end_act; return ACTION;
multi:  return MULTI;

::	{
        make_def_value(IPV6_TYPE);
	return VALUE;
	}
txt	{
	yy_cli_parse_lval.type = TEXT_TYPE;
	return TYPE_DEF;
	}

pattern   return PATTERN;

exec   return EXEC;

,         return COMMA;
\|\|      return OR;
\&\&     return AND;
\=       return ASSIGN;
\=\=     {yy_cli_parse_lval.cond = EQ_COND; return COND;}
\!\=     {yy_cli_parse_lval.cond = NE_COND; return COND;}
\<       {yy_cli_parse_lval.cond = LT_COND; return COND;}
\>       {yy_cli_parse_lval.cond = GT_COND; return COND;}
\<\=      {yy_cli_parse_lval.cond = LE_COND; return COND;}
\>\=      {yy_cli_parse_lval.cond = GE_COND; return COND;}
in        {yy_cli_parse_lval.cond = IN_COND; return COND;}
\!      { return NOT; }
\$\([^)]+\) {
        yy_cli_parse_lval.strp = my_strdup(yy_cli_def_text, "TEXT");
        return VAR;
	}
true	{
        make_def_value(BOOL_TYPE);
	return VALUE;
	}

false	{
        make_def_value(BOOL_TYPE);
	return VALUE;
	}

[0-9]+	{
        make_def_value(INT_TYPE);
	return VALUE;
	}

{RE_IPV4}	{
        make_def_value(IPV4_TYPE);
	return VALUE;
	}

{RE_IPV4NET} {
        make_def_value(IPV4NET_TYPE);
	return VALUE;
	}

{RE_IPV6}	{
        make_def_value(IPV6_TYPE);
	return VALUE;
	}

{RE_IPV6NET} {
        make_def_value(IPV6NET_TYPE);
	return VALUE;
	}

{RE_MACADDR}	{
        make_def_value(MACADDR_TYPE);
	return VALUE;
	}
u32	{
	yy_cli_parse_lval.type = INT_TYPE;
	return TYPE_DEF;
	}

ipv4	{
	yy_cli_parse_lval.type = IPV4_TYPE;
	return TYPE_DEF;
	}

ipv4net	{
	yy_cli_parse_lval.type = IPV4NET_TYPE;
	return TYPE_DEF;
	}
ipv6	{
	yy_cli_parse_lval.type = IPV6_TYPE;
	return TYPE_DEF;
	}

ipv6net	{
	yy_cli_parse_lval.type = IPV6NET_TYPE;
	return TYPE_DEF;
	}
bool	{
	yy_cli_parse_lval.type = BOOL_TYPE;
	return TYPE_DEF;
	}
macaddr	{
        yy_cli_parse_lval.type = MACADDR_TYPE;
	return TYPE_DEF;
	}
\(       return LP;
\)       return RP;
;       return SEMI;

\\\n    { yy_cli_def_lineno++; /*whitespace -- continuation => ignore */ }

[ \t]+	/* whitespace */

.	{
	/* everything else is a syntax error */
	return SYNTAX_ERROR;
	}

%%

static void make_def_value(vtw_type_e type)
{
   memset(&yy_cli_parse_lval.val.val, 0, sizeof(yy_cli_parse_lval.val.val));
   yy_cli_parse_lval.val.free_me = TRUE;
   yy_cli_parse_lval.val.val = my_strdup(yy_cli_def_text, "cli_parse.l");
   yy_cli_parse_lval.val.val_type = type;
}
