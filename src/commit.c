#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>

#include "cli_val.h"
#include "cli_objects.h"
#include "cli_parse.h"
#include "cli_path_utils.h"

static char def_name[] = DEF_NAME;
static char tag_name[] = TAG_NAME;
static char opaque_name[] = OPQ_NAME;

static int fin_commit(boolean ok);
static boolean commit_value(vtw_def *defp, char *cp, 
			    vtw_cmode mode, boolean in_txn);
static void perform_create_node();
static void perform_delete_node();
static void perform_move();
static boolean commit_delete_child(vtw_def *pdefp, char *child, 
				   boolean deleting, boolean in_txn);
static boolean commit_delete_children(vtw_def *defp, boolean deleting, 
				      boolean in_txn);
static boolean commit_update_children(vtw_def *defp, boolean creating, 
				      boolean in_txn, boolean *parent_update);

#if BITWISE
static void make_dir()
{
  struct stat    statbuf;
  if (lstat(m_path.path, &statbuf) < 0) {
    char *command;
    command = my_malloc(strlen(m_path.path) + 10, "set"); 
    sprintf(command, "mkdir -p %s", m_path.path);
    system(command);
    free(command);
    return;
  } 
  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
    bye("directory %s expected, found regular file", m_path.path);
  }
  return;
}
#endif

/*************************************************
 validate_dir_for_commit: 
   validate value.value if there is one, validate
   subdirectries names (for tag directory, and for 
   regular directory);
   validate subdirectories
   returns TRUE if OK, FASLE if errors
   exits with status != 0 in case of parse error
*/
static boolean validate_dir_for_commit()
{
    struct stat    statbuf;
    int            status=0;
    vtw_def        def;
    boolean        def_present=FALSE;
    boolean        value_present=FALSE;
    int            subdirs_number=0;
    DIR           *dp=NULL;
    struct dirent *dirp=NULL;
    char          *cp=NULL;
    boolean        ret=TRUE;
    char          *uename = NULL;

#ifdef DEBUG
    printf("validating directory (node_cnt %d, free_node_cnt %d)\n"
	   "t_path |%s|\n",
	   node_cnt, free_node_cnt, t_path.path);
#endif
    
    /* find definition */

    push_path(&t_path, def_name);  /* PUSH 1 */
    if ((lstat(t_path.path, &statbuf) >= 0) &&
	((statbuf.st_mode & S_IFMT) == S_IFREG)) {
      /* definition present */
      def_present = TRUE;
      memset(&def, 0, sizeof(def));
#ifdef DEBUG1
      printf("Parsing definition\n");
#endif
      
      if ((status = parse_def(&def, t_path.path, 
			      FALSE))) {
	exit(status);
      }

    }
#ifdef DEBUG1
    else
      printf("No definition\n");
#endif
    pop_path(&t_path);  /* for PUSH 1 */

    /* look at modified stuff */
    if ((dp = opendir(m_path.path)) == NULL){
      INTERNAL;
    }
    if (def_present && def.tag) {
      push_path(&t_path, tag_name); /* PUSH 2a */
    }

    while ((dirp = readdir(dp)) != NULL) {

      if (strcmp(dirp->d_name, ".") == 0 ||
	  strcmp(dirp->d_name, "..") == 0 ||
	  strcmp(dirp->d_name, MOD_NAME) == 0 ||
	  strcmp(dirp->d_name, opaque_name) == 0 ||
	  strncmp(dirp->d_name, ".wh.", 4) == 0) {
	continue; /*ignore dot and dot-dot*/
      }

      subdirs_number++;

      if(uename)
	my_free(uename);

      uename = clind_unescape(dirp->d_name);

      if (strcmp(uename, VAL_NAME) == 0) {

	value_present=TRUE;

	  /* deal with the value */
	if (!def_present) {
	  printf("There is no definition specified in template\n"
		 "\t%s\n\t - therefore no value permitted\n",
		 t_path.path);
	  ret = FALSE;
	  continue;
	}

	if (def.tag) {
	  printf("Tag specified in template\n"
		 "\t%s\n\t - therefore no value permitted\n",
		 t_path.path);
	  ret = FALSE;
	  continue;
	}

	/* value is OK */
	/* read it */
	cp = NULL;
	status = get_value(&cp, &m_path);
	if (status == VTWERR_OK){
#ifdef DEBUG1
	  printf("Validating value |%s|\n"
		 "for path %s\n", cp, m_path.path);
#endif
	  status = validate_value(&def, cp);
	  ret = ret && status; 
	}
	if (cp) 
	  my_free(cp);
	continue;  
      }

      push_path(&m_path, uename); /* PUSH 3 */
      if (lstat(m_path.path, &statbuf) < 0) {
	printf("Can't read directory %s\n", 
	       m_path.path);
	ret = FALSE;
	pop_path(&m_path);  /* for PUSH 3 */
	continue;
      }

      if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
	printf("Non directory file %s\n", m_path.path);
	ret = FALSE;
	pop_path(&m_path);  /* for PUSH 3 */
	continue;
      }

      if (def_present && def.tag) {
	/* do not push t_path, it is already pushed above */
	/* validate dir name against definition */
	boolean res = validate_value(&def, uename);
	value_present=TRUE;
	if (!res) {
	  ret = FALSE;
	  /* do not go inside bad directory */
	  pop_path(&m_path);  /* for PUSH 3 */
	  continue;
	}
      } else {
	push_path(&t_path, uename);  /* PUSH 2b the same as PUSH 2a */
	if (lstat(t_path.path, &statbuf) < 0) {
	  printf("No such template directory (%s)\n"
		 "for directory %s", 
		 t_path.path, m_path.path);
	  ret = FALSE;
	  pop_path(&t_path);  /* for PUSH 2b */
	  pop_path(&m_path);  /* for PUSH 3 */
	  continue;
	}
	if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
	  printf("Non directory file %s\n", m_path.path);
	  ret = FALSE;
	  pop_path(&t_path);  /* for PUSH 2b */
	  pop_path(&m_path);  /* for PUSH 3 */
	  continue;
	}
      }

      status = validate_dir_for_commit();
      ret = ret && status;
      pop_path(&m_path);  /* for PUSH 3 */
      if (!def_present || !def.tag)
	pop_path(&t_path);  /* for PUSH 2b */

    }
    status = closedir(dp);
    if (status)
      bye("Cannot close dir %s\n", m_path.path);

    if(!value_present && def_present && !def.tag) {
      ret = ret && validate_value(&def, "");
    }

    if (def_present && def.tag)
      pop_path(&t_path);  /* for PUSH 3a */
    if (def_present)
      free_def(&def);
