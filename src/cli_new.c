#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include "cli_val.h"
#include "cli_parse.h"
#include <regex.h>
#include <errno.h>
#include <time.h>
#include <utime.h>

#include "cli_objects.h"
#include "cli_val_engine.h"

#include "cstore/cstore-c.h"

/* Defines: */

#define EXE_STRING_DELTA 512
#define PATH_DELTA 1000
#define ENDS_ALLOC 20
/* mcd vs. ccd location
   change when m_root changed */
#define PATH_CM_LOCATION 25

#define VAR_REF_MARKER "$VAR("
#define VAR_REF_MARKER_LEN 5
#define VAR_REF_SELF_MARKER "$VAR(@)"
#define VAR_REF_SELF_MARKER_LEN 7

/* Global vars: */
vtw_path m_path, t_path;
void *var_ref_handle = NULL;

/* Local vars: */
static vtw_node *vtw_free_nodes; /* linked via left */
static int cond1[TOP_COND] ={5, 0,-1,-1, 0, 1, 0, 0};
static int cond2[TOP_COND] ={5, 0, 1,-1,-1, 1, 1, 0};
static char const *cond_formats[DOMAIN_TYPE] = 
  {
    0, 
    "%u",                         /* INT_TYPE     */
    "%u.%u.%u.%u",                /* IPV4_TYPE    */ 
    "%u.%u.%u.%u/%u",             /* IPV4NET_TYPE */ 
    "%x:%x:%x:%x:%x:%x:%x:%x",    /* IPV6NET      */ 
    "%x:%x:%x:%x:%x:%x:%x:%x/%u", /* IPV6NET_TYPE */ 
    "%x:%x:%x:%x:%x:%x"           /* MACADDR_TYPE */
  };

static int cond_format_lens[DOMAIN_TYPE] = 
  {
     0, 
     1, /* INT_TYPE     */
     4, /* IPV4_TYPE    */ 
     5, /* IPV4NET_TYPE */ 
    16, /* IPV6_TYPE    */ 
    17, /* IPV6NET_TYPE */ 
     6  /* MACADDR_TYPE */
  };

static int cli_val_len;
static char *cli_val_ptr;

static char *exe_string;
static int exe_string_len;
static int node_cnt;
static int free_node_cnt;
static boolean in_validate_val;
static valstruct validate_value_val;  /* value being validated 
					 to be used as $(@) */

/* Local function declarations: */

static void touch(void);
static int check_comp(vtw_node *cur);
static boolean check_syn_func(vtw_node *cur,const char *prepend_msg,boolean format, const char* func,int line);
#define check_syn(cur,msg_buf,format) check_syn_func((cur),(msg_buf),(format),__FUNCTION__,__LINE__)
static void copy_path(vtw_path *to, vtw_path *from);
static int eval_va(valstruct *res, vtw_node *node);
static int expand_string(char *p);
static void free_node(vtw_node *node);
static void free_node_tree(vtw_node *node);
static void free_reuse_list(void);
void free_path(vtw_path *path);
static void free_string(char *str);
static vtw_node * get_node(void);
static void scan_ipv6(char *val, unsigned int *parts);
static int set_reference_environment(const char* var_reference,
				     clind_path_ref *n_cfg_path,
				     clind_path_ref *n_tmpl_path,
				     clind_path_ref *n_cmd_path,
				     int active);

/*************************************************
     GLOBAL FUNCTIONS
***************************************************/

const char *cli_operation_name = NULL;

void bye(const char *msg, ...)
{
  va_list ap;
            
  OUTPUT_USER("%s failed\n",
              (cli_operation_name) ? cli_operation_name : "Operation");

  va_start(ap, msg);
  vprintf(msg, ap);
  printf("\n");
  va_end(ap);

  exit(1);
}

int mkdir_p(const char *path)
{
  if (mkdir(path, 0777) == 0)
    return 0;

  if (errno != ENOENT)
    return -1;

  char *tmp = strdup(path);
  if (tmp == NULL) {
    errno = ENOMEM;
    return -1;
  }

  char *slash = strrchr(tmp, '/');
  if (slash == NULL)
    return -1;
  *slash = '\0';

  /* recurse to make missing piece of path */
  int ret = mkdir_p(tmp);
  if (ret == 0)
    ret = mkdir(path, 0777);

  free(tmp);
  return ret;
}

static void
touch_dir(const char *dp)
{
  struct stat    statbuf;

  if (lstat(dp, &statbuf) < 0) {
    if (errno != ENOENT) 
      bye("can't access directory: %s (%s)", dp, strerror(errno));

    if (mkdir_p(dp) < 0)
      bye("can't make directory: %s (%s)", dp, strerror(errno));
  } else {
    if(!S_ISDIR(statbuf.st_mode))
      bye("directory %s expected, found file", dp);
    utime(dp, NULL);
  }
}

/*****************************************************
  add_val:
    verify that the types are the same;
    if first valstruct is single value, convert it 
    into multivalue;
    add the value of second to the list of first;
*****************************************************/
void add_val(valstruct *first, valstruct *second)
{
  assert (first->free_me && second->free_me);
  assert(second->cnt == 0);
  if (first->cnt%MULTI_ALLOC == 0) {
    /* convert into multivalue */
    first->vals = my_realloc(first->vals, (first->cnt + MULTI_ALLOC) *
			     sizeof(char *), "add_value 1");
    first->val_types = my_realloc(first->val_types,(first->cnt + MULTI_ALLOC) * 
				  sizeof(vtw_type_e), "add_value 2");
    if (first->cnt == 0) { /* single value - convert */
      first->vals[0] = first->val;
      first->val_types[0] = first->val_type;
      first->cnt = 1;
      first->val = NULL;
    }
  }
  second->free_me = FALSE; /* we took its string */
  first->vals[first->cnt] = second->val;
  first->val_types[first->cnt] = second->val_type;
  ++first->cnt;
}
/*****************************************************
  append - append node to the tail of list
*****************************************************/
void append(vtw_list *l, vtw_node *n, int aux)
{
  vtw_node *lnode;
  lnode = make_node(LIST_OP, n, NULL);
  lnode->vtw_node_aux = aux;
  if(l->vtw_list_tail) {
    assert(l->vtw_list_tail->vtw_node_right == NULL);
    l->vtw_list_tail->vtw_node_right = lnode;
  } else {
    assert(l->vtw_list_head == NULL);
    l->vtw_list_head = lnode;
  }
  l->vtw_list_tail = lnode;
}

/* this recursive function walks the specified "vtw_node" tree representing
 * "syntax" actions and looks for the first "in" action on a "self ref"
 * specified as follows in template:
 *   $VAR(@) in "val1", "val2", ...
 *
 * if found, the corresponding valstruct is returned. this is used by the
 * completion mechanism to get such "allowed" values specified by "syntax".
 */
const valstruct *
get_syntax_self_in_valstruct(const vtw_node *vnode)
{
  const valstruct *ret = NULL;
  if (!vnode) {
    return NULL;
  }
  if (vnode->vtw_node_oper == COND_OP && vnode->vtw_node_aux == IN_COND
      && vnode->vtw_node_left && vnode->vtw_node_right) {
    vtw_node *ln = vnode->vtw_node_left;
    vtw_node *rn = vnode->vtw_node_right;
    if (ln->vtw_node_oper == VAR_OP && ln->vtw_node_string
        && strncmp(VAR_REF_SELF_MARKER, ln->vtw_node_string,
                   VAR_REF_SELF_MARKER_LEN) == 0
        && rn->vtw_node_oper == VAL_OP) {
      // found a matching syntax action. return valstruct.
      return &(rn->vtw_node_val);
    }
  }
  // this node does not match. walk down.
  ret = get_syntax_self_in_valstruct(vnode->vtw_node_left);
  if (!ret) {
    ret = get_syntax_self_in_valstruct(vnode->vtw_node_right);
  }
  return ret;
}

/* execute specified command and return output in specified buffer as
 * a null-terminated string. return number of characters in the output
 * or -1 if failed.
 *
 * NOTE: NO attempt is made to ensure the security of the specified command.
 *       in other words, *DO NOT* use a user-supplied string as the command
 *       for this function.
 */
