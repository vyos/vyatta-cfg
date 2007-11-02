#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "cli_val.h"
#include "cli_objects.h"
#include "cli_path_utils.h"

static void make_dir(void);
static void handle_defaults(void);

static void make_dir()
{
  touch_dir(m_path.path);
}
/***************************************************
  set_validate:
    validate value against definition
    return TRUE if OK, FALSE otherwise
****************************************************/
boolean set_validate(vtw_def *defp, char *valp, boolean empty_val)
{  
  boolean res;
  int status;
  struct stat    statbuf;
  char* path_end=NULL;

  if (!empty_val) {
    int i = 0;
    int val_len = strlen(valp);

    for (i = 0; i < val_len; i++) {
      if (valp[i] == '\'') {
        fprintf(out_stream, "Cannot use the \"'\" (single quote) character "
                            "in a value string\n");
        exit(1);
      }
    }

    {
      clind_path_ref tp = clind_path_construct(t_path.path);
      if(tp) {
	path_end=clind_path_pop_string(tp);
      }
      clind_path_destruct(&tp);
    }

    if (strcmp(path_end, TAG_NAME) == 0) {
      /* it was a tag, so the def is at 1 level up */
      pop_path(&t_path);
    } else {
      /* it's actual value, so the tmpl path is fine */
      free(path_end);
      path_end = NULL;
    }
  }
  push_path(&t_path, DEF_NAME);
  if (lstat(t_path.path, &statbuf) < 0 || 
      (statbuf.st_mode & S_IFMT) != S_IFREG) {
    fprintf(out_stream, "The specified configuration node is not valid\n");
    bye("Can not set value (4), no definition for %s, template %s", 
        m_path.path,t_path.path);
  }
  /* defniition present */
  memset(defp, 0, sizeof(vtw_def));
  if ((status = parse_def(defp, t_path.path, FALSE)))
    exit(status);
  pop_path(&t_path);
  if(path_end) {
    push_path(&t_path,path_end);
    free(path_end);
    path_end=NULL;
  }
  if (defp->def_type == ERROR_TYPE) {
    /* no type in def. setting value not allowed. */
    fprintf(out_stream, "The specified configuration node is not valid\n");
    bye("Can not set value: no type for %s, template %s", 
        m_path.path, t_path.path);
  }
  if (empty_val) {
    if (defp->def_type != TEXT_TYPE || defp->tag || defp->multi){
      printf("Empty string may be assigned only to TEXT type leaf node\n");
      fprintf(out_stream, "Empty value is not allowed\n");
      return FALSE;
    }
    return TRUE;
  }
  res = validate_value(defp, valp);
  return res;
}

