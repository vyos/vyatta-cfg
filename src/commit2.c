#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/time.h>
#include <glib-2.0/glib.h>
#include "common/common.h"
#include "cli_path_utils.h"

boolean g_debug = FALSE;
boolean g_display_error_node = FALSE;
boolean g_coverage = FALSE;
boolean g_dump_trans = FALSE;
boolean g_dump_actions = FALSE;

const int ActionOrder[top_act] = {
  4,
  5,
  6,
  0,
  1,
  2,
  3,
  7
};



const char* ActionNames[top_act] = {
  "delete",   //0
  "create",   //1
  "activate", //2
  "update",   //3
  "syntax",   //4
  "commit",   //5
  "begin",    //6
  "end"       //7
};

GNode*
get_transactions(GNode*, boolean priority);

boolean
complete(GSList *node_coll, boolean test_mode);

gboolean
sort_func(GNode *node, gpointer data, boolean priority_mode);

gboolean
sort_func_priority(GNode *node, gpointer data);

gboolean
sort_func_simple(GNode *node, gpointer data);

void
cleanup(GNode *root_node);

gboolean
dump_func(GNode *node, gpointer data);

static gboolean
enclosing_process_func(GNode *node, gpointer data);

static gboolean
process_func(GNode *node, gpointer data);

boolean
process_priority_node(GNode *priority_node);

static gboolean
enclosing_process_func(GNode *node, gpointer data);
/*
NOTES: reverse: use the n-nary tree in commit2.c and only encapuslate data store. pass in func pointer for processing of commands below.

also, the algorithm for collapsing the tree into a transaction list is:
1) iterate through tree and mark all explicit transactions
2) when done, prune the tree of all root explicit transactions
3) Now iterate through remaining tree and remove each node and append to transaction list.
 */

/**
 *
 *
 **/
void 
usage(void)
{
  printf("commit2\n");
  printf("\t-d\t\tdebug mode\n");
  printf("\t-s\t\tdump sorted transactions and exit\n");
  printf("\t-a\t\tdump ordered node actions and exit\n");
  printf("\t-p\t\tdisable priority mode\n");
  printf("\t-t\t\ttest mode (don't apply directory modifications)\n");
  printf("\t-e\t\tprint node where error occurred\n");
  printf("\t-c\t\tdump node coverage and execution times\n");
  printf("\t-o\t\tdisable partial commit\n");
  printf("\t-h\t\thelp\n");
}

/**
 *
 *
 **/