int
get_shell_command_output(const char *cmd, char *buf, unsigned int buf_size)
{
  int ret = -1;
  FILE *cmd_in = NULL;
  size_t cnt = 0;

  if (!buf || !buf_size) {
    return -1;
  }
  if (!(cmd_in = popen(cmd, "r"))) {
    return -1;
  }
  cnt = fread(buf, 1, buf_size - 1, cmd_in);
  if (cnt == (buf_size - 1) || feof(cmd_in)) {
    /* buffer full or got the whole output. null terminate */
    buf[cnt] = 0;
    ret = cnt;
  }
  if (pclose(cmd_in) == -1) {
    ret = -1;
  }
  return ret;
}

#define LOCK_FILE "/opt/vyatta/config/.lock"
#define COMMIT_CMD "/opt/vyatta/sbin/my_commit"

void
release_config_lock()
{
  unlink(LOCK_FILE);
  /* error ignored */
}

/* try to clean up orphaned lock file. return -1 if failed */
static int
try_lock_cleanup()
{
  char buf[128];
  char proc[128];
  FILE *f = NULL;
  int ret = -1;
  struct stat statb;

  do {
    /* get the proc entry */ 
    if ((f = fopen(LOCK_FILE, "r")) == NULL) {
      break;
    }
    if (fgets(proc, 128, f) == NULL) {
      break;
    }
    /* read the proc entry */
    if (stat(proc, &statb) == -1) {
      if (errno == ENOENT) {
        /* proc entry doesn't exist. can clean up the lock now */
        ret = 0;
        break;
      }
    }
    fclose(f);
    if ((f = fopen(proc, "r")) == NULL) {
      /* can't open proc entry. assume we can't clean up */
      break;
    }
    if (fgets(buf, 128, f) == NULL) {
      /* can't read proc entry. assume we can't clean up */
      break;
    }
    /* check if the process is commit */
    if (strcmp(buf, COMMIT_CMD) == 0) {
      /* it is commit. can't clean up */
      break;
    }
    /* can clean up the lock */
    ret = 0;
  } while (0);
  if (f) {
    fclose(f);
  }
  if (ret == 0) {
    unlink(LOCK_FILE);
    if (stat(LOCK_FILE, &statb) != -1 || errno != ENOENT) {
      /* proc entry still exists. cleanup failed */
      ret = -1;
    }
  }
  return ret;
}

static int
create_lock_file(int try_cleanup)
{
  int fd = -1;
  int i = 0;
  struct timespec req;

#define LOCK_WAIT_TIME 2
#define LOCK_NUM_RETRIES 5
  req.tv_sec = LOCK_WAIT_TIME;
  req.tv_nsec = 0;
  fd = open(LOCK_FILE, O_WRONLY | O_CREAT | O_EXCL, 0660);
  if (fd == -1) {
    for (i = 0; i < LOCK_NUM_RETRIES; i++) {
      nanosleep(&req, NULL);
      fd = open(LOCK_FILE, O_WRONLY | O_CREAT | O_EXCL, 0660);
      if (fd >= 0) {
        break;
      }
    }
  }
  if (fd == -1 && try_cleanup) {
    if (try_lock_cleanup() != -1) {
      /* cleanup succeeded */
      fd = create_lock_file(0);
    }
  }
  return fd; 
}

int
get_config_lock()
{
  int fd = -1;
  FILE *lfile = NULL;
  int ret = -1;

  do {
    /* create lock file */
    fd = create_lock_file(1);
    if (fd == -1) {
      break;
    }

    /* write pid into lock file */
    if ((lfile = fdopen(fd, "w")) == NULL) {
      break;
    }
    if (fprintf(lfile, "/proc/%u/cmdline", getpid()) < 0) {
      break;
    }
    /* fclose also closes fd */
    if (fclose(lfile) != 0) {
      break;
    }
    /* clean up on exit */
    if (atexit(release_config_lock) != 0) {
      break;
    }
    ret = 0;
  } while (0);
 
  if (ret == -1) {
    if (lfile) {
      fclose(lfile);
    } else if (fd != -1) {
      close(fd);
    }
    //code below will cause a concurrent commit to release lock on failure, and
    //therefore, allow the concurrent to proceed the second time.
    //lock can be forced via commit flag instead.
    //    release_config_lock();
  }
  return ret;
}

/* returns FALSE if execution returns non-null,
   returns TRUE if every excution returns NULL
*/
boolean
execute_list(vtw_node *cur, const vtw_def *def, const char *prepend_msg)
{
  boolean ret;
  int status;
  set_in_exec(TRUE);
  status = char2val(def, get_at_string(), &validate_value_val);  
  if (status) return FALSE;

  // XXX emulate original impl for "error" location
  ret = check_syn(cur, prepend_msg,
                  (getenv("VYATTA_OUTPUT_ERROR_LOCATION") != NULL));
  free_val(&validate_value_val);
  set_in_exec(FALSE);
  return ret;
}

  
/*****************************************************
  make_node - create a node with oper, left, and right
*****************************************************/
vtw_node * make_node(vtw_oper_e oper, vtw_node *left, 
		     vtw_node *right)
{
  vtw_node *ret ;
  ret = get_node();
  ret->vtw_node_oper = oper;
  ret->vtw_node_left = left;
  ret->vtw_node_right = right;
  ret->vtw_node_string = NULL;
  ret->vtw_node_aux = 0;
  return ret;
}
vtw_node *make_str_node0(char *str, vtw_oper_e op)
{
  vtw_node *ret;
  ret = make_node(op, NULL, NULL);
  ret->vtw_node_string = str;
  ret->vtw_node_type = TEXT_TYPE;
  return ret;
}
/*****************************************************
  make_str_node - create a VAL_OP node with str
*****************************************************/
vtw_node *make_str_node(char *str)
{
  return make_str_node0(str, VAL_OP);
}
/*****************************************************
  make_var_node - create a VAR_OP node with str
*****************************************************/
vtw_node *make_var_node(char *str)
{
  return make_str_node0(str, VAR_OP);
}
/*****************************************************
  make_val_node - create a VAl_OP node with str
*****************************************************/
vtw_node *make_val_node(valstruct *val)
{
  vtw_node *ret;
  assert(val->free_me);
  ret = make_node(VAL_OP, NULL, NULL);
  ret->vtw_node_val = *val;
  val->free_me = FALSE;
  return ret;
}
valstruct str2val(char *cp)
{
  valstruct ret;
  memset(&ret, 0, sizeof(ret));
  ret.val_type = TEXT_TYPE;
  ret.val = cp;
  ret.free_me = TRUE;
  return ret;
}
/****************************************************
       STATIC FUNCTIONS
****************************************************/
int char2val_notext(const vtw_def *def, int my_type, int my_type2,
                    char *value, valstruct **valp, char *buf);
int char2val_text(const vtw_def *def, char *value, valstruct **valp);

/**************************************************
  char2val:
    convert string into valstruct verifying the type
    according to def
****************************************************/
int char2val(const vtw_def *def, char *value, valstruct *valp)
{
  int my_type = def->def_type;
  int my_type2 = def->def_type2;

  memset(valp, 0, sizeof (*valp));

  /*
    normal:
    A) single text type
    B) single non-text type

    new:
    C) Text plus non-text (NOT SUPPORTED)!
    D) 2 non-text (double up first loop)
    //so perhaps split the below into two functions, text and non-text
   */

  if (my_type != TEXT_TYPE && 
      my_type != ERROR_TYPE) {
    //since this is the restricted type
    //we'll either do two calls to this 
    //or one call to this as text

    //currently fails to handle mixed text + non-text case...
    char buf1[2048];
    if (char2val_notext(def,my_type,my_type2,value,&valp,buf1) != 0) {
      OUTPUT_USER("%s", buf1);
      return -1; //only single definition
    }
    return 0;
  }
  else {
    return char2val_text(def,value,&valp);
  }
}

//non-text type processing block