int main(int argc, char **argv)
{
  
  int ai;
  struct stat    statbuf;
  vtw_def        def;
  boolean        last_tag;
  int            status;
  FILE          *fp;
  boolean        res;
  char          *cp;
  char          *command;
  boolean       need_mod = FALSE, not_new = FALSE;
  boolean       empty_val = FALSE;

  if (initialize_output() == -1) {
    exit(-1);
  }

  dump_log( argc, argv);
  init_edit();
  last_tag = FALSE;

  /* extend both paths per arguments given */
  /* last argument is new value */
  for (ai = 1; ai < argc; ++ai) {
    if (!*argv[ai]) { /* empty string */
      if (ai < argc -1) {
        fprintf(out_stream, "Empty value is not allowed after \"%s\"\n",
                argv[ai - 1]);
	bye("empty string in argument list \n");
      }
      empty_val = TRUE;
      last_tag = FALSE;
      break;
    }
    push_path(&t_path, argv[ai]);
    push_path(&m_path, argv[ai]);
    if (lstat(t_path.path, &statbuf) >= 0) {
      if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
	bye("INTERNAL:regular file %s in templates", t_path.path);
      }
      last_tag = FALSE;
      continue;
    } /*else */
    pop_path(&t_path);
    push_path(&t_path, TAG_NAME);
    if (lstat(t_path.path, &statbuf) >= 0) {
      if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
	bye("INTERNAL:regular file %s in templates", t_path.path);
      }
      last_tag = TRUE;
      /* every time tag match, need to verify*/
      if(!set_validate(&def, argv[ai], FALSE)) {
	exit(1);
      }
      continue;
    } 
    /* no match */
    break;
  }

  if (ai == argc) {
    /* full path found */
    /* every tag match validated already */
    /* non tag matches are OK by definition */
    /* do we already have it? */
    if (lstat(m_path.path, &statbuf) >= 0) {
      bye("Already exists %s", m_path.path + strlen(get_mdirp()));
    }
    /* else */
    /* prevent value node without actual value */
    push_path(&t_path, DEF_NAME);
    if (lstat(t_path.path, &statbuf) >= 0) {
      memset(&def, 0, sizeof(vtw_def));
      if ((status = parse_def(&def, t_path.path, FALSE)))
	exit(status);
      if (def.def_type != ERROR_TYPE && !def.tag) {
        fprintf(out_stream,
                "The specified configuration node requires a value\n");
	bye("Must provide actual value\n");
      }
      if (def.def_type == ERROR_TYPE && !def.tag) {
	pop_path(&t_path);
	if(!validate_value(&def, "")) {
	  exit(1);
	}
	push_path(&t_path, DEF_NAME);
      }
    }
    touch();
    pop_path(&t_path);
    make_dir();
    handle_defaults();
    exit(0); 
  }
  if(ai < argc -1 || last_tag) {
    fprintf(stderr, "There is no appropriate template for %s",
            m_path.path + strlen(get_mdirp()));
    fprintf(out_stream, "The specified configuration node is not valid\n");
    exit(1);
  }
  /*ai == argc -1, must be actual value */
  if (!empty_val) {
    pop_path(&m_path); /* pop the actual value at the end */
    pop_path(&t_path); /* pop the "node.tag" */
  }
  handle_defaults();

  if(!set_validate(&def, argv[argc-1], empty_val)) {
    exit(1);
  }
    push_path(&m_path, VAL_NAME);
  /* set value */
  if (lstat(m_path.path, &statbuf) >= 0) {
    valstruct new_value, old_value;
    not_new = TRUE;
    if ((statbuf.st_mode & S_IFMT) != S_IFREG)
      bye("Not a regular file at path \"%s\"", m_path.path);
    /* check if this new value */
    status = char2val(&def, argv[argc - 1], &new_value);
    if (status)
      exit(0);
    cp = NULL;
    pop_path(&m_path); /* get_value will push VAL_NAME */
    status = get_value(&cp, &m_path);
    if (status != VTWERR_OK)
      bye("Cannot read old value %s\n", m_path.path);
    status = char2val(&def, cp, &old_value);
    if (status != VTWERR_OK)
      bye("Corrupted old value ---- \n%s\n-----\n", cp);
    res = val_cmp(&new_value, &old_value, IN_COND);
    if (res) {
      if (def.multi) {
        printf("Already in multivalue\n");
      } else {
        printf("The same value \"%s\" for path \"%s\"\n", cp, m_path.path);
      }
      /* not treating as error */
      exit(0);
    }
  } else {
    pop_path(&m_path);
  }
  make_dir(); 
  push_path(&m_path, VAL_NAME);
  if(not_new && !def.multi) {
    /* it is not multi and seen from M */
    /* is it in C */
    switch_path(CPATH);
    if (lstat(m_path.path, &statbuf) < 0) 
      /* yes, we are modifying original value */
      need_mod = TRUE;
    switch_path(MPATH);
  }
  touch();
  /* in case of multi we always append, never overwrite */
  /* in case of single we always overwrite */
  /* append and overwrite work the same for new file */
  fp = fopen(m_path.path, def.multi?"a":"w"); 
  if (fp == NULL)
    bye("Can not open value file %s", m_path.path);
  if (fputs(argv[argc-1], fp) < 0 || fputc('\n',fp) < 0)
    bye("Error writing file %s", m_path.path);
  if (need_mod) {
    pop_path(&m_path); /* get rid of "value" */
    command = my_malloc(strlen(m_path.path) + 30, "set");
    sprintf(command, "touch %s/" MOD_NAME, m_path.path);
    system(command);
  }
  return 0;
}