#ifdef DEBUG
    printf("directory done(node_cnt %d, free_node_cnt %d)\n"
	   "mpath |%s|\n",
	   node_cnt, free_node_cnt, t_path.path);
#endif
    if (uename)
      my_free(uename);
    return ret;
}

/***************************************************
  main:
    main function (for now)
***************************************************/

int main(int argc, char **argv)
{

  boolean  status;
  char *mod;
  struct stat    statbuf;
  int st;
  boolean update_pending = FALSE;
 
  if (initialize_output() == -1) {
    exit(-1);
  }

  set_in_commit(TRUE);
  dump_log( argc, argv);
  init_paths(TRUE);
  mod = my_malloc(strlen(get_mdirp()) + strlen(MOD_NAME)+2, "COMMIT");
  sprintf(mod, "%s/%s", get_mdirp(), MOD_NAME);
  st = lstat(mod, &statbuf);
  my_free(mod);
  if (st < 0 ) {
    fprintf(out_stream, "No configuration changes to commit\n");
    bye("No configuration changes to commit\n");
  }

  if (get_config_lock() == -1) {
    fprintf(out_stream, "Cannot commit: Configuration is locked\n");
    bye("Configuration is locked\n");
  }

  status = validate_dir_for_commit();
  if (status == TRUE) {
    switch_path(CPATH);
    status = commit_delete_children(NULL, FALSE, FALSE);
  }
  if (status == TRUE)
    status = commit_update_children(NULL, FALSE, FALSE, &update_pending);
  fin_commit(status);

  done();

  return (status == TRUE) ? 0 : 1;
}

/*************************************************************
    perform_create_node - 
       remove node and descendent from woring path
       and create a new node
*************************************************************/
static void perform_create_node()
{
#if BITWISE
  static char format[]="rm -f -r %s;mkdir %s";
  char *command;
  switch_path(APATH);
  command = my_malloc(2 * strlen(m_path.path) + sizeof(format),
		      "commit_create");
  sprintf(command, format,  m_path.path, m_path.path);
  system(command);
  my_free(command);
  switch_path(CPATH);
#endif
  return;
}
 

/*************************************************************
    perform_delete_node - 
       delete node in current path
*************************************************************/
static void perform_delete_node()
{
#if BITWISE
  static char format[]="rm -f -r %s";
  char *command;
  command = my_malloc(strlen(m_path.path) + sizeof(format),
		      "commit_delete");
  sprintf(command, format, m_path.path);
  system(command);
  my_free(command);
#endif
  return;
}
static void perform_move()
{
#if BITWISE
  static char format[] = "rm -r -f %s;mkdir %s;mv %s/" VAL_NAME " %s";
  char *a_path;
  char *command;
  switch_path(APATH);
  a_path = my_strdup(m_path.path, "");
  switch_path(CPATH);
  command = my_malloc(sizeof(format)+3*strlen(a_path)+strlen(m_path.path),"");
  sprintf(command, format, a_path, a_path, m_path.path, a_path);
  system(command);
  my_free(command);
  my_free(a_path);
#endif
  return;
}
  
