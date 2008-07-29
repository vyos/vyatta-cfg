#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>

#include "cli_val.h"
#include "cli_objects.h"

extern char *cli_operation_name;

static void remove_rf(boolean do_umount)
{
  char *command = NULL;
  touch();
  if (do_umount) {
    command = malloc(strlen(get_mdirp()) + 20);
    sprintf(command, "sudo umount %s", get_mdirp());
    system(command);
  }

  command = realloc(command, strlen(m_path.path) + 10);
  sprintf(command, "rm -rf %s", m_path.path);
  system(command);

  if (do_umount) {
    command = realloc(command, strlen(get_mdirp()) + strlen(get_cdirp()) + 
		      strlen(get_mdirp()) + 100);
    sprintf(command, "sudo mount -t $UNIONFS -o dirs=%s=rw:%s=ro" 
	    " $UNIONFS %s", get_cdirp(), get_adirp(), get_mdirp());
    system(command);
  }
  free(command);
}

static boolean has_default(char **def, int size)
{
  char *buf_ptr;
  FILE *fp = fopen(t_path.path, "r");

  if (fp) {
    char buf[1025];
    while (fgets(buf, 1024, fp)) {
      if (strncmp(buf, "default:", 8) == 0) {
	buf_ptr = index(buf,':');
	if (buf_ptr == NULL) {
	  break;
	}
	buf_ptr++;
	if (size < strlen(buf_ptr)-1) {
	  bye("default buffer size is too small\n");
	}
	memcpy(*def, buf_ptr, strlen(buf_ptr)-1);
	fclose(fp);
	return 0;
      }
    }
    fclose(fp);
  }

  return 1;
}

static void reset_default(const char *def_val)
{
  if (def_val == NULL)
    return;

  char filename[strlen(m_path.path) + 10];
  touch();
  sprintf(filename, "%s/node.val", m_path.path);

  FILE *fp = fopen(filename, "w");
  if (fp == NULL)
    bye("can not open: %s", filename);
  fputs(def_val, fp);
  fclose(fp);

  sprintf(filename, "%s/def", m_path.path);
  touch_file(filename);
}
/***************************************************
  set_validate:
    validate value against definition
    return TRUE if OK, FALSE otherwise
****************************************************/
boolean set_validate(vtw_def *defp, char *valp)
{  
  boolean res;
  int status;
  struct stat    statbuf;

  pop_path(&t_path); /* it was tag or real value */
  push_path(&t_path, DEF_NAME);
  if (lstat(t_path.path, &statbuf) < 0 || 
      (statbuf.st_mode & S_IFMT) != S_IFREG) {
    fprintf(out_stream, "The specified configuration node is not valid\n");
    bye("Can not set value (2), no definition for %s", m_path.path);
  }
  /* defniition present */
  memset(defp, 0, sizeof(vtw_def));
  if ((status = parse_def(defp, t_path.path, FALSE))) {
    bye("parse error: %d\n", status);
  }
  res = validate_value(defp, valp);
  pop_path(&t_path);
  return res;
}