static char *
last_path_segment(const char *path)
{
  char *tmp = strrchr(path, '/');
  return ((tmp) ? (tmp + 1) : tmp);
}

/* handle_default(mpath, tpath, exclude)
 *  create any nodes with default values at the current level.
 *  mpath: working path
 *  tpath: template path
 *  exclude: path to exclude
 */
static void
handle_default(vtw_path *mpath, vtw_path *tpath, char *exclude)
{
  DIR *dp;
  struct dirent *dirp;
  char *uename = NULL;
  struct stat statbuf;
  vtw_def def;
  int status;
  FILE *fp;

  if ((dp = opendir(tpath->path)) == NULL) {
    perror("handle_default: opendir");
    exit(-1);
  }

  while ((dirp = readdir(dp)) != NULL) {
    if (strcmp(dirp->d_name, ".") == 0
        || strcmp(dirp->d_name, "..") == 0
        || strcmp(dirp->d_name, MOD_NAME) == 0
        || strcmp(dirp->d_name, DEF_NAME) == 0
        || strcmp(dirp->d_name, TAG_NAME) == 0
        || strcmp(dirp->d_name, exclude) == 0) {
      continue;
    }
    if (uename) {
      free(uename);
      uename = NULL;
    }
    uename = clind_unescape(dirp->d_name);
    push_path(tpath, uename);
    if (lstat(tpath->path, &statbuf) < 0) {
      fprintf(stderr, "no template directory [%s]\n", tpath->path);
      pop_path(tpath);
      continue;
    }
    if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
      fprintf(stderr, "non-directory [%s]\n", tpath->path);
      pop_path(tpath);
      continue;
    }
    push_path(tpath, DEF_NAME);
    if (lstat(tpath->path, &statbuf) < 0) {
      /* no definition */
      pop_path(tpath); /* definition */
      pop_path(tpath); /* child */
      continue;
    }
    memset(&def, 0, sizeof(def));
    if ((status = parse_def(&def, tpath->path, FALSE))) {
      fprintf(stderr, "parse error in [%s]\n", tpath->path);
      pop_path(tpath); /* definition */
      pop_path(tpath); /* child */
      continue;
    }
    if (def.def_default) {
      push_path(mpath, uename);
      push_path(mpath, VAL_NAME);
      if (lstat(mpath->path, &statbuf) < 0) {
        /* no value. add the default */
        pop_path(mpath);
        touch_dir(mpath->path); /* make sure directory exist */
        push_path(mpath, VAL_NAME);
        fp = fopen(mpath->path, "w");
        if (fp == NULL) {
          bye("Can not open value file %s", mpath->path);
        }
        if (fputs(def.def_default, fp) < 0
            || fputc('\n',fp) < 0) {
          bye("Error writing file %s", mpath->path);
        }
        fclose(fp);
      }
      pop_path(mpath); /* value */
      pop_path(mpath); /* child */
    }
    free_def(&def);
    pop_path(tpath); /* definition */
    pop_path(tpath); /* child */
  }
  if (uename) {
    free(uename);
  }
  closedir(dp);
}

/* handle_defaults()
 *  create any nodes with default values along the current "global"
 *  configuration/template path (m_path/t_path).
 */
static void
handle_defaults()
{
  vtw_path mpath;
  vtw_path tpath;
  char *path_end = strdup("");

  memset(&mpath, 0, sizeof(mpath));
  memset(&tpath, 0, sizeof(tpath));
  copy_path(&mpath, &m_path);
  copy_path(&tpath, &t_path);

  while (mpath.path_lev > 0) {
    handle_default(&mpath, &tpath, path_end);
   
    if (mpath.path_lev == 1) {
      break;
    }

    free(path_end);
    path_end = strdup(last_path_segment(tpath.path));
    pop_path(&mpath);
    pop_path(&tpath);
  }
  
  free(path_end);
  free_path(&mpath);
  free_path(&tpath);
}