/*************************************************
 commit_update_child:
   preconditions: t_path and c_path set up for parent
   pdefp (IN) - parent definition (may be NULL)
   child (IN) - name of the child
   creating  (IN) - mode of commiting (update or create)
   update_parent (OUT) - unfulfilled update request 
   commit child and its descendants 
   returns TRUE if no errors 
   exit if error during execution
*/
boolean commit_update_child(vtw_def *pdefp, char *child, 
		  boolean creating, boolean in_txn, boolean *update_parent)
{
  boolean        update_pending = FALSE;
  boolean        multi_tag = FALSE;
  struct stat    statbuf;
  int            status;
  vtw_def        def;
  vtw_def       *my_defp; /*def to be given to my children */
  vtw_def       *act_defp;/*def to be used for actions */
  char          *cp;
  vtw_mark       mark;
  vtw_act_type   act;
  boolean        do_end, ok, do_begin = FALSE, do_txn = FALSE;

  set_at_string(NULL);

  ok = TRUE;
#ifdef DEBUG
    printf("commiting directory (node_cnt %d, free_node_cnt %d)\n"
	   "mpath |%s|\n",
	   node_cnt, free_node_cnt, t_path.path);
#endif
    if (strcmp(child, ".") == 0 ||
	strcmp(child, "..") == 0 ||
	strcmp(child, MOD_NAME) == 0 ||
	strcmp(child, opaque_name) == 0)
      return TRUE;
    
    /* deleted subdirectory */
    if (strncmp(child, ".wh.", 4) == 0) { /* whiteout */
      /* ignore */
      return TRUE;
    }
    mark_paths(&mark);
    if (!creating) {
      /* are we marked with opaque */
      push_path(&m_path, child);
      push_path(&m_path, opaque_name);
      if (lstat(m_path.path, &statbuf) >= 0 && !creating) {
	creating = TRUE;
      }
      pop_path(&m_path);
      pop_path(&m_path);
    }
    /* find our definition */
    if (pdefp && pdefp->tag) {
      my_defp = NULL;
      act_defp = pdefp;
      push_path(&t_path,tag_name); 
    } else {
      push_path(&t_path, child);
      push_path(&t_path, def_name);  
      if ((lstat(t_path.path, &statbuf) >= 0) &&
	  ((statbuf.st_mode & S_IFMT) == S_IFREG)) {
	/* defniition present */
	act_defp = my_defp = &def;
	memset(&def, 0, sizeof(def));
#ifdef DEBUG1
	printf("Parsing definition\n");
#endif
	if ((status = parse_def(&def, t_path.path, 
				FALSE)))
	  exit(status);
	my_defp = act_defp = &def;
	if (def.tag)
	  multi_tag = TRUE;
      } else {
	my_defp = act_defp = NULL;
      }
      pop_path(&t_path); /*def_name */
    }
    do_end = FALSE;
    if (!multi_tag && !in_txn && act_defp && 
	(act_defp->actions[begin_act].vtw_list_head ||
	 act_defp->actions[end_act].vtw_list_head)){
      /* we are traversing change directory
	 if we are here, there is a change somewhere */
      do_txn = in_txn = TRUE;
      if (act_defp->actions[end_act].vtw_list_head)
	do_end = TRUE;
      /* if creating, delete skipped this directory,
	 we have to do begin act */
      if (creating && 
	  act_defp->actions[begin_act].vtw_list_head)
	do_begin = TRUE;
    }
    push_path(&m_path, child);
    /* are we a "value" parent node */
    if (act_defp && !act_defp->tag && act_defp->def_type != ERROR_TYPE ) {
      /* we are value node */
      if (!act_defp->multi) {
	/* single node */
	/* create_mode do create,
	   update_mode do create or update */

	if (creating)
	  act = create_act;
	else
	  act = update_act;

	if ((act==update_act) && !act_defp->actions[update_act].vtw_list_head){
	    /* updating but no action 
	       ask parent to do - propagate up */
	  *update_parent = TRUE;
	}
	/* look for actions */
        /* if act != create_act => run actions[act] if not empty */
        /* if act == create_act
         *   if actions[create_act] not empty
         *     run it
         *   else
         *     run actions[update_act] if not empty
         *   run actions[activate_act] if not empty
         */
	if (act_defp->actions[act].vtw_list_head
            || (act == create_act
                && (act_defp->actions[activate_act].vtw_list_head
                    || act_defp->actions[update_act].vtw_list_head))) {
	  status = get_value_to_at_string(&m_path);
	  if (status != VTWERR_OK)
	    bye("Can not read value at %s", m_path.path);
	  /* remove \n at the line end */
	  cp = strchr(get_at_string(), '\n');
	  if (cp) 
	    *cp = 0;
	  if (act_defp->actions[act].vtw_list_head) {
	    status = execute_list(act_defp->actions[act].vtw_list_head,
				  act_defp);
	    if (!status) {
#ifdef DEBUG
	      bye("begin action not NULL for %s\n", get_at_string());
#else
	      return(FALSE);
#endif
	    }
	  } else {
            /* creating but no create action */
            /* try update action */
            if ((act == create_act)
                && act_defp->actions[update_act].vtw_list_head) {
              status
                = execute_list(act_defp->actions[update_act].vtw_list_head,
                               act_defp);
              if (!status) {
                return (FALSE);
              }
            }
          }
          /* try activate action if creating */
          if ((act == create_act)
              && act_defp->actions[activate_act].vtw_list_head) {
            status
              = execute_list(act_defp->actions[activate_act].vtw_list_head,
                             act_defp);
            if (!status) {
              return (FALSE);
            }
          }
	  free_at_string();
	}
	/* now handle commit value */
	if (!in_txn) { /* ELSE WAIT TILL THE END OF TXN */
	  perform_move();
	  perform_delete_node();
	}
	goto restore;
      }
      /* else multi_node */
      cp = NULL;
      status = get_value(&cp, &m_path);
      if (status != VTWERR_OK)
	bye("Can not read value at %s", m_path.path);
      ok = commit_value(act_defp, cp, creating?create_mode:update_mode, in_txn);
      if (cp)
	my_free(cp);
      goto restore;
    }
    /* else not a value */
    /* regular */
    /* do not do anything for tag type multinode */
    if (!multi_tag && creating) {
      set_at_string(child); /* for expand inside actions */
      if (do_begin) {
	status = execute_list(act_defp->
			      actions[begin_act].
			      vtw_list_head, act_defp);
	if (!status) {
#ifdef DEBUG
	  bye("begin action not NULL for %s\n", get_at_string());
#else
	  return(FALSE);
#endif
	}
      }
	
      if (act_defp) {
        if (act_defp->actions[create_act].vtw_list_head) {
          status
            = execute_list(act_defp->actions[create_act].vtw_list_head,
                           act_defp);
          if (!status) {
            return (FALSE);
          }
        } else if (act_defp->actions[update_act].vtw_list_head) {
          /* no create action => use update action */
          status
            = execute_list(act_defp->actions[update_act].vtw_list_head,
                           act_defp);
          if (!status) {
            return (FALSE);
          }
        }
        /* not trying activate action here (activate after children are
         * configured)
         */
      }
    }
    if (creating && !in_txn) /* ELSE WAIT TILL THE END OF TXN */
      perform_create_node();
    /* children */
    ok = commit_update_children(my_defp, creating, in_txn, &update_pending);
    if (!ok)
      return(FALSE);

    if (update_pending){
      if (!multi_tag && act_defp &&
	  act_defp->actions[update_act].vtw_list_head){
	set_at_string(child); /* for expand inside actions */
	ok = execute_list(act_defp->actions[update_act].
			  vtw_list_head, act_defp);
	if (!ok)
	  return(FALSE);
	/* update_pending = FALSE; */
      } else
	*update_parent = TRUE;
    }
    if (creating && !multi_tag && act_defp &&
	act_defp->actions[activate_act].vtw_list_head){
	set_at_string(child); /* for expand inside actions */
	ok = execute_list(act_defp->actions[activate_act].
			  vtw_list_head, act_defp);
	/* ignore result */
    }
    if (!in_txn) /* ELSE WAIT TILL THE END OF TXN */
      perform_delete_node();
 restore:
    if (do_end){
      set_at_string(child);
      ok = execute_list(act_defp->actions[end_act].
		   vtw_list_head, act_defp);
    }
#if BITWISE
    if (do_txn && ok) {
      int len;
      char *command;
      char format1[]="rm -r -f %s/*;cp -r -f %s/%s %s";
      char format2[]="rm -r -f %s/%s;mv %s/%s %s";
      char format3[]="rm -r -f %s/%s";
      restore_paths(&mark);
      switch_path(MPATH);
      len = sizeof(format1) + 2 * strlen(get_tmpp()) + 
	strlen(m_path.path) + strlen(child);
      command = my_malloc(len, "");
      sprintf(command, format1, get_tmpp(), m_path.path, child, 
	      get_tmpp());
      system(command);
      my_free(command);
      switch_path(APATH);
      len = sizeof(format2) + 2 *strlen(m_path.path) +
	2 * strlen( child) + strlen(get_tmpp());
      command = my_malloc(len, "");
      sprintf(command, format2, m_path.path, child, get_tmpp(), 
	      child, m_path.path);
      system(command);
      my_free(command);
      switch_path(CPATH);
      len = sizeof(format3) + strlen(m_path.path) +
	strlen( child);
      command = my_malloc(len, "");
      sprintf(command, format3, m_path.path, child);
      system(command);
      my_free(command);
    }
#endif
    restore_paths(&mark);
    if (my_defp && my_defp != pdefp)
      free_def(my_defp);
    return ok;
}