int main(int argc, char **argv)
{
  int ai;
  struct stat    statbuf;
  vtw_def        def;
  boolean        last_tag=0;
  int            status;
  FILE          *fp;
  boolean        res;
  char          *cp, *delp, *endp;
  boolean        do_umount;

  cli_operation_name = "Delete";

  if (initialize_output() == -1) {
    bye("can't initialize output\n");
  }

  if (argc < 2) {
    fprintf(out_stream, "Need to specify the config node to delete\n");
    bye("nothing to delete\n");
  }

  dump_log( argc, argv);
  do_umount = FALSE;
  init_edit();
  /* extend both paths per arguments given */
  /* last argument is new value */
  for (ai = 1; ai < argc; ++ai) {
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
      continue;
    } 
    /* no match */
    break;
  }
  /*
    cases:
    multiple tag-value - not  achild 
    mutilple tag-value - not the last child
    multiple tag-value - last child
    single value modified
    signle value unmodified
    multiple non-tag value - the last value
    multiple non-tag value - not the last value
    regular child 
  */
  if (ai == argc) {
    /* full path found */
    /* all cases except multiple non-tag value */
    /* check for single value */ 
    if (last_tag) {
      /* case of multiple tag-value 
	 was this a real child?
	 was it the last child?
      */
      struct dirent *dirp;
      DIR           *dp;

      if (lstat(m_path.path, &statbuf) < 0) {
        fprintf(out_stream, "Nothing to delete\n");
	bye("Nothing to delete at %s", m_path.path);
      }

      remove_rf(FALSE);
      pop_path(&m_path);
      if ((dp = opendir(m_path.path)) == NULL){
	INTERNAL;
      }
      while ((dirp = readdir(dp)) != NULL) {
	/*do we have real child */
	if (strcmp(dirp->d_name, ".") &&
	    strcmp(dirp->d_name, "..") &&
	    strcmp(dirp->d_name, MOD_NAME)  && /* XXX */
	    strncmp(dirp->d_name, ".wh.", 4) )
	  break; 
      }
      if (dirp == NULL) {
	/* no real children left */
	/* kill parent also */
	remove_rf(FALSE);
      }
      exit(0);
    }
    /*not tag */
    push_path(&t_path, DEF_NAME);
    if (lstat(t_path.path, &statbuf) >= 0 && 
	(statbuf.st_mode & S_IFMT) == S_IFREG) {
      /* defniition present */
      memset(&def, 0, sizeof(vtw_def));
      if ((status = parse_def(&def, t_path.path, FALSE))) {
	bye("parse error: %d\n", status);
      }
      if (!def.tag && !def.multi && def.def_type!= ERROR_TYPE) {
	/* signgle value */
	/* is it modified ==
	   it is in C, but not OPAQUE */
	switch_path(CPATH);
	if(lstat(m_path.path, &statbuf) >= 0) {
	  push_path(&m_path, OPQ_NAME);  
	  if(lstat(m_path.path, &statbuf) < 0) {	  
	    /* yes remove from C only */
	    pop_path(&m_path);
	    remove_rf(TRUE);
	    exit(0);
	  }
	  pop_path(&m_path); /*OPQ_NAME */	  
	} 
	switch_path(MPATH);
      }
    }
    /* else no defnition, remove it also */
    char *def_val;
    def_val = malloc(1025);
    if (has_default(&def_val,1024) == 0) {
      reset_default(def_val);
      free(def_val);
    }
    else {
      remove_rf(FALSE);
    }
    
	//    remove_rf(FALSE);
    exit(0);
  } 
  if(ai < argc -1 || last_tag) {
    fprintf(out_stream, "The specified configuration node is not valid\n");
    bye("There is no appropriate template for %s", 
	  m_path.path + strlen(get_mdirp()));
  }
  /*ai == argc -1, must be actual value */
  pop_path(&m_path); /*it was value, not path segment */
  push_path(&m_path, VAL_NAME);
  /* set value */
  if (lstat(m_path.path, &statbuf) < 0) {
    fprintf(out_stream, "Nothing to delete\n");
    bye("Nothing to delete at %s", m_path.path);
  }
  if ((statbuf.st_mode & S_IFMT) != S_IFREG)
    bye("Not a regular file %s", m_path.path);
  /* get definition to deal with potential multi */
  pop_path(&t_path); /* it was tag or real value */
  push_path(&t_path, DEF_NAME);
  if (lstat(t_path.path, &statbuf) < 0 || 
      (statbuf.st_mode & S_IFMT) != S_IFREG) {
    fprintf(out_stream, "The specified configuration node is not valid\n");
    bye("Can not delete value, no definition for %s", m_path.path);
  }
  /* defniition present */
  memset(&def, 0, sizeof(vtw_def));
  if ((status = parse_def(&def, t_path.path, FALSE))) {
    bye("parse error: %d\n", status);
  }
  if (def.multi) {
    /* delete from multivalue */
    valstruct new_value, old_value;
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
    if (!res) {
      fprintf(out_stream, "%s is not a configured value\n", new_value.val);
      bye("Not in multivalue");
    }
    touch();
    if (old_value.cnt) {
      push_path(&m_path, VAL_NAME);
      fp = fopen(m_path.path, "w");
      if (fp == NULL)
	bye("Can not open value file %s", m_path.path);
      if (is_in_cond_tik()) {
	for(delp=cp;delp && is_in_cond_tik(); dec_in_cond_tik()) {
	  delp = strchr(delp, '\n');
	  if (!delp)
	    INTERNAL;
	  ++delp; /* over \n */
	}
	/* write "left" of deleted */
	fwrite(cp, 1, delp-cp, fp);
      }else
	delp = cp;
      /* find end of value */
      endp = strchr(delp, '\n');
      if (endp && *++endp) {
	/* write "right" of deleted */
	fwrite(endp, 1, strlen(endp), fp);
        /* need the final '\n' */
        fwrite("\n", 1, 1, fp);
      }
      fclose(fp);
      return 0;
    }
    /* it multi with only 1 value, remove */
    remove_rf(FALSE);
    return 0;
  }

  /*
    let's do a new check here:
    -> if this is a leaf and there is a value look for a match of the value
    -> make sure to check existing configuration as well as uncommitted config
   */
  if (ai+1 == argc) {
    //does this work up until the last value
    pop_path(&m_path);
    if(lstat(m_path.path, &statbuf) == 0) {
      //now compare last value with that in the node.def file to determine whether to accept this delete
      status = get_value(&cp, &m_path);
      if (status != VTWERR_OK) {
	bye("Cannot read old value %s\n", m_path.path);
      }
      if (!strcmp(cp,argv[argc - 1])) {
	/* Also need to handle the case where the value is not specified. */
	char *def_val;
	def_val = malloc(1025);
	if (has_default(&def_val,1024) == 0) {
	  reset_default(def_val);
	  free(def_val);
	}
	else {
	  remove_rf(FALSE);
	}
	return 0;
      }
    }
  }


  fprintf(out_stream, "The specified configuration node is not valid\n");
  bye("There is no appropriate template for %s", 
      m_path.path + strlen(get_mdirp()));

  return 1;
}