int
main(int argc, char** argv)
{
  int ch;
  boolean priority_mode = TRUE;
  boolean test_mode = FALSE;
  boolean disable_partial_commit = FALSE;

  //grab inputs
  while ((ch = getopt(argc, argv, "dpthsecoa")) != -1) {
    switch (ch) {
    case 'd':
      g_debug = TRUE;
      break;
    case 'h':
      usage();
      exit(0);
      break;
    case 'p':
      priority_mode = FALSE;
      break;
    case 't':
      test_mode = TRUE;
      break;
    case 's':
      g_dump_trans = TRUE;
      break;
    case 'e':
      g_display_error_node = TRUE;
      break;
    case 'c':
      g_coverage = TRUE;
      break;
    case 'o':
      disable_partial_commit = TRUE;
      break;
    case 'a':
      g_dump_actions = TRUE;
      break;
    default:
      usage();
      exit(0);
    }
  }

  initialize_output();
  init_paths(TRUE);
  if (g_debug) {
    printf("commit2: starting up\n");
    syslog(LOG_DEBUG,"commit2: starting up");
  }

  //get local session data plus configuration data
  GNode *config_data = common_get_local_session_data();
  if (g_node_n_children(config_data) == 0) {
    common_commit_clean_temp_config(NULL, test_mode);
    fprintf(out_stream, "No configuration changes to commit\n");
    return 0;
  }
  
  GNode *orig_node_tree = g_node_copy(config_data);

  // Get collection of transactions, i.e. trans nodes that have been activated. 
  GNode *trans_coll = get_transactions(config_data, priority_mode);
  if (trans_coll == NULL) {
    printf("commit2: transactions collection is empty, exiting\n");
    exit(0);
  }

  if (g_debug == TRUE || g_dump_trans == TRUE) {
    if (g_dump_trans == TRUE) {
      fprintf(out_stream,"Dumping transactions\n");
      syslog(LOG_DEBUG,"Dumping transactions");
    }
    //iterate over config_data and dump...
    g_node_traverse(trans_coll,
		    G_PRE_ORDER,
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)dump_func,
		    (gpointer)NULL);
    if (g_dump_trans == TRUE) {
	exit(0);
      }
  }

  GNode *trans_child_node = (GNode*)g_node_first_child(trans_coll);
  if (trans_child_node == NULL) {
    printf("commit2: No child nodes to process, exiting\n");
    exit(0);
  }

  GSList *completed_root_node_coll = NULL;
  boolean no_errors = TRUE;
  set_in_commit(TRUE);
  int i = 0;
  do {
    boolean success = TRUE;
    if (g_debug == TRUE) {
      if (trans_child_node != NULL && trans_child_node->data != NULL && ((struct VyattaNode*)(trans_child_node->data))->_data._name != NULL) {
	printf("commit2: Starting new transaction processing pass on root: %s\n", ((struct VyattaNode*)(trans_child_node->data))->_data._name);
	syslog(LOG_DEBUG,"commit2: Starting new transaction processing pass on root: %s", ((struct VyattaNode*)(trans_child_node->data))->_data._name);
      }
      else {
	printf("commit2: Starting new transaction processing pass on root:\n");
	syslog(LOG_DEBUG,"commit2: Starting new transaction processing pass on root:");
      }
    }

    //complete() now requires a undisturbed copy of the trans_child_node tree
    GNode *comp_cp_node = g_node_copy(trans_child_node);

    if (g_dump_actions == TRUE) {
      fprintf(out_stream,"\n"); //add an extra line here
    }

    //on each priority node now execute actions
    if ((success = process_priority_node(trans_child_node)) == TRUE) {
      //this below copies the node directory from the local to active location
      //if this is true root skip
      if (trans_child_node != NULL && trans_child_node->data != NULL && strcmp(((struct VyattaNode*)(trans_child_node->data))->_data._path,"/") == 0) {
	//no op, need better way to define true root
      }
      else {
	if (disable_partial_commit == FALSE && g_dump_actions == FALSE) {
	  //	  complete(comp_cp_node, test_mode);
	  completed_root_node_coll = g_slist_append(completed_root_node_coll,comp_cp_node);
	}
      }
    }

    if (g_dump_actions == TRUE) {
      success = TRUE; //FORCE SUCCESS ON DISPLAY MODE OF ACTIONS
    }

    if (success == FALSE) {
      no_errors = FALSE;
      if (g_debug == TRUE) {
	printf("commit2: Failed in processing node\n");
	syslog(LOG_DEBUG,"commit2: Failed in processing node\n");
      }
    }
    ++i;
  } while ((trans_child_node = (GNode*)g_node_nth_child((GNode*)trans_coll,(guint)i)) != NULL);

  if (no_errors == TRUE) {
    if (disable_partial_commit == TRUE && g_dump_actions == FALSE) {
      completed_root_node_coll = g_slist_append(completed_root_node_coll,orig_node_tree);
    }
    /*
     * Need to add to the following func below to clean up dangling .wh. files
     */
    if (g_dump_actions == FALSE) {
      complete(completed_root_node_coll, test_mode);
      common_commit_clean_temp_config(orig_node_tree, test_mode);
    }
    if (g_debug == TRUE) {
      printf("commit2: successful commit, now cleaning up temp directories\n");
      syslog(LOG_DEBUG,"commit2: successful commit, now cleaning up temp directories");
    }
  }
  else {
    fprintf(out_stream,"Commit failed\n");
    complete(completed_root_node_coll, test_mode);
  }

  set_in_commit(FALSE);

  cleanup(config_data);
  cleanup(trans_child_node);

  if (g_debug) {
    printf("DONE\n");
    syslog(LOG_DEBUG,"DONE");
  }
  
  exit (no_errors == FALSE);
}

/**
 *
 *
 **/