/*************************************************
 commit_delete_child:
   preconditions: t_path and c_path set up for parent
   pdefp  (IN) - parent definition (may be NULL)
   child  (IN) - name of the child
   do_del (IN) - if FALSE we are looking for delete target
                 if TRUE, we found and switched to A (working) PATH
   commit deleted child and its descendants 
   returns TRUE if no errors 
   exit if error during execution
*/
static boolean commit_delete_child(vtw_def *pdefp,  char *child, 
		  boolean deleting, boolean in_txn)
{
  boolean        do_children, multi_tag = FALSE;
  struct stat    statbuf;
  int            status;
  vtw_def        def;
  vtw_def       *my_defp; /*def to be given to my children */
  vtw_def       *act_defp;/*def to be used for actions */
  char          *cp;
  vtw_mark       mark;
  boolean        ok, do_txn = FALSE;
  int            st;
  ok = TRUE;

#ifdef DEBUG
    printf("commiting directory (node_cnt %d, free_node_cnt %d)\n"
	   "mpath |%s|\n",
	   node_cnt, free_node_cnt, t_path.path);
#endif
    
    /* deleted subdirectory */
    if (strncmp(child, ".wh.", 4) == 0) { /* whiteout */
      if (deleting)
	/* in do_delete mode we traverse A hierarchy, no white-outs possible */
	INTERNAL; /* it is exit */
      else {
	/* deal with counterpart in working */
	switch_path(APATH);
	push_path(&m_path, child+4);
	st = lstat(m_path.path, &statbuf);
	pop_path(&m_path);
	if (st >= 0){
	  /*get rid of ".wh. part in child name"*/
	  /* deleting mode will handle txn, both
	     begin and end 
	  */
	  ok = commit_delete_child(pdefp, child + 4, TRUE, in_txn);
	}else {
	  /* I do not understand how we could be here */
	  printf("Mystery  #1\n"); 
	  ok = TRUE;
	}
	switch_path(CPATH);
	if (ok) {
	  /* delete whiteout */
	  if (!in_txn){ /*ELSE WAIT TILL THE END OF TXN*/
	    push_path(&m_path, child);
	    perform_delete_node();
	    pop_path(&m_path);
	  }
	}
	return ok;
      }
      /* done with whiteouts */
    }
    /* not white out */
    mark_paths(&mark);
    if (!deleting) {
      int status;
      /* are we marked with opaque */
      push_path(&m_path, child);
      push_path(&m_path, opaque_name);
      status = lstat(m_path.path, &statbuf);
      pop_path(&m_path);
      pop_path(&m_path);
      if (status >= 0) {
	/* brand new directory, nothing is 
	   deleted there;
	   update will handle txn (both begin and end)
	*/
	return TRUE;
      }
    }
    /* find our definition */
    if (pdefp && pdefp->tag) {
      /* parent is a tag, node is a tag value node */
      my_defp = NULL;
      act_defp = pdefp;
      push_path(&t_path,tag_name); 
    } else {
      push_path(&t_path, child);
      push_path(&t_path, def_name);  
      if ((lstat(t_path.path, &statbuf) >= 0) &&
	  ((statbuf.st_mode & S_IFMT) == S_IFREG)) {
	/* defniition present */
	act_defp = my_defp = &def;
	memset(&def, 0, sizeof(def));
#ifdef DEBUG1
	printf("Parsing definition\n");
#endif
	if ((status = parse_def(&def, t_path.path, 
				FALSE)))
	  exit(status);
	my_defp = act_defp = &def;
	if (def.tag)
	  multi_tag = TRUE;  /* tag node itself*/ 
      } else {
	my_defp = act_defp = NULL;
      }
      pop_path(&t_path); /*def_name */
    }
    push_path(&m_path, child);
    if (!multi_tag) {
      set_at_string(child); /* for expand inside actions */
      /* deal with txn */
      if (!in_txn && act_defp && 
	  (act_defp->actions[begin_act].vtw_list_head ||
	   act_defp->actions[end_act].vtw_list_head)) {
	/* if we are here we have change,
	   we either in do_del and our node is change node
	   or we are in change directory and our node is 
	   change node also */
	if (deleting) 
	  /* if not deleting, update will handle values */
	  do_txn = TRUE;
	in_txn = TRUE;
	if (act_defp->actions[begin_act].vtw_list_head){
	  status = execute_list(act_defp->actions[begin_act].
				vtw_list_head, act_defp);
	  if (!status) {
#ifdef DEBUG
	    bye("begin action not NULL for %s\n", get_at_string());
#else
	    return(FALSE);
#endif
	  }
	}
      }
    }
    /* are we a "value" parent node */
    if (act_defp && !act_defp->tag && act_defp->def_type != ERROR_TYPE ) {
      /* we are value node */
      if (!act_defp->multi) {
	/* single node */
	if (!deleting) {
	  /* if it was whiteout, it was converted to do_del_mode */
	  restore_paths(&mark);
	  return ok;
	}

	/* do we have actions */
	if (act_defp->actions[delete_act].vtw_list_head){
	  status = get_value_to_at_string(&m_path);
	  if (status != VTWERR_OK)
	    bye("Can not read value at %s", m_path.path);
	  /* remove \n at the line end */
	  cp = strchr(get_at_string(), '\n');
	  if (cp) 
	    *cp = 0;
	  if (act_defp->actions[delete_act].vtw_list_head) {
	    set_in_delete_action(TRUE);
	    status = execute_list(act_defp->actions[delete_act].
				  vtw_list_head, act_defp);
	    set_in_delete_action(FALSE);
	    if (!status) {
#ifdef DEBUG
	      bye("begin action not NULL for %s\n", get_at_string());
#else
	      return(FALSE);
#endif
	    }
	  }
	  free_at_string();
	}
	/* now handle commit value */
	if (!in_txn) /* ELSE WAIT TILL THE END OF TXN */
	  perform_delete_node();
	goto restore;
      }
      /* else multi_node */
      cp = NULL;
      status = get_value(&cp, &m_path);
      if (status != VTWERR_OK)
	bye("Can not read value at %s", m_path.path);
      ok = commit_value(act_defp, cp, deleting?do_del_mode:del_mode,in_txn);
      if (cp)
	my_free(cp);
      goto restore;
    }
    /* else not a value */
    /* regular */
    do_children = TRUE;
    /* do not do anything for tag itself, all action belong to values */
    if (!multi_tag) {
      set_at_string(child); /* for expand inside actions */
      if (deleting) {
	if (act_defp && 
	    act_defp->actions[delete_act].vtw_list_head){
	  do_children = FALSE;
	  set_in_delete_action(TRUE);
	  status = execute_list(act_defp->actions[delete_act].
				vtw_list_head, act_defp);
	  set_in_delete_action(FALSE);
	  if (!status) {
#ifdef DEBUG
	    bye("begin action not NULL for %s\n", get_at_string());
#else
	    return(FALSE);
#endif
	  }
	}
      }
    }
    /* children */
    if (do_children){
      ok = commit_delete_children(my_defp, deleting, in_txn);
      if (!ok)
	goto restore;
    }
    if (deleting) {
      if (do_txn && act_defp && 
	  act_defp->actions[end_act].vtw_list_head) {
	set_at_string(child);
	ok = execute_list(act_defp->actions[end_act].
			      vtw_list_head, act_defp);
	if (!ok)
	  goto restore;
      }
      /* delete node and all its descendants */
      if (!in_txn || do_txn)/* ELSE WAIT TILL THE END OF TXN */
	perform_delete_node();
    }
 restore:
    restore_paths(&mark);
    if (my_defp && my_defp != pdefp)
      free_def(my_defp);
    return ok;
}