int char2val_notext(const vtw_def *def, int my_type, int my_type2,
                    char *value, valstruct **valpp, char *err_buf)
{
  valstruct *valp = *valpp;
  int token;
  boolean first = TRUE;
  cli_val_len = strlen(value);
  cli_val_ptr = value;

  char type_buf[256];
  if (my_type2 != ERROR_TYPE) {
    sprintf(type_buf,"%s or %s",type_to_name(my_type),type_to_name(my_type2));
  }
  else {
    sprintf(type_buf,"%s",type_to_name(my_type));
  }

  while(1) {
    token = yy_cli_val_lex();

    if (token != VALUE) {
      if (first || token){
	
	if (def->def_type_help){
	  set_at_string(value);
	  (void)expand_string(def->def_type_help);
	  sprintf(err_buf, "%s\n", exe_string);
	} else {
	  printf("Wrong type of value in %s, need %s\n",
                 m_path.path_buf + m_path.print_offset, type_buf);

	  token = yy_cli_val_lex();

	  sprintf(err_buf, "\"%s\" is not a valid value of type \"%s\"\n",
		  value, type_buf);
	}
	return -1;
	
      }
      return 0;
    }
    if (my_type != get_cli_value_ptr()->val_type &&
	(my_type2 != ERROR_TYPE && my_type2 != get_cli_value_ptr()->val_type)) {
      if (def->def_type_help){
	set_at_string(value);
	(void)expand_string(def->def_type_help);
	sprintf(err_buf, "%s\n", exe_string);
      } else {
	printf("Wrong type of value in %s, need %s\n",
               m_path.path_buf + m_path.print_offset, type_buf);

	token = yy_cli_val_lex();

	sprintf(err_buf, "\"%s\" is not a valid value of type \"%s\"\n",
		value, type_buf);
      }
      my_free(get_cli_value_ptr()->val);
      if (first) {
	return -1;
      }
      return 0;
    }
    if (first) {
      *valp = *get_cli_value_ptr();
      get_cli_value_ptr()->free_me = FALSE;
      first = FALSE;
    } else {
      if (def->multi)
	add_val(valp, get_cli_value_ptr());
      else {
	printf("Unexpected multivalue in %s\n", m_path.path);
	free_val(get_cli_value_ptr());
      }
    }
    token = yy_cli_val_lex();
    if (!token) {
      return 0;
    }
    if (token != EOL) {

      token = yy_cli_val_lex();

      sprintf(err_buf, "\"%s\" is not a valid value\n", value);
      printf("Badly formed value in %s\n", m_path.path + m_path.print_offset);
      if (token == VALUE) {
	my_free(get_cli_value_ptr()->val);
      }
      return -1;
    }
  }
  return 0;
}

int char2val_text(const vtw_def *def, char *value, valstruct **valpp)
{
  valstruct *valp = *valpp;
  char *endp, *cp;
  int linecnt, cnt;

  //PROCESSING IF TYPE IS TEXT TYPE
  
  valp->val_type = TEXT_TYPE;
  valp->val_types = NULL;
  valp->free_me = TRUE;
  /* count lines */
  linecnt = 0;
  for (cp = value; *cp; ++cp)
    if (*cp == '\n')
      ++linecnt;
  if (cp != value && cp[-1] != '\n')
    ++linecnt;  /* last non empty non \n terminated string */
  if (linecnt == 0)  /* one empty non terminated string */
    linecnt = 1;
  if (linecnt == 1) {
    valp->val=my_strdup(value, "char2val 1");
    /*truncate '\n' etc */
    endp = strchr(valp->val, '\n');
    if (endp)
      *endp = 0;
  } else {
    valp->cnt = linecnt;
    cnt = (linecnt + MULTI_ALLOC - 1) / MULTI_ALLOC;
    cnt *= MULTI_ALLOC;
    valp->vals = my_malloc(cnt * sizeof(char *), "char2val 2");
    valp->val_types = my_malloc(cnt * sizeof(vtw_type_e), "char2val 3");
    int i;
    for (i=0;i<cnt;++i) {
      valp->val_types[i] = ERROR_TYPE;
    }
    for(cp = value, cnt = 0; cnt < linecnt; ++cnt) {
      endp = strchr(cp, '\n');
      if (endp) 
	*endp = 0;
      valp->vals[cnt]=my_strdup(cp, "char2val 3");
      if (endp) {
	*endp = '\n';
	cp = endp + 1;
      } else {
	/* non '\n' terinated string, must be last line, we are done */
	++cnt;
	assert(cnt == linecnt);
	break;
      }
    }
  }
  return 0;
}


/****************************************************
  val_comp:
    compare two values per cond
    returns result of comparison
****************************************************/
static boolean
val_cmp(const valstruct *left, const valstruct *right, vtw_cond_e cond)
{
  unsigned int left_parts[9], right_parts[9];
  vtw_type_e val_type;
  int parts_num, lstop, rstop, lcur, rcur;
  char const *format;
  char *lval, *rval;
  int ret=0, step=0, res=0;

  val_type = left->val_type;
  if (left->cnt)
    lstop = left->cnt;
  else
    lstop = 1;
  if (right->cnt)
    rstop = right->cnt;
  else
    rstop = 1;
  
  for(lcur = 0; lcur < lstop; ++lcur) {
    if (!lcur && !left->cnt)
      lval = left->val;
    else
      lval = left->vals[lcur];
    for(rcur = 0; rcur < rstop; ++rcur) {
      if (!rcur && !right->cnt)
	rval = right->val;
      else
	rval = right->vals[rcur];

      //don't bother comparing if these are different types.
      if ((rcur || right->cnt) 
	  && right->val_types != NULL
	  && right->val_types[rcur] != ERROR_TYPE) {
	if (right->val_types[rcur] != val_type) {
	  continue;
	}
      }

      parts_num = 0;
      switch (val_type) {
      case IPV6_TYPE:
	parts_num = 8;
	goto ipv6_common;
      case IPV6NET_TYPE:
	parts_num = 9;
      ipv6_common:
	scan_ipv6(lval,left_parts);
	scan_ipv6(rval,right_parts);
	break;
      case IPV4_TYPE:
      case IPV4NET_TYPE:
      case MACADDR_TYPE:
      case INT_TYPE:
	format = cond_formats[val_type];
	parts_num = cond_format_lens[val_type];
	(void) sscanf(lval, format, left_parts, left_parts+1, 
		      left_parts+2, left_parts+3, left_parts+4,
		      left_parts+5); 

	if ((rcur || right->cnt) 
	    && right->val_types != NULL
	    && right->val_types[rcur] != ERROR_TYPE) {
	  format = cond_formats[right->val_types[rcur]];
	}
	(void) sscanf(rval, format, right_parts, right_parts+1, 
		      right_parts+2, right_parts+3, right_parts+4,
		      right_parts+5); 
	break;
      case TEXT_TYPE:
      case BOOL_TYPE:
	res = strcmp(lval, rval);
	goto done_comp;
      default:
	bye("Unknown value in switch on line %d\n", __LINE__);
      }
      /* here to do a multistep int compare */
      for (step = 0; step < parts_num; ++ step) {
	if (left_parts[step] > right_parts[step]) {
	  res = 1;
	  break; /* no reason to continue checking other steps */
	}
	if (left_parts[step] < right_parts[step]) {
	  res = -1;
	  break; /* no reason to continue checking other steps */
	}
	res = 0;
      }
    done_comp:
      if (res > 0) 
	res = 1;
      else if (res < 0) 
	res = -1;
      
      ret = ((res == cond1[cond]) || 
	     (res == cond2[cond]));

      if (ret && cond == IN_COND) {
	/* one success is enough for right cycle 
	   in case of IN_COND, continue left cycle */
	break;
      }

      if (!ret && cond != IN_COND) 
	/* one failure is enough in cases 
	   other than IN_COND - go out */
	return ret;
      /* in all other cases: 
	 (fail & IN_COND) or (success & !IN_COND) 
	 contniue checking; */
    }
  }
  return ret;
}



/****************************************************
  check_comp:
    evaluate comparison node.
    returns boolean value of result
****************************************************/
static boolean check_comp(vtw_node *cur)
{
  int ret;
  int status;
  valstruct left, right;

  memset(&left, 0 , sizeof(left));
  memset(&right, 0 , sizeof(right));
  ret = FALSE; /* in case of status */
  status = eval_va(&left, cur->vtw_node_left);
  DPRINT("check_comp left status=%d type=%d cnt=%d val=[%s]\n",
         status, left.val_type, left.cnt, left.val); 
  if (status) 
    goto free_and_return;
  status = eval_va(&right, cur->vtw_node_right);
  DPRINT("check_comp right status=%d type=%d cnt=%d val=[%s]\n",
         status, right.val_type, right.cnt, right.val); 
  if (status)
    goto free_and_return;
  if(left.val_type != right.val_type) {
    printf("Different types in comparison\n");
    goto free_and_return;
  }
  ret = val_cmp(&left, &right,cur->vtw_node_aux);
 free_and_return:
  if (left.free_me)
    free_val(&left);
  if (right.free_me)
    free_val(&right);
  return ret;
}

/******************
  Change value of var in the file

*****************/