static gboolean
process_func(GNode *node, gpointer data)
{
  if (node == NULL) {
    return TRUE;
  }

  struct Result *result = (struct Result*)data;
  gpointer gp = ((GNode*)node)->data;
  struct Config *c = &((struct VyattaNode*)gp)->_config;
  struct Data *d = &((struct VyattaNode*)gp)->_data;

  int status = 0;
  if (c->_def.actions  && 
      c->_def.actions[result->_action].vtw_list_head){
    
    if (g_debug) {
      if (d->_name != NULL) {
	printf("commit2::process_func(), calling process on : %s for action %d, type: %d, operation: %d, path: %s\n",d->_name,result->_action,c->_def.def_type, d->_operation, d->_path);
	syslog(LOG_DEBUG,"commit2::process_func(), calling process on : %s for action %d, type: %d, operation: %d, path: %s",d->_name,result->_action,c->_def.def_type, d->_operation, d->_path);
      }
      else {
	printf("commit2::process_func(), calling process on : [n/a] for action %d, operation: %d, path: %s\n",result->_action, d->_operation, d->_path);
	syslog(LOG_DEBUG,"commit2::process_func(), calling process on : [n/a] for action %d, operation: %d, path: %s",result->_action, d->_operation, d->_path);
      }
    }

    /*
      Needs to be cleaned up a bit such that this convoluted if clause is easier to read. Basically
      is says:
      if a node is SET, is not ACTIVE and is not a DELETE ACTION
      or
      if a node is SET, is ACTIVE, is not a DELETE ACTION or a CREATE ACTION
      or
      if a node is DELETE, is a DELETE ACTION or a END ACTION, or a BEGIN ACTION
     */
    if ((IS_SET(d->_operation) && !IS_ACTIVE(d->_operation) && (result->_action != delete_act && result->_action != create_act)) ||
	(IS_CREATE(d->_operation) && !IS_ACTIVE(d->_operation) && (result->_action == syntax_act || result->_action == begin_act || result->_action == end_act || result->_action == create_act || (result->_action == update_act && !c->_def.actions[create_act].vtw_list_head))) ||
	(IS_ACTIVE(d->_operation) && ((result->_action == begin_act) || (result->_action == end_act))) ||
	//	(IS_DELETE(d->_operation) && ((result->_action == delete_act) || (result->_action == syntax_act) || (result->_action == begin_act) || (result->_action == end_act)) )) {
	(IS_DELETE(d->_operation) && ((result->_action == delete_act) || (result->_action == begin_act) || (result->_action == end_act)) )) {
      //NEED TO ADD IF CREATE, THEN CREATE OR UPDATE
      //IF SET THEN UPDATE

      //let's skip the case where this is active and it's a delete--shouldn't be done, but needs to be include in the rule set above
      if (IS_DELETE(d->_operation) && IS_ACTIVE(d->_operation) && result->_action == delete_act) {
	return FALSE;
      }

      //let's skip any multi-node that does not have have a value (an empty multi-node)
      if (c->_multi && node->children == NULL) {
	return FALSE;
      }

      //look at parent for multi tag
      if (d->_value && d->_name) {
	char *val = d->_name;
	if (c->_def.tag) { //need to handle the embedded multinode as a special case--should be fixed!
	  val = (char*)clind_unescape(d->_name);
	}
	if (g_debug) {
	  printf("commit2::process_func(): @ value: %s\n",(char*)val);
	  syslog(LOG_DEBUG,"commit2::process_func(): @ value: %s",(char*)val);
	}
	set_at_string(val); //embedded multinode value
      }
      else {
	if (g_debug) {
	  printf("commit2::process_func(): boolean value is: %d\n",d->_value);
	  syslog(LOG_DEBUG,"commit2::process_func(): boolean value is: %d",d->_value);
	  if (node->parent != NULL && ((struct VyattaNode*)(node->parent->data))->_data._name != NULL) {
	    printf("commit2::process_func(): parent has a name and it is: %s\n",((struct VyattaNode*)(node->parent->data))->_data._name);
	    syslog(LOG_DEBUG,"commit2::process_func(): parent has a name and it is: %s\n",((struct VyattaNode*)(node->parent->data))->_data._name);
	  }
	  printf("commit2::process_func(): @ value: [NULL]\n");
	  syslog(LOG_DEBUG,"commit2::process_func(): @ value: [NULL]\n");
	}
      }
      
      common_set_context(c->_path,d->_path);
      if (g_debug) {
	printf("Executing %s on this node\n", ActionNames[result->_action]);
	syslog(LOG_DEBUG,"Executing %s on this node\n", ActionNames[result->_action]);
      }

      if (g_coverage) {
	struct timeval t;
	gettimeofday(&t,NULL);
	fprintf(out_stream,"[START] %lu, %s@%s",(unsigned long)t.tv_sec,ActionNames[result->_action],d->_path);
      }


      if (result->_action == delete_act) {
	set_in_delete_action(TRUE);
      }

      //do last sibling check
      GNode *n = g_node_last_sibling(node);
      if (n == node) {
	setenv(ENV_SIBLING_POSITION,"LAST",1);
      }
      n = g_node_first_sibling(node);
      if (n == node) {
	setenv(ENV_SIBLING_POSITION,"FIRST",1);
      }

      //do not set for promoted actions
      if (!IS_ACTIVE(d->_operation)) {
	if (IS_DELETE(d->_operation)) {
	    setenv(ENV_ACTION_NAME,ENV_ACTION_DELETE,1);
	}
	else {
	  setenv(ENV_ACTION_NAME,ENV_ACTION_SET,1);
	}
      }
      else {
	  setenv(ENV_ACTION_NAME,ENV_ACTION_ACTIVE,1);
      }

      if (g_dump_actions == FALSE) {
	status = execute_list(c->_def.actions[result->_action].vtw_list_head,&c->_def);
      }
      else {
	if (c->_def.actions[syntax_act].vtw_list_head &&
	    c->_def.actions[syntax_act].vtw_list_head->vtw_node_aux == 1) {
	  fprintf(out_stream,"commit\t:\t%s\n",d->_path);
	}
	else {
	  fprintf(out_stream,"%s\t:\t%s\n",ActionNames[result->_action],d->_path);
	}
	status = 1;
      }
      if (result->_action == delete_act) {
	set_in_delete_action(FALSE);
      }

      unsetenv(ENV_ACTION_NAME);
      unsetenv(ENV_SIBLING_POSITION);

      if (g_coverage) {
	struct timeval t;
	gettimeofday(&t,NULL);
	fprintf(out_stream,"[END] %lu\n",t.tv_sec);
      }

      if (!status) { //EXECUTE_LIST RETURNS FALSE ON FAILURE....
	syslog(LOG_ERR,"commit error for %s:[%s]\n",ActionNames[result->_action],d->_path);
	if (g_display_error_node) {
	  fprintf(out_stream,"%s:[%s]\n",ActionNames[result->_action],d->_path);
	}
	result->_err_code = 1;
	if (g_debug) {
	  printf("commit2::process_func(): FAILURE: status: %d\n",status);
	  syslog(LOG_DEBUG,"commit2::process_func(): FAILURE: status: %d",status);
	}
	return TRUE; //WILL STOP AT THIS POINT
      }
    }
  }
  return FALSE;
}