static boolean commit_delete_children(vtw_def *defp, boolean deleting, 
				      boolean in_txn)
{
  DIR    *dp;
  int     status;
  struct dirent *dirp;
  boolean ok = TRUE;
  char          *child;
  vtw_type_e     type;
  valstruct      mvals;
  boolean        first;
  char          *cp;
  int            elem, curi;
  vtw_sorted     cur_sorted;
  char          *uename = NULL;

  if ((dp = opendir(m_path.path)) == NULL){
    if (deleting)
      return TRUE;
    INTERNAL;
  }
  if (defp)
    type = defp->def_type;
  else
    type = TEXT_TYPE;
  if (type == ERROR_TYPE)
    type = TEXT_TYPE;
  first = TRUE;
  memset(&mvals, 0, sizeof (valstruct));
  memset(&cur_sorted, 0, sizeof(vtw_sorted));

  while ((dirp = readdir(dp)) != NULL) {
    child = dirp->d_name;
    if (strcmp(child, ".") == 0 ||
	strcmp(child, "..") == 0 ||
	strcmp(child, MOD_NAME) == 0 ||
	strcmp(child, OPQ_NAME) == 0)
      continue;
    uename = clind_unescape(child);
    cp = uename;
    if (first) {
      mvals.free_me = TRUE;
      mvals.val = cp;
      mvals.val_type = type;      
      first = FALSE;
    } else {
      if (mvals.cnt%MULTI_ALLOC == 0) {
	/* convert into multivalue */
	mvals.vals = my_realloc(mvals.vals, 
				(mvals.cnt + MULTI_ALLOC) *
				sizeof(char *), "add_value");
	if (mvals.cnt == 0) { /* single value - convert */
	  mvals.vals[0] = mvals.val;
	  mvals.cnt= 1;
	  mvals.val = NULL;
	}
      }
      mvals.vals[mvals.cnt] = cp;
      ++mvals.cnt;
    }
  }
  status = closedir(dp);
  if (status)
    INTERNAL;
  if (first) {
    return TRUE;
  }
  vtw_sort(&mvals, &cur_sorted);
  for (curi = 0; curi < cur_sorted.num && ok; ++curi){
    if (type == TEXT_TYPE || 
	type == BOOL_TYPE) 
      child = (char *)(cur_sorted.ptrs[curi]);
    else {
      elem = (((unsigned int *)(cur_sorted.ptrs[curi]))-
	      cur_sorted.parts)/
	cur_sorted.partnum;
    child = mvals.cnt?mvals.vals[elem]:
      mvals.val;
    }
    ok = commit_delete_child(defp, child, deleting, in_txn);
  }
  free_val(&mvals);
  free_sorted(&cur_sorted);
  return ok;
}