static int write_value_to_file(const char* var_path,const char* value) {

  if(var_path && value) {

    {
      /*Build directory, if necessary:*/
      clind_path_ref var_dir_path=clind_path_construct(var_path);
      
      if(!var_dir_path) bye("Can not construct path %s", var_path);
      else {

	char* end = clind_path_pop_string(var_dir_path);
	
	if(!end || strcmp(end,VAL_NAME)) {
	  bye("Wrong end of path: %s (%s)", end,var_path);
	}
	    
	free(end);end=NULL;
	
	touch();
	touch_dir(clind_path_get_path_string(var_dir_path));

	clind_path_destruct(&var_dir_path);
      }
    }

    {
      /*Write to file*/
      FILE* fp = fopen(var_path, "w"); 
      if(!fp) bye("Can not open value file %s", var_path);
	  
      if (fputs(value, fp) < 0 || fputc('\n',fp) < 0)
	bye("Error writing file %s", var_path);
      
      fclose(fp);

    }
  }

  return 0;
}

static int change_var_value(const char* var_reference,const char* value, int active_dir) {

  int ret=-1;

  if(var_reference && value) {
    if (var_ref_handle) {
      /* handle is set => we are in cstore operation.  */
      if (cstore_set_var_ref(var_ref_handle, var_reference, value,
                             active_dir)) {
        ret = 0;
      }
    } else {
      /* legacy usage */
      char* var_path=NULL;
      clind_path_ref n_cfg_path=NULL;
      clind_path_ref n_tmpl_path=NULL;
      clind_path_ref n_cmd_path=NULL;

      if(set_reference_environment(var_reference,
                                   &n_cfg_path,
                                   &n_tmpl_path,
                                   &n_cmd_path,
                                   active_dir)==0) {
        clind_val cv;
        memset(&cv,0,sizeof(cv));
        if(clind_config_engine_apply_command_path(n_cfg_path,
                                                  n_tmpl_path,
                                                  n_cmd_path,
                                                  FALSE,
                                                  &cv,
                                                  get_tdirp(),
                                                  TRUE,
                                                  active_dir)==0) {
          var_path=cv.value;
        }
      }

      if(n_cfg_path) clind_path_destruct(&n_cfg_path);
      if(n_tmpl_path) clind_path_destruct(&n_tmpl_path);
      if(n_cmd_path) clind_path_destruct(&n_cmd_path);

      if(var_path) {
        ret=write_value_to_file(var_path,value);
        free(var_path);
      }
    }
  }

  return ret;
}

int system_out(char *command, const char *prepend_msg, boolean eloc);

/****************************************************
 check_syn:
   evaluate syntax tree;
   returns TRUE if all checks are OK,
   returns FALSE if check fails.
****************************************************/
static boolean check_syn_func(vtw_node *cur,const char *prepend_msg,boolean format,const char* func,int line)
{
  int status;
  int ret;
  int ii;

  switch(cur->vtw_node_oper) {
  case LIST_OP:
    ret = TRUE;
    if (is_in_commit() || !cur->vtw_node_aux) {
      ret = check_syn(cur->vtw_node_left,prepend_msg,format);
    }
    if (!ret || !cur->vtw_node_right) /* or no right operand */
      return ret;
    return check_syn(cur->vtw_node_right,prepend_msg,format);
  case HELP_OP:
    ret = check_syn(cur->vtw_node_left,prepend_msg,format);
    if (ret <= 0){
      if (expand_string(cur->vtw_node_right->vtw_node_string) == VTWERR_OK) {
	//NEED TO PROCESS THIS ACCORDING TO ERROR LOC STRING...
	
	if (strstr(exe_string,"_errloc_:[") != NULL) {
	  if (format == FALSE) { 
	    OUTPUT_USER("%s\n\n",exe_string+strlen("_errloc_:"));
	  }
	  else {
	    OUTPUT_USER("%s\n\n",exe_string);
	  }
	}
	else {
	  //currently set to format option for GUI client.
	  if (prepend_msg != NULL) {
	    if (format == FALSE) { 
	      OUTPUT_USER("[%s]\n%s\n\n",prepend_msg,exe_string);
	    }
	    else {
	      OUTPUT_USER("_errloc_:[%s]\n%s\n\n",prepend_msg,exe_string);
	    }
	  }
	  else {
	    OUTPUT_USER("%s\n",exe_string);
	  }
	}	
      }
    }
    return ret;

  case ASSIGN_OP:

    if (is_in_exec()) {

      valstruct right;
	
      char* var_reference = NULL;
      
      memset(&right, 0, sizeof(right));
      status = eval_va(&right, cur->vtw_node_right);

      if (status || right.cnt) { /* bad or multi */
	if (right.free_me) free_val(&right);
	return FALSE;
      }

      if (strncmp(cur->vtw_node_left->vtw_node_string,
                  VAR_REF_MARKER, VAR_REF_MARKER_LEN) != 0) {
        /* bad reference. should not happen */
        return FALSE;
      }
      /* point to char next to '(' */
      var_reference = strdup(cur->vtw_node_left->vtw_node_string
                             + VAR_REF_MARKER_LEN);
      
      {
	int i=0;
	while(var_reference[i]) {
	  if(var_reference[i]==')') {
	    var_reference[i]=0;
	    break;
	  }
	  i++;
	}
      }

      change_var_value(var_reference,right.val,FALSE);
      change_var_value(var_reference,right.val,TRUE);

      if (right.free_me) free_val(&right);

      if(var_reference) free(var_reference);
    }

    return TRUE;

  case EXEC_OP:
    /* for every value */
    if (in_validate_val) {
      char *save_at = get_at_string();

      for(ii = 0; ii < validate_value_val.cnt || ii == 0; ++ii) {
	set_at_string(validate_value_val.cnt?
	  validate_value_val.vals[ii]:validate_value_val.val);
	status = expand_string(cur->vtw_node_left->vtw_node_string);
	if (status != VTWERR_OK) {
	  set_at_string(save_at);
	  return FALSE;
	}
	ret = system_out(exe_string,prepend_msg,format);
	if (ret) {
	  set_at_string(save_at);
	  return FALSE;
	}
      }
      set_at_string(save_at);
      return TRUE;
    }
    /* else */
    status = expand_string(cur->vtw_node_left->vtw_node_string);
    if (status != VTWERR_OK) {
      return FALSE;
    }
    ret = system_out(exe_string,prepend_msg,format);
    return !ret;
    
  case PATTERN_OP:  /* left to var, right to pattern */
    {
      valstruct left;
      regex_t myreg;
      boolean ret;
      int ii;

      ret = TRUE;
      status = eval_va(&left, cur->vtw_node_left);
      if (status) {
	ret = FALSE;
	goto free_and_return;
      }
      status = regcomp(&myreg, cur->vtw_node_right->vtw_node_string,
                       REG_EXTENDED);
      if (status)
	bye("Can not compile regex |%s|, result %d\n", 
	    cur->vtw_node_right->vtw_node_string, status);
	/* for every value */
	for(ii = 0; ii < left.cnt || ii == 0; ++ii) {
	status = regexec(&myreg, left.cnt?
			 left.vals[ii]:left.val,
			 0, 0, 0);
	if(status) {
	  ret = FALSE;
	  break;
	}
      }
    free_and_return:
      if (left.free_me)
	free_val(&left);
      return ret;
    }
    
  case OR_OP:
    ret = check_syn(cur->vtw_node_left,prepend_msg,format) || 
      check_syn(cur->vtw_node_right,prepend_msg,format);
    return ret;
  case AND_OP:
    ret = check_syn(cur->vtw_node_left,prepend_msg,format) && 
      check_syn(cur->vtw_node_right,prepend_msg,format);
    return ret;
  case NOT_OP:
    ret = check_syn(cur->vtw_node_left,prepend_msg,format);
    return !ret;
    
  case COND_OP:   /* aux field specifies cond type (GT, GE, etc.)*/
    ret = check_comp(cur);
    return ret;
      
  case VAL_OP:
    bye("VAL op in check_syn\n");
  case VAR_OP:
    bye("VAR op in check_syn\n");
  default:
    bye("unknown op %d in check_syn\n", cur->vtw_node_oper);
  }
  /* not reachable */
  return FALSE;
}