/**
 *
 **/
boolean
complete(GSList *node_coll, boolean test_mode)
{
  GSList *l;
  for (l = node_coll; l; l = g_slist_next (l)) {
    gpointer gp = ((GNode*)l->data)->data;
    if (g_debug) {
      if (((struct VyattaNode*)gp)->_data._name != NULL) {
	printf("commit2::complete():name: %s, path: %s\n",((struct VyattaNode*)gp)->_data._name,((struct VyattaNode*)gp)->_data._path);
	syslog(LOG_DEBUG,"commit2::complete():name: %s, path: %s",((struct VyattaNode*)gp)->_data._name,((struct VyattaNode*)gp)->_data._path);
      }
      else {
	printf("commit2::complete()\n");
	syslog(LOG_DEBUG,"commit2::complete()");
      }
    }
    //on transactional nodes only, note to avoid calling this if a headless root
    common_commit_copy_to_live_config(l->data, test_mode);
  }
  return TRUE;
}


/**
 *
 *
 **/
gboolean
sort_func_priority(GNode *node, gpointer data)
{
  return sort_func(node,data,TRUE);
}

/**
 *
 *
 **/
gboolean
sort_func_simple(GNode *node, gpointer data)
{
  return sort_func(node,data,FALSE);
}



/**
 *
 *
 **/