static boolean commit_update_children(vtw_def *defp, boolean creating, 
			boolean in_txn, boolean *parent_update)
{
  DIR    *dp;
  int     status;
  struct dirent *dirp;
  boolean ok = TRUE;
  char          *child;
  vtw_type_e     type;
  valstruct      mvals;
  boolean        first;
  char          *cp;
  int            elem, curi;
  vtw_sorted     cur_sorted;
  char          *uename = NULL;


  if ((dp = opendir(m_path.path)) == NULL){
    printf("%s:%d: opendir error: path=%s\n",
	   __FUNCTION__,__LINE__,m_path.path);
    INTERNAL;
  }

  memset(&mvals, 0, sizeof (valstruct));
  memset(&cur_sorted, 0, sizeof(vtw_sorted));
  if (defp)
    type = defp->def_type;
  else
    type = TEXT_TYPE;
  if (type == ERROR_TYPE)
    type = TEXT_TYPE;
  first = TRUE;

  while ((dirp = readdir(dp)) != NULL) {
    child = dirp->d_name;
    if (strcmp(child, ".") == 0 ||
	strcmp(child, "..") == 0 ||
	strcmp(child, MOD_NAME) == 0 ||
	strcmp(child, OPQ_NAME) == 0)
      continue;
    cp = uename = clind_unescape(child);
    if (first) {
      mvals.free_me = TRUE;
      mvals.val = cp;
      mvals.val_type = type;      
      first = FALSE;
    } else {
      if (mvals.cnt%MULTI_ALLOC == 0) {
	/* convert into multivalue */
	mvals.vals = my_realloc(mvals.vals, 
				(mvals.cnt + MULTI_ALLOC) *
				sizeof(char *), "add_value");
	if (mvals.cnt == 0) { /* single value - convert */
	  mvals.vals[0] = mvals.val;
	  mvals.cnt= 1;
	  mvals.val = NULL;
	}
      }
      mvals.vals[mvals.cnt] = cp;
      ++mvals.cnt;
    }
  }
  status = closedir(dp);
  if (status)
    INTERNAL;
  if (first) {
    if (uename)
      my_free(uename);
    return TRUE;
  }
  vtw_sort(&mvals, &cur_sorted);
  for (curi = 0; curi < cur_sorted.num && ok; ++curi){
    if (type == TEXT_TYPE || 
	type == BOOL_TYPE) 
      child = (char *)(cur_sorted.ptrs[curi]);
    else {
      elem = (((unsigned int *)(cur_sorted.ptrs[curi]))-
	      cur_sorted.parts)/
	cur_sorted.partnum;
    child = mvals.cnt?mvals.vals[elem]:
      mvals.val;
    }
    ok = commit_update_child(defp, child, creating, in_txn, parent_update);
  }
  free_val(&mvals);
  free_sorted(&cur_sorted);
  return ok;
}
  
  
/*************************************************
 commit_value:
   executes commit for the value leave node 
**************************************************/
static boolean commit_value(vtw_def *defp, char *cp, 
			    vtw_cmode mode, boolean in_txn)
{
  
  valstruct act_value;
  int status;
  int curi,acti, partnum, res=0; 
  void *actp, *curp;
  boolean no_shadow;
  boolean ok;
  int total, a_res, c_res;
  char **a_ptr, **c_ptr, *val_string;
  boolean        cur_pr_val;
  int            pr_index;
  int            sign;
  boolean        creating;
  vtw_node      *actions;
  valstruct  cur_value;
  vtw_sorted     cur_sorted;
  vtw_sorted act_sorted;

  ok = TRUE;
  actions = NULL;
  if(mode == del_mode || mode == do_del_mode) {
    creating = FALSE;
    if (defp && defp->actions[delete_act].vtw_list_head) {
      set_in_delete_action(TRUE);
      actions = defp->actions[delete_act].vtw_list_head;
    }
  } else { 
    creating = TRUE;
    if (defp && defp->actions[create_act].vtw_list_head)
      actions = defp->actions[create_act].vtw_list_head;
  }    
  /* prepare cur_value */

  status = char2val(defp, cp, &cur_value);
  if (mode != do_del_mode && mode != create_mode) {
    /* get active value */
    switch_path(APATH);     /* switch form CCD to ACD */  
    status = get_value(&cp, &m_path);
    switch_path(CPATH);    /* back to CCD */
    if (status != VTWERR_OK) {
      no_shadow = TRUE;
    }else
      no_shadow = FALSE;
  } else {
    no_shadow = TRUE;
  }
  vtw_sort(&cur_value, &cur_sorted);
  if(no_shadow) {
    act_sorted.num = 0;
  }else {
    status = char2val(defp, cp, &act_value);
    if (status != VTWERR_OK) {
      INTERNAL;
    }
    /* sort them */
    vtw_sort(&act_value, &act_sorted);
  }
  if (mode == do_del_mode) {
    /* it was actually act_sorted, not cur_sorted */
    act_sorted = cur_sorted;
    cur_sorted.num = 0;
    act_value = cur_value;
    /* act_value will be freed by freeing cur_value 
       do not zero out it here */
  }
    
  acti = 0;
  curi = 0; 
  total = act_sorted.num + cur_sorted.num;
  a_res=0;
  c_res=0;
  a_ptr = my_malloc(total*sizeof(char *), "");
  c_ptr = my_malloc(total*sizeof(char *), "");
  while (acti < act_sorted.num || curi < cur_sorted.num) {
    if (acti == act_sorted.num) {
      cur_pr_val = TRUE;
      pr_index = curi;
      sign = +1;
      ++curi;
    } else if (curi == cur_sorted.num) {
      cur_pr_val = FALSE;
      pr_index = acti;
      sign = -1;
      ++acti;
    } else {
      /* compare */
      actp = act_sorted.ptrs[acti];
      curp = cur_sorted.ptrs[curi];
      /* compare */
      if (act_sorted.partnum){
	for(partnum = 0; partnum < act_sorted.partnum; 
	    ++partnum) {
	  res = *((int *)actp + partnum) - 
	    *((int *)curp + partnum);
	  if (res)
	    break;
	}
      } else{
	res = strcmp((char *)actp, (char *) curp);
      }
      if (res == 0) {
	/* the same */
	cur_pr_val = TRUE;
	pr_index = curi;
	sign = 0;
	++acti;
	++curi;
      } else if (res < 0) {
	/* act < cur, act is unmatched */
	cur_pr_val = FALSE;
	pr_index = acti;
	sign = -1;
	++acti;
      }else {
	/* cur < act, cur is unmatched */
	cur_pr_val = TRUE;
	pr_index = curi;
	sign = 1;
	++curi;
      }
    }
    if (defp->def_type == TEXT_TYPE || 
	defp->def_type == BOOL_TYPE) {
      val_string = cur_pr_val?
	((char *)(cur_sorted.ptrs[pr_index])):
	((char *)(act_sorted.ptrs[pr_index]));
    } else {
      if (cur_pr_val) {
	int elem = (((unsigned int *)(cur_sorted.ptrs[pr_index]))-
		    cur_sorted.parts)/
	  cur_sorted.partnum;
	val_string = cur_value.cnt?cur_value.vals[elem]:
	  cur_value.val;
      } else {
	int elem = (((unsigned int *)(act_sorted.ptrs[pr_index]))-
		    act_sorted.parts)/
	  act_sorted.partnum;
	val_string = act_value.cnt?act_value.vals[elem]:
	  act_value.val;
      }
    }
    set_at_string(val_string);
    switch (sign) {
    case 0: /* found in both, no actions, include in both */
      a_ptr[a_res++]=val_string;
      c_ptr[c_res++]=val_string;
      break;
    case 1: /* found only in change */
      if (ok && creating) {
	if (actions) {
          /* do create action */
	  ok = execute_list(actions, defp);
        } else if (defp && defp->actions[update_act].vtw_list_head) {
          /* no create action => use update action */
          ok = execute_list(defp->actions[update_act].vtw_list_head, defp);
        }
        if (ok && defp && defp->actions[activate_act].vtw_list_head) {
          /* try activate action */
          ok = execute_list(defp->actions[activate_act].vtw_list_head, defp);
        }
	/* if succ, make it look old */
	if(ok)
	  a_ptr[a_res++]=val_string;
      }
      c_ptr[c_res++]=val_string; /* in all cases */
      break;
    case -1: /* found only in working */
      if (ok && !creating && actions) {/* ok and deleting */
	ok = execute_list(actions, defp);
      }
      /* if succ and deleting - do nothing, else */
      if (!ok || creating)
	a_ptr[a_res++]=val_string;
    }
  }
  if (creating && ok)
    c_res = 0;
#if BITWISE
  if (!in_txn) {/* ELSE WAIT TILL THE END OF TXN */
    switch_path(APATH);
    if (a_res) {
      make_dir();
      push_path(&m_path, VAL_NAME);
      fp = fopen(m_path.path, "w"); 
      if (fp == NULL)
	bye("Can not open value file %s", m_path.path);
      for(i=0;i<a_res;++i)
	if (fputs(a_ptr[i], fp) < 0 || fputc('\n',fp) < 0)
	  bye("Error writing file %s", m_path.path);
      fclose(fp);
      pop_path(&m_path);
    }else{
      perform_delete_node();
    }
    switch_path(CPATH);
    if (c_res) {
      make_dir();
      push_path(&m_path, VAL_NAME);
      fp = fopen(m_path.path, "w"); 
      if (fp == NULL)
	bye("Can not open value file %s", m_path.path);
      for(i=0;i<c_res;++i)
	if (fputs(c_ptr[i], fp) < 0 || fputc('\n',fp) < 0)
	  bye("Error writing file %s", m_path.path);
      fclose(fp);
      pop_path(&m_path);
    }else{
      perform_delete_node();
    }
  }
#endif /*BITWISE*/
  if (mode == do_del_mode)
    switch_path(APATH);
  else
    switch_path(CPATH);
  if(act_sorted.num)
    free_sorted(&act_sorted);
  if(cur_sorted.num)
    free_sorted(&cur_sorted);
  my_free(a_ptr);
  my_free(c_ptr);
  free_val(&cur_value);
  if (!no_shadow)
    free_val(&act_value);
  set_in_delete_action(FALSE);
  return ok;
}