/*************************************************
  copy_path:
    copy path
    if destination path owns memory, free it
**************************************************/
static void
copy_path(vtw_path *to, vtw_path *from)
{
  if (to->path_buf)
    my_free(to->path_buf);
  if (to->path_ends)
    my_free(to->path_ends);
  *to = *from;
  to->path_buf = (char *) my_malloc(from->path_alloc+2, "copy_path1");
  memcpy(to->path_buf, from->path_buf, to->path_alloc + 1);
  to->path = to->path_buf + (from->path-from->path_buf);
  to->path_ends = (int *) my_malloc(to->path_ends_alloc * sizeof(int),
				    "copy_path2");
  memcpy(to->path_ends, from->path_ends, 
	 to->path_ends_alloc * sizeof(int));
}

/*****************************************************
  eval_va:
    converts VAR_OP or VAL_OP node into valstruct
    in case of VAR_OP we need to find corresponding
    template node to obtain type. 
	
*****************************************************/
static int eval_va(valstruct *res, vtw_node *node)
{  
  char *cp=NULL;
  char *pathp=NULL;
  int   status=0;

  switch (node->vtw_node_oper) {
  case VAR_OP:

    {
      char *endp = 0;
      
      pathp = node->vtw_node_string;
      DPRINT("eval_va var[%s]\n", pathp);
      
      assert(strncmp(pathp, VAR_REF_MARKER, VAR_REF_MARKER_LEN) == 0);
      pathp += VAR_REF_MARKER_LEN;

      if(pathp[0] == '@' && pathp[1]!='@'){
	/* this is why we passed at_val all around */
	*res = validate_value_val;
	res->free_me = FALSE;
	return 0;
      }

      memset(res,0,sizeof(*res));
      
      if ((endp = strchr(pathp, ')')) == NULL) {
	printf("invalid VAR_OP [%s]\n", node->vtw_node_string);
	return VTWERR_BADPATH;
      }
      
      *endp = 0;
      
      if (var_ref_handle) {
        /* handle is set => we are in cstore operation. */
        vtw_type_e vtype;
        char *vptr = NULL;
        if (!cstore_get_var_ref(var_ref_handle, pathp, &vtype, &vptr,
                                is_in_delete_action())) {
          status = -1;
        } else {
          /* success */
          status = 0;
          if(vptr) {
            res->val_type = vtype;
            res->val_types = NULL;
            res->free_me = TRUE;
            res->val = vptr;
          }
        }
      } else {
        /* legacy usage */
        clind_path_ref n_cfg_path=NULL;
        clind_path_ref n_tmpl_path=NULL;
        clind_path_ref n_cmd_path=NULL;
        if(set_reference_environment(pathp,
                                     &n_cfg_path,
                                     &n_tmpl_path,
                                     &n_cmd_path,
                                     is_in_delete_action())==0) {
          clind_val cv;

          memset(&cv,0,sizeof(cv));

          status=clind_config_engine_apply_command_path(n_cfg_path,
                                                        n_tmpl_path,
                                                        n_cmd_path,
                                                        TRUE,
                                                        &cv,
                                                        get_tdirp(),
                                                        FALSE,
                                                        is_in_delete_action());

          if(status==0) {
            if(cv.value) {
              res->val_type = cv.val_type;
              res->val_types = NULL;
              res->free_me = TRUE;
              res->val = cv.value;
            }
          }
        }
      
        if(n_cfg_path) clind_path_destruct(&n_cfg_path);
        if(n_tmpl_path) clind_path_destruct(&n_tmpl_path);
        if(n_cmd_path) clind_path_destruct(&n_cmd_path);
      }

      *endp = ')';

      return status;
    }

  case VAL_OP:
    DPRINT("eval_va val[%s]\n", res->val);
    *res = node->vtw_node_val;
    res->free_me = FALSE; 
    return 0;
  case B_QUOTE_OP:
    {
      FILE *f;
      int a_len, len, rd;

      status = expand_string(node->vtw_node_string);
      if (status != VTWERR_OK) {
	return FALSE;
      }

      f = popen(exe_string, "r");
      if (!f)
	return -1;
#define LEN 24
      len = 0;
      cp = my_malloc(LEN,"");
      a_len = LEN;
      for(;;){
	rd = fread(cp + len, 1, a_len - len , f);
	len += rd;
	if (len < a_len)
	  break;
	cp = my_realloc(cp, a_len+LEN, "");
	a_len += LEN;
      }
      cp[len] = 0;
      pclose(f);
      memset(res, 0, sizeof (*res));
      res->val_type = TEXT_TYPE;
      res->free_me = TRUE;
      res->val = cp;
    }
    return 0;
  default:
    return 0;
  }
}

/**********************************************************
 expand_string:
   expand string replacing var references with the appropriate 
   values, the formed string is collected in the buffer pointed 
   at by the global exe_string. The buffer dynamically allocated 
   and reallocated.
***********************************************************/
static int expand_string(char *stringp)
{
  char *scanp;
  char *resp = exe_string;
  int left = exe_string_len; 
  int my_len;
  int len;

  scanp = stringp; /* save stringp for printf */

  do{

    if (left <= 1){
       my_len = resp - exe_string;
       exe_string_len += EXE_STRING_DELTA;
       exe_string = my_realloc(exe_string, exe_string_len, "expand_string 1");
       left += EXE_STRING_DELTA;
       resp = exe_string + my_len;
       /* back in business */
    }
    if (*scanp != '$') {
      /* we don't check for '\''$' any more.
       * only "$VAR(" is significant now */
      *resp++ = *scanp++;
      --left;
    } else if (strlen(scanp) < (VAR_REF_MARKER_LEN + 1 + 1)) {
      /* shorter than "$VAR(@)". cannot be a reference */
      *resp++ = *scanp++;
      --left;
    } else if (strncmp(scanp, VAR_REF_MARKER, VAR_REF_MARKER_LEN) != 0) {
      /* doesn't start with "$VAR(". not a reference */
      *resp++ = *scanp++;
      --left;
    } else {
      /* the first char is '$'
       * && remaining length is enough for a reference
       * && starts with marker.
       */
      char *cp=NULL;
      boolean my_cp=FALSE;

      /* advance scanp to 'R'. */
      scanp += 3;

      if(scanp[2] == '@' && scanp[3] == ')') {

	cp = get_at_string();
	my_cp = FALSE;
	scanp += 4;

      } else {
	char *endp;

	endp = strchr(scanp, ')');
	if (!endp ){
	  return -1;
	}

	scanp += 2;
	/* path reference */
	*endp = 0;
	if (endp == scanp)
	  bye("Empty path");	  
	
        if (var_ref_handle) {
          /* handle is set => we are in cstore operation. */
          vtw_type_e vtype;
          char *vptr = NULL;
          if (cstore_get_var_ref(var_ref_handle, scanp, &vtype, &vptr,
                                 is_in_delete_action()) && vptr) {
            cp = vptr;
          }
        } else {
          /* legacy usage */
          clind_path_ref n_cfg_path=NULL;
          clind_path_ref n_tmpl_path=NULL;
          clind_path_ref n_cmd_path=NULL;

          if(set_reference_environment(scanp,
                                       &n_cfg_path,
                                       &n_tmpl_path,
                                       &n_cmd_path,
                                       is_in_delete_action())==0) {

            clind_val cv;
            memset(&cv,0,sizeof(cv));
            if(clind_config_engine_apply_command_path(n_cfg_path,
                                                  n_tmpl_path,
                                                  n_cmd_path,
                                                  TRUE,
                                                  &cv,
                                                  get_tdirp(),
                                                  FALSE,
                                                  is_in_delete_action())==0) {
              cp=cv.value;
            }

          }

          if(n_cfg_path) clind_path_destruct(&n_cfg_path);
          if(n_tmpl_path) clind_path_destruct(&n_tmpl_path);
          if(n_cmd_path) clind_path_destruct(&n_cmd_path);
        }

	if(!cp) {
	  cp="";
	} else {
	  my_cp=TRUE;
	}
	
	*endp = ')';
	
	scanp = strchr(scanp, ')') + 1;
      }
      len = strlen(cp);
      while(len + 1 > left) { /* 1 for termination */
	my_len = resp - exe_string;
	exe_string_len += EXE_STRING_DELTA;
	exe_string = my_realloc(exe_string, exe_string_len, "expand_string 2");
	left += EXE_STRING_DELTA;
	resp = exe_string + my_len;
	/* back in business */
      }

      strcpy(resp, cp);
      if(my_cp && cp) {
	free(cp);
      }
      resp += len;
      left -= len;
    }

  } while(*scanp);
  
  *resp = 0;
  
  return VTWERR_OK; 
}