gboolean
sort_func(GNode *node, gpointer data, boolean priority_mode)
{
  gpointer gp = ((GNode*)node)->data;
  GNode *root_node = (GNode*)data;
  if (g_debug) {
    if (((struct VyattaNode*)gp)->_data._name != NULL) {
      printf("commit2::sort_func(): %s, node count: %d\n",((struct VyattaNode*)gp)->_data._name,g_node_n_children(root_node));
      syslog(LOG_DEBUG,"commit2::sort_func(): %s, node count: %d",((struct VyattaNode*)gp)->_data._name,g_node_n_children(root_node));
    }
    else {
      printf("commit2::sort_func(): [n/a], node count: %d\n",g_node_n_children(root_node));
      syslog(LOG_DEBUG,"commit2::sort_func(): [n/a], node count: %d",g_node_n_children(root_node));
    }
  }

  //change action state of node according to enclosing behavior
  if ((G_NODE_IS_ROOT(node) == FALSE) &&
      ((IS_SET_OR_CREATE(((struct VyattaNode*)gp)->_data._operation))  || 
       (IS_DELETE(((struct VyattaNode*)gp)->_data._operation))) && 
      (IS_NOOP(((struct VyattaNode*)(node->parent->data))->_data._operation))) {

    //first check if there is enclosing behavior
    boolean enclosing = FALSE;
    GNode *n = node;
    while (TRUE) {
      n = n->parent;
      vtw_def def = ((struct VyattaNode*)(n->data))->_config._def;
      if (def.actions[end_act].vtw_list_head || def.actions[begin_act].vtw_list_head ||
	  (def.actions[syntax_act].vtw_list_head && def.actions[syntax_act].vtw_list_head->vtw_node_aux == 1)) {
	enclosing = TRUE;
	break;
      }
      if (G_NODE_IS_ROOT(n) == TRUE) {
	break;
      }
    }

    //walk back up and flip operations until enclosing behavior
    if (enclosing == TRUE) {
      GNode *n = node;
      while (TRUE) {
	n = n->parent;
	vtw_def def = ((struct VyattaNode*)(n->data))->_config._def;
	((struct VyattaNode*)(n->data))->_data._operation = ((struct VyattaNode*)gp)->_data._operation | K_ACTIVE_OP;
	if (def.actions[end_act].vtw_list_head || def.actions[begin_act].vtw_list_head || 
	    (def.actions[syntax_act].vtw_list_head && def.actions[syntax_act].vtw_list_head->vtw_node_aux == 1)) {
	  break;
	}
	if (G_NODE_IS_ROOT(n) == TRUE) {
	  break;
	}
      }
    }
  }

  if (priority_mode) {
    if (((struct VyattaNode*)gp)->_priority < LOWEST_PRIORITY) { //only if priority is specified.
      //unlink from original tree
      g_node_unlink(node);

      GNode *new_node = g_node_copy(node);
      GNode *sibling = root_node->children;
      //now iterate through siblings of root_node and compare priority
      
      while (sibling != NULL && ((struct VyattaNode*)gp)->_priority > ((struct VyattaNode*)(sibling->data))->_priority) {
	sibling = sibling->next;
	if (sibling == NULL || ((struct VyattaNode*)gp)->_priority < ((struct VyattaNode*)(sibling->data))->_priority) {
	  break;
	}
      }

      if (g_debug) {
	int pri = LOWEST_PRIORITY;
	if (sibling != NULL) {
	  pri = ((struct VyattaNode*)(sibling->data))->_priority;
	}
	printf("commit2::sort_func(): inserting %s into transaction, priority: %d BEFORE %d\n", ((struct VyattaNode*)gp)->_data._name, ((struct VyattaNode*)gp)->_priority, pri);
	syslog(LOG_DEBUG,"commit2::sort_func(): inserting %s into transaction, priority: %d BEFORE %d\n", ((struct VyattaNode*)gp)->_data._name, ((struct VyattaNode*)gp)->_priority, pri);
      }
      g_node_insert_before(root_node,sibling,new_node);
    }
  }
  else {
    if (g_node_depth(node) == 2) {
      if (g_debug) {
	printf("commit2::sort_func(): inserting %s into transaction\n", ((struct VyattaNode*)gp)->_data._name);
	syslog(LOG_DEBUG,"commit2::sort_func(): inserting %s into transaction\n", ((struct VyattaNode*)gp)->_data._name);
      }
      GNode *new_node = g_node_copy(node);
      g_node_insert(root_node,-1,new_node); //make a flat structure for now
    }
  }
  return FALSE;
}


