#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <dirent.h>
#include <sys/time.h>
#include <string.h>
#include <glib-object.h>  /* g_type_init */

void recurse(char *cur_dir,FILE *out);

/**
 *
 *
 **/
void
usage(void)
{
  printf("priority: recurses templates and generates priority file\n");
  printf("\t-h\thelp\n");
  printf("\t-f\toutput file\n");
}


/**
 *
 *
 **/
int
main(int argc, char** argv)
{
  int ch;
  char *filename = NULL;

  /* this is needed before calling certain glib functions */
  g_type_init();

  //grab inputs
  while ((ch = getopt(argc, argv, "hf:")) != -1) {
    switch (ch) {
    case 'h':
      usage();
      exit(0);
    case 'f':
      filename = optarg;
      //GET OUT FILE HERE
    }

    if (filename == NULL) {
        strcpy(filename,"priority");
    }

    FILE *fp = fopen(filename,"w");
    if (fp == NULL) {
      printf("cannot open priority file. exiting...\n");
    }
    
    char root_dir[2048] = "";
    recurse(root_dir,fp);
    fclose(fp);
  }
  return 0;
}


/**
 * On each priority node write out location and value and continue recursion
 *
 **/
void
recurse(char *cur_dir,FILE *out)
{
  char root_path[] = "/opt/vyatta/share/vyatta-cfg/templates";
  char str[2048];
  //open and scan node.def

  char file[2048];
  sprintf(file,"%s/%s/node.def",root_path,cur_dir);
  FILE *fp = fopen(file,"r");
  //  printf("found node.def at: %s\n",file);

  if (fp != NULL) {
    while (fgets(str, 1024, fp) != 0) {
      if (strncmp("priority:",str,9) == 0) {
	//retrieve value and write out...

	const char delimiters[] = " ";
	char *running;
	char *token;

	running = strdup(str);
	token = strsep(&running, delimiters);    
	token = strsep(&running, delimiters);    

	unsigned long val = strtoul(token,NULL,10);
	if (val > 0 && val <= 1000) {
	  fwrite(token,1,strlen(token)-1,out);
	  fwrite(" ",1,1,out);

	  //remove fixed path
	  //offset by 1 to remove the leading slash
	  fwrite(cur_dir+1,1,strlen(cur_dir)-1,out);
	  fwrite("\n",1,1,out);
	}
	break;
      }
    }    
    fclose(fp);
  }


  //now recurse the other directories here.
  //iterate over directory here

  char path[2048];
  sprintf(path,"%s/%s",root_path,cur_dir);
  DIR *dp;
  if ((dp = opendir(path)) == NULL) {
    return;
  }

  //finally iterate over valid child directory entries
  struct dirent *dirp = NULL;
  while ((dirp = readdir(dp)) != NULL) {
    if (strcmp(dirp->d_name, ".") != 0 && 
	strcmp(dirp->d_name, "..") != 0 &&
	strcmp(dirp->d_name, "node.def") != 0) {
      char local_dir[2048];
      strcpy(local_dir,cur_dir);
      strcat(local_dir,"/");
      strcat(local_dir,dirp->d_name);
      recurse(local_dir,out);
    }
  }  
  closedir(dp);
}
