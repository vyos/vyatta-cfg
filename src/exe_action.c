#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib-2.0/glib.h>
#include "common/common.h"

boolean g_debug = FALSE;

/**
 *
 *
 **/
void 
usage()
{
  printf("exe_action\n");
  printf("-p\t\trelative path to node.def\n");
  printf("-a\t\taction (integer value)\n");
  printf("-d\t\tdebug mode\n");
  printf("-h\t\thelp\n");
}


/**
 *
 **/
const char* get_tmpp(void) {

  const char* tmpp=getenv(ENV_T_DIR);

  if(!tmpp)
    tmpp="";

  return tmpp;
}


/**
 *
 *
 **/
int
main(int argc, char** argv)
{
  int ch;
  char *path = NULL;
  unsigned long act = 0;

  /* this is needed before calling certain glib functions */
  g_type_init();

  //grab inputs
  while ((ch = getopt(argc, argv, "dhp:a:")) != -1) {
    switch (ch) {
    case 'd':
      g_debug = TRUE;
      break;
    case 'h':
      usage();
      exit(0);
      break;
    case 'p':
      path = optarg;
      break;
    case 'a':
      act = strtoul(optarg,NULL,10);
      break;
    default:
      usage();
      exit(0);
    }
  }


  vtw_def def;
  struct stat s;
  const char *root = get_tmpp();
  char buf[2048];
  sprintf(buf,"%s/%snode.def",root,path);
  printf("%s\n",buf);
  initialize_output();
  init_paths(TRUE);

  printf("[path: %s][act: %lu]\n",buf,act);

  if ((lstat(buf,&s) == 0) && S_ISREG(s.st_mode)) {
    if (parse_def(&def,buf,FALSE) == 0) {
      //execute
      int status;
      if (def.actions[act].vtw_list_head) {
	set_in_commit(TRUE);

	char foo[1024];
	sprintf(foo,"b");
	set_at_string(foo);

	//BROKEN--NEEDS TO BE FIX BELOW FOR DPATH AND CPATH
	common_set_context(path,path);

	status = execute_list(def.actions[act].vtw_list_head,&def);
	if (status == FALSE) {
	  printf("command failed! status: %d\n", status);
	}
	else {
	  printf("SUCCESS!\n");
	}
	set_in_commit(FALSE);
      }
      else {
	printf("no action for this node\n");
      }
    }
    else {
      printf("failure to parse defination file\n");
    }
  }
  else {
    printf("node.def not found at: %s\n",buf);
  }
  return 0;
}