/**
 * Gets a flat collection of nodes, sorted by priority
 *
 *
 **/
GNode*
get_transactions(GNode *config, boolean priority_mode)
{
  if (g_debug) {
    printf("commit2::get_transactions()\n");
    syslog(LOG_DEBUG,"commit2::get_transactions()\n");
  }

  if (config == NULL) {
    return NULL;
  }

  gpointer gp = ((GNode*)config)->data;

  GNode *trans_root = g_node_new(gp);
  if (priority_mode) {
    g_node_traverse(config,
		    G_POST_ORDER,
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)sort_func_priority,
		    (gpointer)trans_root);

    //only do this if the root isn't empty
    if (g_node_n_children(config) != 0) {
      g_node_insert(trans_root,-1,config); //add what's left
    }
  }
  else {
    g_node_traverse(config,
		    G_IN_ORDER,
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)sort_func_simple,
		    (gpointer)trans_root);
  }
  return trans_root;
}


/**
 *
 *
 **/
static gboolean
cleanup_func(GNode *node, gpointer data)
{
  struct VyattaNode *vn = ((struct VyattaNode*)(node->data));
  if (vn->_data._name) {
    free(vn->_data._name);
  }
  if (vn->_data._path) {
    free(vn->_data._path);
  }
  if (vn->_config._help) {
    free(vn->_config._help);
  }
  if (vn->_config._default) {
    free(vn->_config._default);
  }
  if (vn->_config._path) {
    free(vn->_config._path);
  }
  return FALSE;
}

/**
 *
 *
 **/
void
cleanup(GNode *root_node) 
{
  if (root_node == NULL) {
    return;
  }

  g_node_traverse(root_node,
		  G_IN_ORDER,
		  G_TRAVERSE_ALL,
		  -1,
		  (GNodeTraverseFunc)cleanup_func,
		  (gpointer)NULL);

  g_node_destroy(root_node);
}

/**
 *
 **/
