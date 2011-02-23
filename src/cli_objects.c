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

#include "cli_val_engine.h"

#include "cli_objects.h"

/************************ Storage area: *****************/

static char *at_string=NULL;
static boolean in_delete_action=FALSE;
static valstruct cli_value;
static boolean in_commit=FALSE; /* TRUE if in commit program*/
static boolean in_exec=FALSE; /* TRUE if in exec */
static first_seg f_seg_a;
static first_seg f_seg_c;
static first_seg f_seg_m;

/******************** Accessors: ************************/

static char at_buffer[1024]={0};

/* the string to use as $(@), must be set 
   before call to expand_string */
char* get_at_string(void) {
  if(at_string) {
    return at_string;
  } else {
    return at_buffer;
  }
}

void set_at_string(char* s) {
  if(s!=at_buffer) {
    at_string=s;
  } else {
    at_string=NULL;
  }
}

void free_at_string(void) {
  if(at_string) {
    if(at_string!=at_buffer) free(at_string);
    at_string=NULL;
  }
}

boolean is_in_delete_action(void) {
  return in_delete_action;
}

void set_in_delete_action(boolean b) {
  in_delete_action=b;
}

boolean is_in_commit(void) {
  return in_commit;
}

void set_in_commit(boolean b) {
  in_commit=b;
}

boolean is_in_exec(void) {
  return in_exec;
}

void set_in_exec(boolean b) {
  in_exec=b;
}

valstruct* get_cli_value_ptr(void) {
  return &cli_value;
}

first_seg* get_f_seg_a_ptr(void) {
  return &f_seg_a;
}

first_seg* get_f_seg_c_ptr(void) {
  return &f_seg_c;
}

first_seg* get_f_seg_m_ptr(void) {
  return &f_seg_m;
}

const char* get_tdirp(void) {

  const char* tdirp=getenv(ENV_T_DIR);

  if (!tdirp)
    tdirp = DEF_T_DIR;
  if(!tdirp)
    tdirp="";

  return tdirp;
}

const char* get_cdirp(void) {
  return getenv(ENV_C_DIR);
}

const char* get_adirp(void) {

  const char* adirp=getenv(ENV_A_DIR);

  if (!adirp)
    adirp = DEF_A_DIR;
  if(!adirp)
    adirp="";

  return adirp;
}

const char* get_mdirp(void) {

  const char* mdirp=getenv(ENV_M_DIR);

  if(!mdirp)
    mdirp="";

  return mdirp;
}

const char* get_tmpp(void) {

  const char* tmpp=getenv(ENV_TMP_DIR);

  if(!tmpp)
    tmpp="";

  return tmpp;
}

static char *
get_elevp(void)
{
  static char elevp_buffer[2049];
  static char* elevp=NULL;

  if(elevp==NULL) {

    const char* tmp=getenv(ENV_EDIT_LEVEL);

    if(tmp) {
      strncpy(elevp_buffer,tmp,sizeof(elevp_buffer)-1);
      elevp=elevp_buffer;
    }
  } 

  return elevp;
}

static char *
get_tlevp(void)
{
  static char tlevp_buffer[2049];
  static char* tlevp=NULL;

  if(tlevp==NULL) {

    const char* tmp=getenv(ENV_TEMPLATE_LEVEL);

    if(tmp) {
      strncpy(tlevp_buffer,tmp,sizeof(tlevp_buffer)-1);
      tlevp=tlevp_buffer;
    }
  } 

  return tlevp;
}

/************************* Init ***************************/

void init_paths(boolean for_commit)
{
  struct stat    statbuf;
  const char* tdirp = get_tdirp();
  const char* cdirp = get_cdirp();
  const char* adirp = get_adirp();
  const char* mdirp = get_mdirp();
  const char* tmpp = get_tmpp();

  if (!mdirp || !mdirp[0])
    bye("Environment variable %s for temp configuration is not set",ENV_M_DIR);
  if (!cdirp)
    bye("INTERNAL: environment var |%s|  is not set",
	ENV_C_DIR);
  if (!tmpp || !tmpp[0])
    bye("INTERNAL: environment var |%s|  is not set",
	ENV_TMP_DIR);
  /* make sure that template root is present */

  if (lstat(tdirp, &statbuf) < 0)
    bye("Template directory |%s| isn't present\n", tdirp);
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) 
    bye("Template directory |%s| isn't a directory\n", tdirp);  
  /* set paths to current roots */
  if (for_commit) {
    int   max_len;
    const char *startp;

    /* make sure that master configuration root is present */
    if (lstat(adirp, &statbuf) < 0)
      bye("Master configuration directory |%s| isn't present\n", adirp);
    if ((statbuf.st_mode & S_IFMT) != S_IFDIR) 
      bye("Master configuration directory |%s| isn't a directory\n", adirp);  

    get_f_seg_a_ptr()->f_segp = adirp;
    get_f_seg_c_ptr()->f_segp = cdirp;
    get_f_seg_m_ptr()->f_segp = mdirp;
    get_f_seg_a_ptr()->f_seglen = strlen(adirp);
    get_f_seg_c_ptr()->f_seglen = strlen(cdirp);
    get_f_seg_m_ptr()->f_seglen = strlen(mdirp);
    if(get_f_seg_a_ptr()->f_seglen > get_f_seg_m_ptr()->f_seglen) {
      max_len = get_f_seg_a_ptr()->f_seglen;
      startp = adirp;
    }else{
      max_len = get_f_seg_m_ptr()->f_seglen;
      startp = mdirp;
    }
    if(get_f_seg_c_ptr()->f_seglen > max_len) {
      max_len = get_f_seg_c_ptr()->f_seglen;
      startp = cdirp;
    }      
    get_f_seg_a_ptr()->f_segoff = max_len - get_f_seg_a_ptr()->f_seglen;
    get_f_seg_c_ptr()->f_segoff = max_len - get_f_seg_c_ptr()->f_seglen;
    get_f_seg_m_ptr()->f_segoff = max_len - get_f_seg_m_ptr()->f_seglen;
    init_path(&m_path, startp);
    switch_path(get_f_seg_c_ptr());
    m_path.print_offset = max_len;
  } else
    init_path(&m_path, mdirp);

  init_path(&t_path, tdirp);
}

/**********************************************************/