/*****************************************************
  free_def:
    free all memory allocated to def
*****************************************************/
void free_def(vtw_def *defp)
{
  vtw_act_type act;
  for(act=0; act<top_act; ++ act)
    if (defp->actions[act].vtw_list_head)
      free_node_tree(defp->actions[act].vtw_list_head);
  if (defp->def_type_help)
    my_free(defp->def_type_help);
  if (defp->def_node_help) {
    my_free(defp->def_node_help);
  }
  if (defp->def_default)
    my_free(defp->def_default);
}

/*****************************************************
  free_node - add node to free list 
*****************************************************/
static void free_node(vtw_node *node)
{
  --node_cnt;
  ++free_node_cnt;
  node->vtw_node_left = vtw_free_nodes;
  vtw_free_nodes = node;
}

/*****************************************************
  free_node_tree - add all nodes of the tree to free list 
*****************************************************/
static void free_node_tree(vtw_node *node)
{
  if (node->vtw_node_left)
    free_node_tree(node->vtw_node_left);
  if (node->vtw_node_right)
    free_node_tree(node->vtw_node_right);
  if (node->vtw_node_string)
    free_string(node->vtw_node_string);
  if (node->vtw_node_val.free_me)
    free_val(&(node->vtw_node_val));
  free_node(node);
}

void free_path(vtw_path *path)
{
  if (path->path_ends)
    my_free(path->path_ends);
  if (path->path_buf) {
    my_free(path->path_buf);
  }
}

#ifdef notused
static void free_reuse_list(void)
{
  vtw_node *next;
  int cnt = 0;
  
  while (vtw_free_nodes) {
    next = vtw_free_nodes->vtw_node_left;
    my_free(vtw_free_nodes);
    ++cnt;
    vtw_free_nodes = next;
    --free_node_cnt;
  }
#if DEBUG
  printf("%d nodes used\n", cnt);
#endif
}
#endif

/*****************************************************
  free_val - dealloc allocated memory of valstruct
*****************************************************/
void free_val(valstruct *val)
{
  int cnt;

  assert(val->free_me);
  if (val->val) 
    my_free(val->val);
  for (cnt = 0; cnt < val->cnt; ++ cnt)
    my_free(val->vals[cnt]);
  if(val->vals)
    my_free(val->vals);
  if(val->val_types) 
    my_free(val->val_types);
}
/*****************************************************
  free_string - dealloc string
  just free for now, we might do something else later
*****************************************************/
static void free_string(char *str)
{
  my_free(str);
}


/*****************************************************
  get_node - take node from free list or allocate
*****************************************************/
static vtw_node * get_node(void)
{
  vtw_node *ret;
  if (vtw_free_nodes){
    ret = vtw_free_nodes;
    vtw_free_nodes = vtw_free_nodes->vtw_node_left;
    --free_node_cnt;
  } else {
    ret = my_malloc(sizeof(vtw_node), "New node");
  }
  ++node_cnt;
  memset(ret, 0, sizeof(vtw_node));
  return ret;
}

/****************************************************
 get_value: 
   for a given path (*path) verify that value exists,
   open it and read it. The pointer to allocated 
   memory is returned in valpp.  It is responsibility
   of a caller to release memory.
   Returns: 
******************************************************/
int get_value(char **valpp, vtw_path *pathp)
{
    struct stat    statbuf;
    int status = VTWERR_OK;
    char const *err = NULL;
    FILE *in = NULL;
    char *valp;
    int   readcnt;

    
    /* find value */
    *valpp = 0;
    push_path(pathp, VAL_NAME);

    if (lstat(pathp->path, &statbuf) < 0) {
      err = "no value file in [%s]\n";
      goto bad_path;
    }
    if ((statbuf.st_mode & S_IFMT) != S_IFREG) {
      err = "no value file in [%s]\n";
      goto bad_path;
    }
    in = fopen(pathp->path, "r");
    if (!in) {
      err = "Can not open value file in [%s]\n";
      goto bad_path;
    }
    valp = my_malloc(statbuf.st_size + 1, "get_value");
    readcnt = fread(valp, 1, statbuf.st_size, in);
    if (readcnt != statbuf.st_size) {
      my_free(valp);
      err = "Error reading value file in [%s]\n";
      goto bad_path;
    }
    valp[statbuf.st_size] = 0;
    /* remove \n at the line end */
    if (valp[statbuf.st_size - 1] == '\n')
      valp[statbuf.st_size - 1] = 0;
    *valpp = valp;
    status = 0;
pop:
    if (in)
      fclose(in);
    pop_path(pathp);
    if (err) {
      fprintf(stderr, err, pathp->path_buf + m_path.print_offset);
      printf(err, pathp->path_buf + m_path.print_offset);
    }
    return status;
 bad_path:
    status = VTWERR_BADPATH;
    goto pop;
}

/*************************************************
  init_path:
    init path, exit if not able (out_of_memory)
**************************************************/
void init_path(vtw_path *path, const char *root)
{
    long   path_len;
    memset(path, 0, sizeof(vtw_path));
    path_len = pathconf(root, _PC_PATH_MAX);
    if (path_len < 0)
	path_len = PATH_DELTA;
    path->path_alloc = path_len - 2; 
    /* 1 byte for null termination, and 1 byte for '/' */
    path->path_buf = path->path = 
      (char *)my_malloc(path_len, "init_path 1");
    strcpy(path->path, root);
    path->path_len = strlen(root);
    path->path_ends = (int *)my_malloc(ENDS_ALLOC * sizeof(int *), 
				       "init_path 2");
    path->path_lev = 1;
    path->path_ends[0] = path->path_len;
    path->path_ends_alloc = ENDS_ALLOC;
}

/*****************************************************
  pop_path - shorten path by one segment
*****************************************************/

void pop_path(vtw_path *path)
{
    if (--path->path_lev < 1) {
      bye("INTERNAL: line %d in %s\n", __LINE__, __FILE__);
    }
    path->path_len = path->path_ends[path->path_lev - 1];
    path->path_buf[path->path_len] = 0;
}
void warrant_path(vtw_path *path, int len)
{
  int delta = path->path - path->path_buf;

  while(path->path_alloc - path->path_len < len + 1){
    path->path_buf = (char *)my_realloc(path->path_buf, 
				    path->path_alloc +
			     PATH_DELTA, "push_path 1");
    path->path_alloc += PATH_DELTA;
  }
  path->path = path->path_buf + delta;
}
/*****************************************************
  push_path - extend path by '/' and one new segment 
*****************************************************/
void push_path(vtw_path *path, const char *segm)
{
    int len;
    const char *cp;
    char *pp;

    for(cp=segm, len=0;*cp;++cp, ++len)
      if(*cp=='%' || *cp=='/')
	len +=2;
    warrant_path(path, len + 1);
    path->path_buf[path->path_len] = '/';
    path->path_buf[++path->path_len] = 0;	
    for(pp=path->path_buf + path->path_len,cp=segm;
	*cp;++cp, ++pp)
      if(*cp=='%') {
	pp[0] = '%';
	pp[1] = '2';
	pp[2] = '5';
	pp += 2;
      }else if (*cp == '/') {
	pp[0] = '%';
	pp[1] = '2';
	pp[2] = 'F';
	pp += 2;
      }else 
	*pp = *cp; 
    *pp = 0;
    path->path_len += len;
    if (path->path_lev == path->path_ends_alloc){
	path->path_ends_alloc += ENDS_ALLOC;
	path->path_ends = (int *)my_realloc(path->path_ends, 
		     sizeof(int *)*path->path_ends_alloc, "push_path 2");
    }
    path->path_ends[path->path_lev++] = path->path_len;
    //    push_path_no_escape();
}

/**
 * Version of above that doesn't escape value before stuffing.
 *
 **/
void push_path_no_escape(vtw_path *path, char *segm)
{
    int len;
    char *cp;
    char *pp;
    
    for(cp=segm, len=0;*cp;++cp, ++len);
    warrant_path(path, len + 1);
    path->path_buf[path->path_len] = '/';
    path->path_buf[++path->path_len] = 0;	
    for(pp=path->path_buf + path->path_len,cp=segm;
	*cp;++cp, ++pp) {
      *pp = *cp; 
    }
    *pp = 0;
    path->path_len += len;
    if (path->path_lev == path->path_ends_alloc){
	path->path_ends_alloc += ENDS_ALLOC;
	path->path_ends = (int *)my_realloc(path->path_ends, 
		     sizeof(int *)*path->path_ends_alloc, "puhs_path 2");
    }
    path->path_ends[path->path_lev++] = path->path_len;
}