gboolean
dump_func(GNode *node, gpointer data)
{
  FILE *out;
  if (g_dump_trans) {
    out = out_stream;
  }
  else {
    out = stdout;
  }

  if (node != NULL) {
    guint depth = g_node_depth(node);

    if (depth == 2) {
      fprintf(out,"NEW TRANS\n");
    }

    gpointer gp = ((GNode*)node)->data;
    if (((struct VyattaNode*)gp)->_data._name != NULL) {
      int i;

      if (IS_ACTIVE(((struct VyattaNode*)gp)->_data._operation)) {
	fprintf(out,"*");
      }
      else if (IS_DELETE(((struct VyattaNode*)gp)->_data._operation)) {
	fprintf(out,"-");
      }
      else if (IS_CREATE(((struct VyattaNode*)gp)->_data._operation)) {
	fprintf(out,"+");
      }
      else if (IS_SET(((struct VyattaNode*)gp)->_data._operation)) {
	fprintf(out,">");
      }
      else {
	fprintf(out," ");
      }
      for (i = 0; i < depth; ++i) {
	fprintf(out,"  ");
      }
      fprintf(out,"%s (t: %d, p: %d)", ((struct VyattaNode*)gp)->_data._name,((struct VyattaNode*)gp)->_config._def.def_type,((struct VyattaNode*)gp)->_priority);
      if (((struct VyattaNode*)gp)->_data._value == TRUE) {
	fprintf(out," [VALUE]");
      }
      if (((struct VyattaNode*)gp)->_config._multi == TRUE) {
	fprintf(out," [MULTI]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head &&
	  ((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head->vtw_node_aux == 0) {
	fprintf(out," [SYNTAX]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[create_act].vtw_list_head) {
	fprintf(out," [CREATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[activate_act].vtw_list_head) {
	fprintf(out," [ACTIVATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[update_act].vtw_list_head) {
	fprintf(out," [UPDATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[delete_act].vtw_list_head) {
	fprintf(out," [DELETE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head &&
	  ((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head->vtw_node_aux == 1) {
	fprintf(out," [COMMIT]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[begin_act].vtw_list_head) {
	fprintf(out," [BEGIN]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[end_act].vtw_list_head) {
	fprintf(out," [END]");
      }
      if (((struct VyattaNode*)gp)->_config._help != NULL) {
	//	fprintf(out,"[help: %s]",((struct VyattaNode*)gp)->_config._help);
      }
      fprintf(out,"\n");
    }
    
  }
  return FALSE;
}


/**
 *
 **/
boolean
process_priority_node(GNode *priority_node)
{
  //on each node that is deleted run the delete action within the context of the transaction
  struct Result result;
  result._err_code = 0;

  if (priority_node == NULL) {
    return FALSE;
  }

  //if this node is an enclosing node, we'll skip this iteration
  gpointer gp = ((GNode*)priority_node)->data;
  struct Config *c = &((struct VyattaNode*)gp)->_config;
  //does this node contain a begin or end statement?
  boolean priority_node_is_enclosing_node = FALSE;
  if (c->_def.actions  && 
      (c->_def.actions[end_act].vtw_list_head || c->_def.actions[begin_act].vtw_list_head)){
    priority_node_is_enclosing_node = TRUE;
  }

  if (priority_node_is_enclosing_node == FALSE) {
    //traverse priority node from children up
    g_node_traverse((GNode*)priority_node,
		    G_POST_ORDER,
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)enclosing_process_func,
		    (gpointer)&result);
    
    if (result._err_code != 0) {
      return FALSE;
    }
  }
  //now perform processing on what's left outside of the enclosing begin/end statements
  int i;
  for (i = 0; i < top_act; ++i) {
    int order;
    if (delete_act != ActionOrder[i]) {
      order = G_PRE_ORDER;
    }
    else {
      order = G_POST_ORDER;
    }
    
    result._action = ActionOrder[i];
    g_node_traverse((GNode*)priority_node,
		    order,
		    G_TRAVERSE_ALL,
		    -1,
		    (GNodeTraverseFunc)process_func,
		    (gpointer)&result);

    if (result._err_code != 0) {
      if (g_debug) {
	printf("commit2::process_priority_node(): failure on processing pass: %d\n", i);
	syslog(LOG_DEBUG,"commit2::process_priority_node(): failure on processing pass: %d\n", i);
      }
      return FALSE;
    }
  }
  return TRUE;
}


/**
 * Look for begin/end statements to begin processing
 * of actions.
 **/
static gboolean
enclosing_process_func(GNode *node, gpointer data)
{
  if (node == NULL) {
    return TRUE;
  }

  struct Result *result = (struct Result*)data;
  gpointer gp = ((GNode*)node)->data;
  struct Config *c = &((struct VyattaNode*)gp)->_config;
  struct Data *d = &((struct VyattaNode*)gp)->_data;

  //does this node contain a begin or end statement?
  if (c->_def.actions  && 
      (c->_def.actions[end_act].vtw_list_head || c->_def.actions[begin_act].vtw_list_head)){
    //gotten to this point need to do a call around this enclosing being/end node
    g_node_unlink(node); //removed this...

    if (g_debug) {
      printf("commit2::enclosing_process_func(): enclosing statement found on: %s\n",d->_path);
      syslog(LOG_DEBUG,"commit2::enclosing_process_func(): enclosing statement found on: %s\n",d->_path);
    }
    //perform recursive calling on new process node...

    int i;
    for (i = 0; i < top_act; ++i) {
      int order;
      if (delete_act != ActionOrder[i]) {
	order = G_PRE_ORDER;
      }
      else {
	order = G_POST_ORDER;
      }

      result->_action = ActionOrder[i];
      g_node_traverse((GNode*)node,
		      order,
		      G_TRAVERSE_ALL,
		      -1,
		      (GNodeTraverseFunc)process_func,
		      (gpointer)result);
    
      if (result->_err_code != 0) { //EXECUTE_LIST RETURNS FALSE ON FAILURE....
	if (g_debug) {
	  printf("commit2::enclosing_process_func(): FAILURE: status: %d\n",result->_err_code);
	  syslog(LOG_DEBUG,"commit2::enclosing_process_func(): FAILURE: status: %d\n",result->_err_code);
	}
	return TRUE; //WILL STOP AT THIS POINT
      }
    }
  }
  return FALSE;
}


