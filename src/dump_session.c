#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib-2.0/glib.h>
#include "common/common.h"

boolean g_debug = FALSE;
boolean g_verbose = FALSE;

static gboolean
dump_func(GNode *node, gpointer data);

extern GNode*
common_get_local_session_data();

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
usage()
{
  printf("dump_session\n");
  printf("-d\t\tdebug mode\n");
  printf("-v\t\tverbose mode\n");
  printf("-h\t\thelp\n");
}

/**
 *
 *
 **/
int
main(int argc, char** argv)
{
  int ch;

  /* this is needed before calling certain glib functions */
  g_type_init();

  //grab inputs
  while ((ch = getopt(argc, argv, "dvh")) != -1) {
    switch (ch) {
    case 'd':
      g_debug = TRUE;
      break;
    case 'v':
      g_verbose = TRUE;
      break;
    case 'h':
      usage();
      exit(0);
      break;
    default:
      usage();
      exit(0);
    }
  }

  //get local session data plus configuration data
  GNode *config_root_node = common_get_local_session_data();
  if (config_root_node == NULL) {
    exit(0);
  }
  
  printf("Starting dump\n");

  //iterate over config_data and dump...
  g_node_traverse(config_root_node,
		  G_PRE_ORDER,
		  G_TRAVERSE_ALL,
		  -1,
		  (GNodeTraverseFunc)dump_func,
		  (gpointer)NULL);

  return 0;
 }


static gboolean
dump_func(GNode *node, gpointer data)
{
  if (node != NULL) {
    guint depth = g_node_depth(node);

    gpointer gp = ((GNode*)node)->data;
    if (((struct VyattaNode*)gp)->_data._name != NULL) {
      int i;

      if (IS_DELETE(((struct VyattaNode*)gp)->_data._operation)) {
	printf("-");
      }
      else if (IS_CREATE(((struct VyattaNode*)gp)->_data._operation)) {
	printf("+");
      }
      else if (IS_SET(((struct VyattaNode*)gp)->_data._operation)) {
	printf(">");
      }
      else {
	printf(" ");
      }
      for (i = 0; i < depth; ++i) {
	printf("  ");
      }
      printf("%s (t: %d, p: %d)", ((struct VyattaNode*)gp)->_data._name,((struct VyattaNode*)gp)->_config._def.def_type,((struct VyattaNode*)gp)->_priority);
      if (((struct VyattaNode*)gp)->_data._value == TRUE) {
	printf(" [VALUE]");
      }
      if (((struct VyattaNode*)gp)->_config._multi == TRUE) {
	printf(" [MULTI]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head &&
	  ((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head->vtw_node_aux == 0) {
	printf(" [SYNTAX]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[create_act].vtw_list_head) {
	printf(" [CREATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[activate_act].vtw_list_head) {
	printf(" [ACTIVATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[update_act].vtw_list_head) {
	printf(" [UPDATE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[delete_act].vtw_list_head) {
	printf(" [DELETE]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head &&
	  ((struct VyattaNode*)gp)->_config._def.actions[syntax_act].vtw_list_head->vtw_node_aux == 1) {
	printf(" [COMMIT]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[begin_act].vtw_list_head) {
	printf(" [BEGIN]");
      }
      if (((struct VyattaNode*)gp)->_config._def.actions[end_act].vtw_list_head) {
	printf(" [END]");
      }

      if (g_verbose == TRUE) {
	printf("\n");
	for (i = 0; i < depth; ++i) {
	  printf("  ");
	}
	printf("%s\n",((struct VyattaNode*)gp)->_config._path);
	for (i = 0; i < depth; ++i) {
	  printf("  ");
	}
	printf("%s\n",((struct VyattaNode*)gp)->_data._path);
      }

      if (((struct VyattaNode*)gp)->_config._help != NULL) {
	//	printf("[help: %s]",((struct VyattaNode*)gp)->_config._help);
      }
      printf("\n");
    }
    
  }
  return FALSE;
}