/****************************************************
  scan_ipv6:
    scans ipv6 or ipv6net pointed by val 
    and returns as array of integers pointed
    by parts
***************************************************/
static void scan_ipv6(char *val, unsigned int *parts)
{
  int num = 0;
  int num_cnt = 0;
  int dot_dot_pos = -1;
  int dot_cnt = 0;
  char c;
  char *p;
  int base = 16;
  int total = 8;
  int gap;

  p = val;
  if (strncmp(p, ".wh.", 4) == 0)
    p = p + 4;
  for ( ;TRUE; ++p) {
    switch ((c = *p)) {
    case '.':
      if (dot_cnt == 0) {
	/* turn out it was decimal, convert our wrong
	   hex interpretation;
	   decimal may not have more than 3 digits */
	num = (num/256)*100 + (num%256)/16*10 + num%16;
	base = 10;
      }
      ++dot_cnt;
      break;
    case ':':
      if (p[1] == ':'){
	++p;
	dot_dot_pos = num_cnt + 1;
      }
      break;
    case '/':
      base = 10;
      total = 9;
      break;
    case 0:
      break;
    default:
      /* must be a digit */
      c = tolower(*p);
      if (isdigit(c))
	num = num * base + c - '0';
      else
	num = num * base + c - 'a' + 10;
      continue;
    }
    /* close the number */
    /* the case of "::234: etc " 
     handled automatically as 0::234
    with allowing :: to represent 0 or more
    groups instead of 1 or more */
    
    parts[num_cnt] = num;
    num = 0;
    ++num_cnt;
    /* combine two decimal if needed */
    if (dot_cnt == 2 || (dot_cnt == 3 && (c == 0 || c == '/'))) {
      --num_cnt;
      parts[num_cnt - 1] = parts[num_cnt - 1] * 256 + parts[num_cnt];
    }
    if (*p == 0)
      break;
  }
  /* replace '::' with 0s */
  if (dot_dot_pos != -1 && total != num_cnt) {
    int i;
    gap = total - num_cnt;
    if (dot_dot_pos != num_cnt)
      memmove(parts+dot_dot_pos+gap, parts+dot_dot_pos,
	      (num_cnt-dot_dot_pos)*sizeof(int));
    for (i = 0; i<gap; ++i)
      parts[dot_dot_pos+i] = 0;
  }
}

/***************************************************
  switch_path:
   switch m_path between mcd and ccd directories - 
   modified (UNIONFS) configuration and changed 
   (real) configuration
****************************************************/
void switch_path(first_seg *segp)
{
  memcpy(m_path.path_buf + segp->f_segoff, segp->f_segp,
	 segp->f_seglen);
  m_path.path = m_path.path_buf + segp->f_segoff;
}

/*************************************************
  validate_value:
    validates value against type and syntax 
   return TRUE if OK, FALSE otherwise
**************************************************/
boolean validate_value(const vtw_def *def, char *cp)
{
  int        status;
  boolean    ret=TRUE;

  /* certain characters are not allowed */
  {
    int i = 0;
    static const char *disallowed[] = {
      "'", "single quote (')",
      "\n", "newline",
      "\"", "double quote (\")",
      NULL, NULL
    };
    for (i = 0; i < strlen(cp); i++) {
      int j = 0;
      for (j = 0; disallowed[j]; j += 2) {
        if (cp[i] == disallowed[j][0]) {
          OUTPUT_USER("Cannot use the %s character in a value string\n",
                      disallowed[j + 1]);
          return FALSE;
        }
      }
    }
  }

  /* prepare cur_value */
  set_at_string(cp);
  status = char2val(def, cp, &validate_value_val);
  if (status != VTWERR_OK) {
    return FALSE;
  }
  if ((def->def_type!=ERROR_TYPE) && 
      ((validate_value_val.val_type != def->def_type) &&
       (validate_value_val.val_type != def->def_type2))) {
    if (def->def_type_help){
      (void)expand_string(def->def_type_help);
      OUTPUT_USER("%s\n", exe_string);
    } else {
      OUTPUT_USER("\"%s\" is not a valid value of type \"%s\"\n", cp,
                  type_to_name(def->def_type));
    }
    ret = FALSE;
    goto  validate_value_free_and_return;
  }
  ret = TRUE;
  if (def->actions  && def->actions[syntax_act].vtw_list_head){
    in_validate_val = TRUE;
    ret = check_syn(def->actions[syntax_act].vtw_list_head,(const char *)NULL,FALSE);
    in_validate_val = FALSE;
  }
 validate_value_free_and_return:
  free_val(&validate_value_val);
  return ret;
}

int cli_val_read(char *buf, int max_size)
{
  int len;
  
  if (cli_val_len > max_size)
    len = max_size;
  else
    len = cli_val_len;
  if (len) {
    (void)memcpy(buf, cli_val_ptr, len);
    cli_val_len -= len;
    cli_val_ptr += len;
  }
  return len;
}

static void
touch_file(const char *filename)
{
  int fd = creat(filename, 0666);
  if (fd < 0)
    {
      if (errno == EEXIST)
	utime(filename, NULL);
      else
	bye("can't touch %s (%s)", filename, strerror(errno));
    }
  else
    close(fd);
}

static void
touch(void)
{
  char filename[strlen(get_mdirp()) + 20];
  sprintf(filename, "%s/%s", get_mdirp(), MOD_NAME);
  touch_file(filename);
}
  
const char *type_to_name(vtw_type_e type) {
  switch(type) {
  case INT_TYPE: return("u32");
  case IPV4_TYPE: return("ipv4");
  case IPV4NET_TYPE: return("ipv4net");
  case IPV6_TYPE: return("ipv6");
  case IPV6NET_TYPE: return("ipv6net");
  case MACADDR_TYPE: return("macaddr");
  case DOMAIN_TYPE: return("domain");
  case TEXT_TYPE: return("txt");
  case BOOL_TYPE: return("bool");
  default: return("unknown");
  }
}

/********************* New Dir ****************************/

static int set_reference_environment(const char* var_reference,
				     clind_path_ref *n_cfg_path,
				     clind_path_ref *n_tmpl_path,
				     clind_path_ref *n_cmd_path,
				     int active) {

  if(var_reference && n_cfg_path && n_tmpl_path && n_cmd_path) {

    if(*var_reference=='/') {
      
      if(is_in_delete_action()) {
	*n_cfg_path=clind_path_construct(get_adirp());
      } else {
	*n_cfg_path=clind_path_construct(get_mdirp());
      }
      *n_tmpl_path=clind_path_construct(get_tdirp());
      *n_cmd_path=clind_path_construct(var_reference);

    } else {

      vtw_path n_vt_path;
	
      memset(&n_vt_path,0,sizeof(n_vt_path));

      copy_path(&n_vt_path, &m_path);
      /* switch to MPATH */
      memcpy(n_vt_path.path_buf + get_f_seg_m_ptr()->f_segoff, get_f_seg_m_ptr()->f_segp,
	     get_f_seg_m_ptr()->f_seglen);
      n_vt_path.path = n_vt_path.path_buf + get_f_seg_m_ptr()->f_segoff;

      if(active) {

	vtw_path active_path;
	
	memset(&active_path,0,sizeof(active_path));
	
	copy_path(&active_path, &n_vt_path);
	
	memcpy(active_path.path_buf + get_f_seg_a_ptr()->f_segoff, get_f_seg_a_ptr()->f_segp,
	       get_f_seg_a_ptr()->f_seglen);
	active_path.path = active_path.path_buf + get_f_seg_a_ptr()->f_segoff;
	
	*n_cfg_path=clind_path_construct(active_path.path);
        
        free_path(&active_path);
      } else {
	
	*n_cfg_path=clind_path_construct(n_vt_path.path);

      }

      *n_tmpl_path=clind_path_construct(t_path.path);
      *n_cmd_path=clind_path_construct(var_reference);
      
      free_path(&n_vt_path);
    }

    return 0;

  } else {

    return -1;

  }
}

/*** output ***/

int out_fd = -1;
FILE *out_stream = NULL;

int err_fd = -1;
FILE *err_stream = NULL;
int new_out_fd = -1;
int new_err_fd = -1;