static int fin_commit(boolean ok)
{
  char *command;
  static char format1[]="cp -r -f %s/* %s"; /*mdirp, tmpp*/
  static char format2[]="sudo umount %s"; /*mdirp*/
  static char format3[]="rm -f %s/" MOD_NAME " >&/dev/null ; /bin/true";
                        /*tmpp*/
  static char format4[]="rm -rf %s/{.*,*} >&/dev/null ; /bin/true"; /*cdirp*/
  static char format5[]="rm -rf %s/{.*,*} >&/dev/null ; /bin/true"; /*adirp*/
  static char format6[]="mv -f %s/* -t %s";/*tmpp, adirp*/
  static char format7[]="sudo mount -t unionfs -o dirs=%s=rw:%s=ro" 
    " unionfs %s"; /*cdirp, adirp, mdirp*/
  int m_len = strlen(get_mdirp()); 
  int t_len = strlen(get_tmpp()); 
  int c_len = strlen(get_cdirp()); 
  int a_len = strlen(get_adirp()); 
  set_echo(TRUE);
  if (!ok){
    fprintf(out_stream, "Commit failed\n");
    return -1;
  }
  command = my_malloc(strlen(format1) + m_len + t_len, "");
  sprintf(command, format1, get_mdirp(), get_tmpp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format2) + m_len, "");
  sprintf(command, format2, get_mdirp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format3) + c_len, "");
  sprintf(command, format3, get_tmpp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format4) + c_len, "");
  sprintf(command, format4, get_cdirp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format5) + a_len, "");
  sprintf(command, format5, get_adirp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format6) + t_len + a_len, "");
  sprintf(command, format6, get_tmpp(), get_adirp());
  system(command);
  my_free(command);

  command = my_malloc(strlen(format7) + c_len + a_len + m_len, "");
  sprintf(command, format7, get_cdirp(), get_adirp(), get_mdirp());
  system(command);
  my_free(command);

  return 0;
}  