int
initialize_output(const char *op)
{
  if ((out_fd = dup(STDOUT_FILENO)) == -1) {
    return -1;
  }
  if ((out_stream = fdopen(out_fd, "a")) == NULL) {
    return -1;
  }
  if ((err_fd = dup(STDOUT_FILENO)) == -1) {
    return -1;
  }
  if ((err_stream = fdopen(err_fd, "a")) == NULL) {
    return -1;
  }

  new_out_fd = open(LOGFILE_STDOUT, O_WRONLY | O_CREAT, 0660);
  new_err_fd = open(LOGFILE_STDERR, O_WRONLY | O_CREAT, 0660);
  if (new_out_fd == -1 || new_err_fd == -1) {
    return -1;
  }
  if ((lseek(new_out_fd, 0, SEEK_END) == ((off_t) -1))
      || (lseek(new_err_fd, 0, SEEK_END) == ((off_t) -1))) {
    return -1;
  }
  if ((dup2(new_out_fd, STDOUT_FILENO) == -1)
      || (dup2(new_err_fd, STDERR_FILENO) == -1)) {
    return -1;
  }

  cli_operation_name = op;
  return 0;
}

int
redirect_output()
{
  if ((dup2(new_out_fd, STDOUT_FILENO) == -1)
      || (dup2(new_err_fd, STDERR_FILENO) == -1)) {
    return -1;
  }
  return 0;
}

int
restore_output()
{
  if ((dup2(out_fd, STDOUT_FILENO) == -1)
      || (dup2(err_fd, STDERR_FILENO) == -1)) {
    return -1;
  }
  return 0;
}

int
system_out(char *cmd, const char *prepend_msg, boolean eloc)
{
  int pfd[2];
  int ret;
  pid_t cpid;

  if (!cmd || (ret = pipe(pfd)) != 0) {
    return -1;
  }

  /* note that the process management mechanism here is a new implementation.
   * this fixes bug 6771, which was broken by the change introduced in
   * commit 792d6aa0dd0ecfd45c9b5ab57c6c0cb71a9b8da6.
   *
   * basically, that commit changed the process management such that the
   * commit process does not terminate if any child process is spawned in a
   * certain way (more specifically, if its STDERR fd is not closed).
   *
   * saying that "STDERR *should* be closed and therefore this is not our
   * fault" is not an acceptable solution since we use many debian packages
   * "as-is", and expecting all child processes spawned by any present and
   * future packages in our system to do that is not practical (nor is it
   * feasible to expect us to keep finding and fixing such cases, which
   * would most likely involve taking ownership of debian packages that we
   * could have used as-is.)
   *
   * the new process management mechanism below does not have this problem.
   */
  if ((cpid = fork())) {
    int status;
    int waited = 0;
    int prepend = 1;

    if (cpid == -1) {
      return -1;
    }

    close(pfd[1]);
    while (1) {
      int sret;
      fd_set readfds;
      struct timeval timeout;
      FD_ZERO(&readfds);
      FD_SET(pfd[0], &readfds);
      timeout.tv_sec = 0;
      timeout.tv_usec = 100000;
      sret = select(pfd[0] + 1, &readfds, NULL, NULL, &timeout);
      if (sret == 1) {
        /* ready for read */
        char buf[128];
        char *out = buf;
        ssize_t count = read(pfd[0], buf, 128);
        if (count <= 0) {
          /* eof or error */
          break;
        }

        /* XXX XXX XXX BEGIN emulating original "error" location handling */

        /* the following code segment is the "logic" for handling "error"
         * location in the original impl. (note that this code is preserved
         * here for demonstration purpose. this is not "commented-out"
         * code.)
         */
        /***
        if (first == TRUE) {
          if (strncmp(buf,errloc_buf,errloc_len) == 0) {
            if (format == FALSE) {
              fprintf(out_stream,"%s",buf+errloc_len);
            }
            else {
              fprintf(out_stream,"%s",buf);
            }
          } else {
            //currently set to format option for GUI client.
            if (prepend_msg != NULL) {
              if (format == FALSE) {
                fprintf(out_stream,"[%s]\n%s",prepend_msg,buf);
              } else {
                fprintf(out_stream,"%s[%s]\n%s",errloc_buf,prepend_msg,buf);
              }
            }
          }
        } else {
          if (strncmp(buf,errloc_buf,errloc_len) == 0 && format == FALSE) {
            fprintf(out_stream,"%s",buf+errloc_len);
          } else {
            fprintf(out_stream,"%s",buf);
          }
        }
        ***/
        /* XXX analysis of above:
         * the main issue is that this seems to indicate that the "error"
         * location can actually be prepended in two different layers (the
         * layer here and the actual command output). the "logic" above
         * seems to be:
         *   (1) for first buffer read
         *     (A) if the lower layer has already prepended "errloc" string
         *       (a) if we DON'T want errloc string, then strip it from
         *           the lower-layer output.
         *       (b) if we DO want the string, let it pass through
         *     (B) if the lower layer did not prepend
         *       (a) if we DON'T want errloc, don't prepend it here
         *       (b) if we DO want the string, prepend it here
         *   (2) for any subsequent buffer reads
         *     (A) if lower layer prepended errloc AND we DON'T want errloc,
         *         strip it from output
         *     (B) otherwise (lower layer did not prepend OR
         *         we DO want errloc), let it pass through
         *
         * note that the handling of subsequent buffer reads makes no sense.
         * the reads can start at any offsets, so if we actually need to
         * strip out any errloc string at the start of any subsequent reads
         * from the command output, then something is very broken here.
         *
         * secondly, assuming (2) is in fact not needed, the main issue
         * in (1) is the fact that the errloc string can be prepended in two
         * different layers, resulting in the "logic" seen above. if the
         * eventual appearance (i.e., errloc or not) is completely determined
         * in this layer here, then such a "design" choice is weird.
         *
         * given the resource availability, at the moment, the only feasible
         * approach here is to emulate the original impl's behavior in terms
         * of "errloc".
         *
         * another (unrelated) issue is that the original impl assumes the
         * buffer reads do not contain any '\0' bytes since it uses
         * fprintf() to output the buffer. A '\0' byte will cause the rest
         * of the buffer to be truncated. the new impl does not have this
         * problem.
         *
         * the logic below emulates the case (1) in the original impl and
         * ignores case (2). if somehow case (2) is indeed necessary, we
         * should really take a good look at the reason and fix the
         * underlying problem. (heck, even (1) is fugly as hell, but
         * right now it's simply not feasible to look into it.)
         */
        if (prepend && out_stream != NULL) {
          prepend = 0;

          /* XXX follow original behavior */
#define errloc_str "_errloc_:"
#define errloc_len 9
          if (count > errloc_len
              && memcmp(buf, errloc_str, errloc_len) == 0) {
            /* XXX lower-layer already prepended errloc, so strip it out if
             * we don't want errloc. AND in such cases we don't want the
             * prepend_msg either. (!?)
             *  It looks like the lower layer will print _errloc_:[prepend_msg]
             * see Vyatta::Config::outputError in perl. 
             * This is why when stripping errloc we don't want prepend_msg. 
             */
            out = (eloc ? buf : (buf + errloc_len));
            count = (eloc ? count : (count - errloc_len));
          } else {
            /* XXX lower-layer did not prepend errloc */
            if (eloc) {
              /* XXX prepend errloc since we want it */
              fprintf(out_stream, "%s", errloc_str);
            }
            /* XXX and in such cases we DO want prepend_msg */
            if (prepend_msg) {
              fprintf(out_stream, "[%s]\n", prepend_msg);
            }
          }
#undef errloc_str
#undef errloc_len
        }

        /* XXX XXX XXX END emulating original "error" location handling */
        if (out_stream != NULL) {
          if (fwrite(out, count, 1, out_stream) != 1) {
            return -1;
          }
          fflush(out_stream);
        }
      } else if (sret == 0) {
        /* timeout */
        if (waitpid(cpid, &status, WNOHANG) == cpid) {
          /* child done */
          waited = 1;
          break;
        }
      } else {
        /* error (-1) */
        break;
      }
    }
    if (!prepend && out_stream != NULL) {
      fprintf(out_stream, "\n");
    }
    close(pfd[0]);
    if (!waited) {
      if (waitpid(cpid, &status, 0) != cpid) {
        return -1;
      }
    }
    return (WIFEXITED(status) ? WEXITSTATUS(status) : 1);
  } else {
    /* child process */
    close(pfd[0]);
    if ((ret = dup2(pfd[1], STDOUT_FILENO) < 0)
        || (ret = dup2(pfd[1], STDERR_FILENO)) < 0) {
      exit(1);
    }
    char *eargs[] = { "sh", "-c", cmd, NULL };
    execv("/bin/sh", eargs);
    return -1; /* should not get here */
  }
}

