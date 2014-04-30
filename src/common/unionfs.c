#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <glib-2.0/glib.h>
#include <gio/gio.h>
#include "common/defs.h"
#include "common/unionfs.h"

#include <cstore/cstore-c.h>

boolean g_debug;

extern vtw_path m_path;
extern vtw_path t_path;

pid_t pid;
int status;
int commpipe[2];

/*** functions for filesystem operations ***/
/* these functions replace the system() invocations that were a major source
 * of commit overhead. note that they currently duplicate the previous
 * behavior so no status is returned (since system() return code was not
 * checked). this should probably be changed so each invocation is checked
 * for error.
 *
 * XXX these (and many other things here) will need to be moved into the
 *     library to consolidate the usage of all low-level implementation
 *     details.
 */
static inline void
sys_mkdir_p(const char *path)
{
  if (g_mkdir_with_parents(path, 0775) != 0) {
    /* error */
    return;
  }
}

static inline void
sys_rm(const char *file)
{
  GFile *target = g_file_new_for_path(file);
  if (!g_file_delete(target, NULL, NULL)) {
    /* error */
    return;
  }
}

static inline void
sys_cp(const char *src_file, const char *dst_file)
{
  GFile *src = g_file_new_for_path(src_file);
  GFile *dst = g_file_new_for_path(dst_file);
  if (!g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL)) {
    /* error */
    return;
  }
}

static inline void
sys_umount_session(void)
{
#ifdef USE_UNIONFSFUSE
  const char *fusermount_path, *fusermount_prog;
  const char *fusermount_umount;

  fusermount_path = "/usr/bin/fusermount";
  fusermount_prog = "fusermount";
  fusermount_umount = "-u";

  if(pipe(commpipe)){
    fprintf(stderr,"Pipe error!\n");
    perror("pipe");
  }

  if((pid = fork()) == -1) {
    perror("pid");
  }

  if(pid) {
    dup2(commpipe[1],1);
    close(commpipe[0]);
    setvbuf(stdout,(char*)NULL,_IONBF,0);
    wait(&status);
  }
  else {
    dup2(commpipe[0],0);
    close(commpipe[1]);
    if (execl(fusermount_path, fusermount_prog, fusermount_umount, get_mdirp(), NULL) != 0) {
      perror("execl");
    }
  }
#else
  if (umount(get_mdirp()) != 0) {
    perror("umount");
  }
#endif
}

static inline void
sys_mount_session(void)
{
#ifdef USE_UNIONFSFUSE
  char mopts[MAX_LENGTH_DIR_PATH * 2];
  const char *fusepath, *fuseprog;
  const char *fuseoptinit;
  const char *fuseopt1, *fuseopt2;
  const char *moptfmt;

  fusepath = "/usr/bin/unionfs-fuse";
  fuseprog = "unionfs-fuse";
  fuseoptinit = "-o";
  fuseopt1 = "cow";
  fuseopt2 = "allow_other";
  moptfmt = "%s=RW:%s=RO";

  if(pipe(commpipe)){
    fprintf(stderr,"Pipe error!\n");
    perror("pipe");
  }

  if((pid = fork()) == -1) {
    perror("pid");
  }

  if(pid) {
    dup2(commpipe[1],1);
    close(commpipe[0]);
    setvbuf(stdout,(char*)NULL,_IONBF,0);
    wait(&status);
  }
  else {
    dup2(commpipe[0],0);
    close(commpipe[1]);
    snprintf(mopts, MAX_LENGTH_DIR_PATH * 2, moptfmt,
             get_cdirp(), get_adirp());
    if (execl(fusepath, fuseprog, fuseoptinit, fuseopt1, fuseoptinit, fuseopt2, mopts, get_mdirp(), NULL) != 0) {
      perror("execl");
    }
  }
#else
  char mopts[MAX_LENGTH_DIR_PATH * 2];
  snprintf(mopts, MAX_LENGTH_DIR_PATH * 2, "dirs=%s=rw:%s=ro",
           get_cdirp(), get_adirp());
  if (mount("unionfs", get_mdirp(), "unionfs", 0, mopts) != 0) {
    perror("mount");
  }
#endif
}

void
set_path(char *path, boolean config);

static struct VyattaNode*
copy_vyatta_node(struct VyattaNode* vn);

static void
get_term_data_values(GNode *node);

static void
dlist_test_func(GQuark key_id,gpointer data,gpointer user_data);

static GNode*
insert_sibling_in_order(GNode *parent, GNode *child);

static gboolean
copy_func(GNode *node, gpointer data);

static gboolean
delete_func(GNode *node, gpointer data);

static gboolean
delete_wh_func(GNode *node, gpointer data);

static void
piecewise_copy(GNode *root_node, boolean test_mode);

/**
 * Data is stored on the path:
 *   <newcfgroot>/system/login/user/foo/authentication/plaintext-password
 *
 * Template is stored along this path:
 *   <tmplroot>/system/login/user/node.tag/authentication/plaintext-password
 *
 * 1) Need to split out relative path
 * 2) if node is *MULTI* then the path value is the actual value
 * 3) For the config copy the pointer from an existing config value for each
 *    multinode <Need to figure out how this is going to happen>
 *
 * This should allow a combined data/config tree
 **/
static char *
get_config_path(GNode *node)
{
  if (node == NULL) {
    return NULL;
  }

  char buf[MAX_LENGTH_DIR_PATH];
  buf[0] = '/';
  buf[1] = '\0';

  GNode *n = node;
  while (G_NODE_IS_ROOT(n) != TRUE) {
    struct VyattaNode *d = (struct VyattaNode*)n->data;
    if (d == NULL) {
      if (g_debug) {
        printf("unionfs::get_config_path(): data ptr is null\n");
        syslog(LOG_DEBUG,"unionfs::get_config_path(): data ptr is null");
      }
      return NULL;
    }

    //if not found, mark current node as multinode
    if (d->_data._name != NULL) {
      char tmp[MAX_LENGTH_DIR_PATH];
      strcpy(tmp,buf);

      /* need to check the configuration location for the existance of
       * this node to determine if it's a multi
       */
      if (G_NODE_IS_ROOT(n->parent) != TRUE && 
          ((struct VyattaNode*)(n->parent->data))->_config._multi == TRUE) {
        sprintf(buf,"node.tag/%s",tmp);
      }
      else {
        sprintf(buf,"%s/%s",d->_data._name,tmp);
      }
    }
    n = n->parent;
  }

  char *buf2 = malloc(strlen(buf)+1);
  strcpy(buf2,buf);
  return buf2;
}

/**
 *
 **/
static void
retrieve_data(char* rel_data_path, GNode *node, const char* root, 
              NODE_OPERATION op)
{
  boolean final_node = FALSE;
  
  if (node == NULL) {
    return;
  }

  const char *tmp = root;//get_cdirp();
  char full_data_path[sizeof(char)*MAX_LENGTH_DIR_PATH];

  strcpy(full_data_path,tmp);
  if (rel_data_path != NULL) {
    strcat(full_data_path,rel_data_path);
  }

  if (g_debug) {
    printf("unionfs::retrieve_data(): %s\n", full_data_path);
    syslog(LOG_DEBUG,"unionfs::retrieve_data(): %s\n", full_data_path);
  }


  //WE'LL WANT TO DO SOMETHING LIKE--IS THIS NODE A VALUE NODE, THEN PROCESS.

  //now check for value at this node and copy
  char *cp = NULL;
  //data_path needs to be of type vtw_path!!
  vtw_path vpath;
  init_path(&vpath, full_data_path);
  //lstat for value file BEFORE retrieving
  if (value_exists(full_data_path) && get_value(&cp, &vpath) == 0) {
    //terminating value need to create new terminating node here!
    struct VyattaNode *vn = calloc(1,sizeof(struct VyattaNode));
    GNode *new_node = g_node_new(vn);
    new_node = insert_sibling_in_order(node,new_node);
    //    new_node = g_node_insert(node, -1, new_node);
    vn->_data._name = cp;
    vn->_data._value = FALSE;//TRUE; //data value
    vn->_data._operation = op;
    vn->_config._priority = LOWEST_PRIORITY;
    if (rel_data_path != NULL) {
      vn->_data._path = (char*)malloc(strlen(rel_data_path)+1);
      strcpy(vn->_data._path,rel_data_path);
    }
    else {
      vn->_data._path = NULL;
    }
    final_node = TRUE;
  }
  /*
    NOTE: will need to implement ptr copy over for the config data that 
    traverses up multi-nodes. Currently this will just read the data from
    the parser AGAIN, but in the name of memory and performance efficiency
    this should be a copy of the already processed data.
   */

  //Now dig up the configuration data 
  char *config_path = get_config_path(node);

  if (config_path != NULL) {
    char *conf_base = (char*)get_tdirp();
    char buf[MAX_LENGTH_HELP_STR];
    sprintf(buf,"%s/%snode.def",conf_base,config_path);
    struct stat s;
    if (g_debug) {
      printf("unionfs::retrieve_data(): config path: %s\n",buf);
      syslog(LOG_DEBUG,"unionfs::retrieve_data(): config path: %s\n",buf);
    }

    struct VyattaNode* vn = (struct VyattaNode*)node->data;
    vtw_def def;
    memset(&def, 0, sizeof(def));
    if ((lstat(buf,&s) == 0) && S_ISREG(s.st_mode)) {
      if (parse_def(&def, buf, FALSE) == 0) {
        if (g_debug) {
          printf("[FOUND node.def]");
          syslog(LOG_DEBUG,"[FOUND node.def]");
        }

        /* either multi or tag--shouldn't have made a difference, but arkady
         * was confused.
         */
        vn->_config._multi = (def.tag | def.multi); 
        if (def.def_tag != 0) {
          vn->_config._limit = def.def_tag;
        }
        else if (def.def_multi != 0) {
          vn->_config._limit = def.def_multi;
        }
        else {
          vn->_config._limit = 0;
        }
      }
    }

    vn->_config._def = def;
    vn->_config._path = config_path;
    char tmp[MAX_LENGTH_DIR_PATH];
    tmp[0] = '\0';
    if (rel_data_path != NULL && strlen(rel_data_path) > 0) {
      strcpy(tmp,rel_data_path);
    }
    strcat(tmp,"/");

    vn->_data._path = (char*)malloc(strlen(tmp)+1);
    strcpy(vn->_data._path,tmp);

    //will stamp an embedded node as a value node
    if (G_NODE_IS_ROOT(node) == FALSE) {
      struct VyattaNode* vn_parent = (struct VyattaNode*)node->parent->data;
      if (vn_parent->_config._multi == TRUE) {
        ((struct VyattaNode*)node->data)->_data._value = TRUE;

        /* patch up to preserve multinode behavior on value node, can also
         * put node.def on node.tag with priority
         * Need to do two things:
         * 1. Come to agreement on where the behavior splits on priority
         *    multinodes
         * 2. Not check for tag node in the def datastructure but use the
         *    new datastructure as at some point tag and multi will be the
         *    same
         */
        //now let's patch up the def multi-nodes
        //move the def for the multinode from the parent to the value node
        struct VyattaNode* vn2 = (struct VyattaNode*)node->data;
        if (final_node == FALSE) { //non-term multi
          if (g_node_n_children(node->parent) == 1) {
            vn2->_config._def
              = ((struct VyattaNode*)node->parent->data)->_config._def;
            memset(&((struct VyattaNode*)node->parent->data)->_config._def,
                   0, sizeof(vtw_def));
          }
          else { //find node other than myself to copy defs across
            GNode *first_child = g_node_first_child(node->parent);
            if (first_child == node) {
              first_child = g_node_next_sibling(first_child);
            }            
            vn2->_config._def
              = ((struct VyattaNode*)first_child->data)->_config._def;
          }
        }
      }
    }
    
    if (g_debug) {
      printf("\n");
    }
  }

  if (G_NODE_IS_ROOT(node) == FALSE) {
    struct VyattaNode* vn = (struct VyattaNode*)node->data;

    if (vn->_config._def.tag && vn->_config._multi) {
      vn->_config._priority = LOWEST_PRIORITY;
    }
    else if (vn->_config._def.def_priority == 0) {
      vn->_config._priority = LOWEST_PRIORITY;
    }
    else {
      vn->_config._priority = vn->_config._def.def_priority;
    }

    if (vn->_config._def.tag && vn->_config._multi) {
      vn->_config._priority_extended = '\0';
    }
    else {
      vn->_config._priority_extended = vn->_config._def.def_priority_ext;
    }
  }

  if (final_node == TRUE) {
    //move defs to child...
    get_term_data_values(node);

    //fix operation on parent because this is a final node
    char buf[MAX_LENGTH_HELP_STR];
    sprintf(buf,"%s/%s",get_adirp(),rel_data_path);
    struct stat s;
    if (lstat(buf,&s) != 0) {
      struct VyattaNode* vn = (struct VyattaNode*)node->data;
      vn->_data._operation = K_CREATE_OP;
    }
    return;
  }
  
  //iterate over directory here
  DIR *dp;
  if ((dp = opendir(full_data_path)) == NULL){
    if (g_debug) {
      //could also be a terminating value now
      printf("unionfs::retrieve_data(), failed to open directory: %s\n",
             full_data_path);
      syslog(LOG_DEBUG,
             "unionfs::retrieve_data(), failed to open directory: %s\n",
             full_data_path);
    }
    return;
  }

  //finally iterate over valid child directory entries
  boolean processed = FALSE;
  boolean whiteout_file_found = FALSE;
  struct dirent *dirp = NULL;
  while ((dirp = readdir(dp)) != NULL) {
    if (strcmp(dirp->d_name,WHITEOUT_FILE) == 0) {
      whiteout_file_found = TRUE;
    }
    if (strcmp(dirp->d_name, ".") != 0 && 
        strcmp(dirp->d_name, "..") != 0 &&
        strcmp(dirp->d_name, MOD_NAME) != 0 &&
        strcmp(dirp->d_name, UNSAVED_FILE) != 0 &&
        strcmp(dirp->d_name, DEF_FILE) != 0 &&
        strcmp(dirp->d_name, WHITEOUT_FILE) != 0 &&
        strcmp(dirp->d_name, VAL_NAME) != 0) {
      processed = TRUE;

      char *data_buf = malloc(strlen(dirp->d_name)+5);
      if (strncmp(dirp->d_name,DELETED_NODE,4) == 0) {
        struct VyattaNode *vn = calloc(1,sizeof(struct VyattaNode));
        if (strncmp(dirp->d_name,DELETED_NODE,4) == 0) {
          strcpy(data_buf,dirp->d_name+4); //SKIP THE .WH.
          vn->_data._operation = K_DEL_OP;
        }
        else {
          strcpy(data_buf,dirp->d_name); 
          vn->_data._operation = K_NO_OP;
        }
        
        //create new node and insert...
        vn->_data._name = data_buf;
        vn->_data._value = FALSE;
        vn->_config._priority = LOWEST_PRIORITY;
        
        char new_data_path[MAX_LENGTH_DIR_PATH];
        sprintf(new_data_path,"%s/%s",rel_data_path,data_buf);
        
        GNode *new_node = g_node_new(vn);
        new_node = insert_sibling_in_order(node,new_node);

        /* will need to enter a special recursion against the active
         * configuration to mark nested delete nodes
         */
        retrieve_data(new_data_path,new_node,get_adirp(),K_DEL_OP);
      } else {
        strcpy(data_buf,dirp->d_name);
        //create new node and insert...
        struct VyattaNode *vn = calloc(1,sizeof(struct VyattaNode));
        vn->_data._name = data_buf;
        vn->_data._value = FALSE;
        vn->_config._priority = LOWEST_PRIORITY;

        char new_data_path[MAX_LENGTH_DIR_PATH];
        sprintf(new_data_path,"%s/%s",rel_data_path,data_buf);

        char active_data_path[MAX_LENGTH_DIR_PATH];
        sprintf(active_data_path,"%s%s",get_adirp(),rel_data_path);
        struct stat s;

        if (lstat(active_data_path,&s) == 0) {
          vn->_data._operation = K_NO_OP;
        }
        else {
          vn->_data._operation = K_CREATE_OP;
          ((struct VyattaNode*)node->data)->_data._operation = K_CREATE_OP;
        }
        //set recursed entry op to del
        if (op == K_DEL_OP) {
          vn->_data._operation = K_DEL_OP;
        }
        GNode *new_node = g_node_new(vn);
        new_node = insert_sibling_in_order(node,new_node);
        if (op == K_DEL_OP) {
          retrieve_data(new_data_path, new_node, get_adirp(),
                        vn->_data._operation);
        }
        else {
          retrieve_data(new_data_path,new_node,root,vn->_data._operation);
        }
      }
    }
  }
  //catch hanging case where embedded multinode is new with no children
  if (processed == FALSE) {
    char active_data_path[MAX_LENGTH_DIR_PATH];
    sprintf(active_data_path,"%s/%s",get_adirp(),rel_data_path);
    struct stat s;
    if ((lstat(active_data_path,&s) != 0)) {
      ((struct VyattaNode*)node->data)->_data._operation = K_CREATE_OP;
    }
  }
  closedir(dp);

  /* if there is a ".wh.__dir_opaque" and were not already
   * iterating the active dir then test for a hidden deletion
   */
  if (whiteout_file_found == TRUE && op != K_DEL_OP) {
    //scan active dir for entry not found in tmp
    DIR *dp_wo;
    //build active directory for this...
    char active_data_path[MAX_LENGTH_DIR_PATH];
    sprintf(active_data_path,"%s%s",get_adirp(),rel_data_path);
    if ((dp_wo = opendir(active_data_path)) != NULL) {
      if (g_debug) {
        //could also be a terminating value now
        syslog(LOG_DEBUG,
               "unionfs::retrieve_data(), failed to open directory: %s\n",
               active_data_path);
        printf("unionfs::retrieve_data(), failed to open directory: %s\n",
               active_data_path);
      } 
      struct dirent *dirp_wo = NULL;
      while ((dirp_wo = readdir(dp_wo)) != NULL) {
        char tmp_new_data_path[MAX_LENGTH_DIR_PATH];
        sprintf(tmp_new_data_path, "%s/%s/%s", get_cdirp(), rel_data_path,
                dirp_wo->d_name);
        struct stat s;
        if (lstat(tmp_new_data_path,&s) != 0) {
          //create new node and insert...
          struct VyattaNode *vn = calloc(1,sizeof(struct VyattaNode));
          char *data_buf = malloc(strlen(dirp_wo->d_name)+1);
          strcpy(data_buf,dirp_wo->d_name); 
          vn->_data._name = data_buf;
          vn->_data._value = FALSE;
          vn->_data._operation = K_DEL_OP;
          vn->_config._priority = LOWEST_PRIORITY;
          
          GNode *new_node = g_node_new(vn);
          new_node = insert_sibling_in_order(node,new_node);
          char new_data_path[MAX_LENGTH_DIR_PATH];
          sprintf(new_data_path,"%s/%s",rel_data_path,dirp_wo->d_name);
          retrieve_data(new_data_path,new_node,get_adirp(),K_DEL_OP);
        }
      }
      closedir(dp_wo);
    }
  }
  
}


/**
 *
 **/
GNode*
common_get_local_session_data(void)
{
  //get root directory
  char *data_path = malloc(sizeof(char)*MAX_LENGTH_DIR_PATH);
  data_path[0] = '\0';

  struct VyattaNode *vn = calloc(1,sizeof(struct VyattaNode));
  vn->_data._name = NULL; //root node has null
  vn->_data._operation = K_NO_OP;
  vn->_config._priority = LOWEST_PRIORITY;

  //create first node
  GNode *root_node = g_node_new(vn);

  /* iterate through recursive calls to parse_new() calls (see original
   * commit())
   */
  retrieve_data(data_path,root_node,get_cdirp(),K_SET_OP);

  return root_node;
}


/**
 *
 **/
boolean
value_exists(const char *path)
{
  char buf[MAX_LENGTH_DIR_PATH];
  sprintf(buf, "%s/%s",path,VAL_NAME);
  struct stat stat_buf;
  return !stat(buf,&stat_buf);
}

/**
 * need to compute parent as it might not be in the node structure
 *
 **/
void
common_set_parent_context(char *cpath, char *dpath)
{
  if (g_debug) {
    printf("common_set_parent_context(incoming): %s, %s\n",cpath,dpath);
    syslog(LOG_DEBUG, "common_set_parent_context(incoming): %s, %s\n",
           cpath, dpath);
  }
  //strip off last path and set
  int index = strlen(cpath)-1;
  if (cpath[index] == '/') {
    while (TRUE) {
      if (cpath[--index] != '/') {
        cpath[index] = '\0';
        break;
      }
    }
  }
  char *ptr = rindex(cpath,'/');
  if (ptr != NULL) {
    *ptr = '\0';
  }
  set_path(cpath,TRUE);

  index = strlen(dpath)-1;
  if (dpath[index] == '/') {
    while (TRUE) {
      if (dpath[--index] != '/') {
        dpath[index] = '\0';
        break;
      }
    }
  }
  ptr = rindex(dpath,'/');
  if (ptr != NULL) {
    *ptr = '\0';
  }
  set_path(dpath,FALSE);
  if (g_debug) {
    printf("common_set_parent_context: %s, %s\n",cpath,dpath);
    syslog(LOG_DEBUG,"common_set_parent_context: %s, %s\n",cpath,dpath);
  }
}

/**
 *
 **/
void
common_set_context(char *cpath, char *dpath)
{
  if (g_debug) {
    printf("common_set_context: %s, %s\n",cpath,dpath);
    syslog(LOG_DEBUG,"common_set_context: %s, %s\n",cpath,dpath);
  }
  set_path(cpath,TRUE);
  set_path(dpath,FALSE);
}

/**
 *
 **/
void
set_path(char *path, boolean config)
{
  //set t_path, m_path
  //tokenize path and iterate
  if (config == FALSE) {
    init_path(&m_path, get_mdirp());
  }
  else {
    init_path(&t_path, get_tdirp());
  }

  char* start_ptr = NULL;
  char* end_ptr = NULL;
  
  if (path == NULL) {
    if (g_debug) {
      printf("unionfs::set_path() null value on entry\n");
      syslog(LOG_DEBUG,"unionfs::set_path() null value on entry\n");
    }
    return;
  }

  start_ptr = path;
  while((end_ptr = index(start_ptr+1,'/')) != NULL) {
    char tmp[1024];
    
    if (*start_ptr == '/') {
      ++start_ptr;
    }

    int size = end_ptr-start_ptr;
    if (size < 1 || size > 1024) {
      return;
    }

    memcpy(tmp, start_ptr, size);
    tmp[size] = '\0';

    if (config == FALSE) {
      push_path_no_escape(&m_path, tmp); //data
    }
    else {
      push_path_no_escape(&t_path, tmp); //config
    }
    start_ptr = end_ptr;
  }
}

/**
 * NEED TO PROTECT AGAINST NESTED COMMIT NODES. CANNOT BE SUPPORTED
 * IN CURRENT HIERARCHICAL STRUCTURE WITHOUT CHANGING HOW UNDERLYING
 * SYSTEM MAINTAINS DATA.
 *
 **/
void
common_commit_copy_to_live_config(GNode *node, boolean suppress_piecewise_copy,
                                  boolean test_mode)
{
  //first check for existence of path before committing
  char *path = ((struct VyattaNode*)(node->data))->_data._path;

  if (g_debug) {
    printf("common_commit_copy_to_live_config(): %s\n",path);
    syslog(LOG_DEBUG,"common_commit_copy_to_live_config(): %s\n",path);
  }

  /* this function is called for each "subtree" that has been successfully
   * committed. before doing anything else, remove the "changed" status
   * from any changed nodes in this subtree first (since this subtree is
   * going into the active config).
   */
  {
    void *cs = cstore_init();
    int ncomps;
    char **pcomps = cstore_path_string_to_path_comps(path, &ncomps);
    /* note: return status is not checked and operation continues even if
     *       this fails. this follows the original logic.
     */
    cstore_unmark_cfg_path_changed(cs, (const char **) pcomps, ncomps);
    cstore_free_path_comps(pcomps, ncomps);
    cstore_free(cs);
  }

  char *command = malloc(MAX_LENGTH_DIR_PATH);
  /* XXX must ... remove ... this ... */
  static const char format0[]="mkdir -p %s ; /bin/true";
  static const char formatpoint5[]="rm -fr '%s'"; /*tmpp*/
  static const char format1[]="cp -r -f %s/* %s"; /*mdirp, tmpp*/
  static const char format1point1[]="mv -f %s/* -t %s"; /*mdirp, tmpp*/
  static const char format1point5[]="rm -fr '%s'/*"; /*tmpp*/
  
  char mbuf[MAX_LENGTH_DIR_PATH];
  sprintf(mbuf,"%s%s",get_mdirp(),path);
  char cbuf[MAX_LENGTH_DIR_PATH];
  sprintf(cbuf,"%s%s",get_cdirp(),path);
  char tbuf[MAX_LENGTH_DIR_PATH];
  sprintf(tbuf,"%s%s",get_tmpp(),path);
  char abuf[MAX_LENGTH_DIR_PATH];
  sprintf(abuf,"%s%s",get_adirp(),path);

  char mbuf_root[MAX_LENGTH_DIR_PATH];
  sprintf(mbuf_root,"%s",get_mdirp());
  char cbuf_root[MAX_LENGTH_DIR_PATH];
  sprintf(cbuf_root,"%s",get_cdirp());
  char tbuf_root[MAX_LENGTH_DIR_PATH];
  sprintf(tbuf_root,"%s",get_tmpp());
  char abuf_root[MAX_LENGTH_DIR_PATH];
  sprintf(abuf_root,"%s",get_adirp());

  //only operate on path if it exists
  //have to clean out tbuf before copying
  sprintf(command, formatpoint5, tbuf);
  if (g_debug) {
    printf("%s\n",command);
    syslog(LOG_DEBUG,"%s\n",command);
    fflush(NULL);
  }
  if (test_mode == FALSE) {
    system(command);
  }

  //mkdir temp merge
  sprintf(command,format0,tbuf);
  if (g_debug) {
    printf("%s\n",command);
    syslog(LOG_DEBUG,"%s\n",command);
    fflush(NULL);
  }
  if (test_mode == FALSE) {
    system(command);
  }



  //cp merge to temp merge
  sprintf(command, format1, mbuf, tbuf);
  if (g_debug) {
    printf("%s\n",command);
    syslog(LOG_DEBUG,"%s\n",command);
    fflush(NULL);
  }
  if (test_mode == FALSE) {
    system(command);
  }

  // unmount the session dir
  if (test_mode == FALSE) {
    sys_umount_session();
  }

  if (suppress_piecewise_copy) {
    sprintf(command, format1point5, abuf_root);
    if (g_debug) {
      printf("%s\n",command);
      syslog(LOG_DEBUG,"%s\n",command);
      fflush(NULL);
    }
    if (test_mode == FALSE) {
      system(command);
    }
    sprintf(command, format1point1, tbuf_root, abuf_root);
    if (g_debug) {
      printf("%s\n",command);
      syslog(LOG_DEBUG,"%s\n",command);
      fflush(NULL);
    }
    if (test_mode == FALSE) {
      system(command);
    }
  }
  else {
    piecewise_copy(node, test_mode);
  }

  if (test_mode == FALSE) {
    sys_mount_session();
  }

  fflush(NULL);

  free(command);
}  


//needed for iteration below
struct SrcDst {
  const char *_src;
  const char *_dst;
  boolean _test_mode;
};

/**
 *
 **/
void
common_commit_clean_temp_config(GNode *root_node, boolean test_mode)
{
  if (g_debug) {
    printf("common_commit_clean_temp_config()\n");
    syslog(LOG_DEBUG,"common_commit_clean_temp_config()\n");
  }
  
  char *command;
  command = malloc(MAX_LENGTH_DIR_PATH);
  /* XXX must ... remove ... this ... */
  static const char format5[]="rm -fr '%s'/{.*,*} >&/dev/null ; /bin/true"; /*cdirp*/

  char tbuf[MAX_LENGTH_DIR_PATH];
  sprintf(tbuf,"%s",get_tmpp());
  char cbuf[MAX_LENGTH_DIR_PATH];
  sprintf(cbuf,"%s",get_cdirp());
  char mbuf[MAX_LENGTH_DIR_PATH];
  sprintf(mbuf,"%s",get_mdirp());
  char abuf[MAX_LENGTH_DIR_PATH];
  sprintf(abuf,"%s",get_adirp());

  if (test_mode == FALSE) {
    sys_umount_session();
  }

  /*
   * Need to add to the following func below to clean up dangling .wh. files.
   * This pass needs to be prior to the commands below (but after the umount).
   * This fixes a bug when higher priority root nodes are deleted and not
   * removed.
   */
  
  /* Iterate through node hierarchy and remove deleted nodes from active
   * config--insurance to protect against priority whiteouts in parent/child
   * order
   */
  //TOP DOWN
  if (root_node != NULL) {
    struct SrcDst sd;
    sd._test_mode = test_mode;
    
    g_node_traverse(root_node,
                    G_PRE_ORDER,
                    G_TRAVERSE_ALL,
                    -1,
                    (GNodeTraverseFunc)delete_wh_func,
                    (gpointer)&sd);
  }

  /* originally the root "changed" marker was being removed here. this is now
   * handled in common_commit_copy_to_live_config() since we need to do it
   * subtree-by-subtree (and also remove the markers from any descendants).
   */

  sprintf(command, format5, cbuf);
  if (g_debug) {
    printf("%s\n",command);
    syslog(LOG_DEBUG,"%s\n",command);
    fflush(NULL);
  }
  if (test_mode == FALSE) {
    system(command);
  }

  if (test_mode == FALSE) {
    sys_mount_session();
  }

  /* notify other users in config mode */
  system("/opt/vyatta/sbin/vyatta-cfg-notify");

  free(command);
}

/**
 *
 **/
static struct VyattaNode*
copy_vyatta_node(struct VyattaNode* vn)
{
  struct VyattaNode *new_vn = calloc(1,sizeof(struct VyattaNode));

  if (vn->_data._name != NULL) {
    new_vn->_data._name = malloc(strlen(vn->_data._name)+1);
    strcpy(new_vn->_data._name,vn->_data._name);
  }
  new_vn->_data._value = vn->_data._value;
  if (vn->_data._path != NULL) {
    new_vn->_data._path = malloc(strlen(vn->_data._path)+1);
    strcpy(new_vn->_data._path,vn->_data._path);
  }
  new_vn->_data._operation = vn->_data._operation;
  new_vn->_config._multi = vn->_config._multi;
  new_vn->_config._priority = vn->_config._priority;
  new_vn->_config._priority_extended = vn->_config._priority_extended;
  if (vn->_config._default != NULL) {
    new_vn->_config._default = malloc(strlen(vn->_config._default)+1);
    strcpy(new_vn->_config._default,vn->_config._default);
  }
  if (vn->_config._path != NULL) {
    new_vn->_config._path = malloc(strlen(vn->_config._path)+1);
    strcpy(new_vn->_config._path,vn->_config._path);
  }
  return new_vn;
}

/**
 *
 **/
static void
get_term_data_values(GNode *node)
{
  struct VyattaNode* vn = (struct VyattaNode*)node->data;
  char full_new_data_path[MAX_LENGTH_DIR_PATH];
  char full_active_data_path[MAX_LENGTH_DIR_PATH];
  sprintf(full_active_data_path,"%s/%s",get_adirp(),vn->_data._path);
  sprintf(full_new_data_path,"%s/%s",get_mdirp(),vn->_data._path);

  GData *datalist;
  g_datalist_init(&datalist);

  //now check for value at this node and copy
  char *cp = NULL;
  //data_path needs to be of type vtw_path!!
  vtw_path vpath;
  //lstat for value file BEFORE retrievin
  gchar **tok_str_new = NULL;
  gchar **tok_str_active = NULL;

  //create full_data_path here
  
  init_path(&vpath, full_active_data_path);
  if (value_exists(full_active_data_path) && get_value(&cp, &vpath) == 0) {
    tok_str_active = g_strsplit(cp,"\n",0);
  }

  init_path(&vpath, full_new_data_path);
  if (value_exists(full_new_data_path) && get_value(&cp, &vpath) == 0) {
    tok_str_new = g_strsplit(cp,"\n",0);
  }

  if (vn->_config._multi == TRUE) {
    //add active elements
    int i;
    for (i = 0; tok_str_active !=  NULL && tok_str_active[i] != NULL; ++i) {
      struct ValueData *data;
      data = (struct ValueData*)calloc(1, sizeof(struct ValueData));
      //HANDLE EMPTY NEW CONFIG
      data->_state = K_DEL_OP;
      g_datalist_set_data(&datalist, tok_str_active[i], data);
    }
    
    //add value elements not found in set yet
    for (i = 0; tok_str_new !=  NULL && tok_str_new[i] != NULL; ++i) {
      gpointer g;
      if ((g = g_datalist_get_data(&datalist,tok_str_new[i])) == NULL) {
        struct ValueData *data;
        data = (struct ValueData*)calloc(1, sizeof(struct ValueData));
        data->_state = K_CREATE_OP;
        g_datalist_set_data(&datalist, tok_str_new[i], data);
      }
      else {
        ((struct ValueData*)g)->_state = K_NO_OP;
      }
    }
  }
  else { //leaf
    struct ValueData *data;
    data = (struct ValueData*)calloc(1, sizeof(struct ValueData));
    if ((tok_str_active == NULL || tok_str_active[0] == NULL) &&
        (tok_str_new == NULL || tok_str_new[0] == NULL)) {
      cp = malloc(sizeof(char));
      cp[0] = '\0';
      data->_state
        = ((struct VyattaNode*)node->parent->data)->_data._operation;
      g_datalist_set_data(&datalist, cp, data);
    }
    else if (tok_str_active == NULL || tok_str_active[0] == NULL) {
      data->_state = K_CREATE_OP;
      g_datalist_set_data(&datalist, tok_str_new[0], data);
    }
    else if (tok_str_new == NULL || tok_str_new[0] == NULL) {
      data->_state = K_DEL_OP;
      g_datalist_set_data(&datalist, tok_str_active[0], data);
    }
    else {
      if (strcmp(tok_str_active[0],tok_str_new[0]) != 0) {
        data->_state = K_SET_OP;
        g_datalist_set_data(&datalist, tok_str_new[0], data);
      }
      else {
        data->_state = K_NO_OP;
        g_datalist_set_data(&datalist, tok_str_new[0], data);
      }
    }
  }

  //now let's process the node's values....

  g_datalist_foreach(&datalist, dlist_test_func, node);
  struct VyattaNode *vn_parent = (struct VyattaNode*)node->data;
  memset(&vn_parent->_config._def, 0, sizeof(vtw_def));

  g_strfreev(tok_str_new);
  g_strfreev(tok_str_active);
}

/**
 *
 **/
static void
dlist_test_func(GQuark key_id,gpointer data,gpointer user_data)
{
  if (key_id == 0) {
    return;
  }
  GNode *node = (GNode*)user_data;
  struct VyattaNode *vn = (struct VyattaNode*)node->children->data;
  struct VyattaNode *vn_parent = (struct VyattaNode*)node->data;
  struct VyattaNode *new_vn = NULL;
  //single entry has alread been created.


  if (vn->_data._value == TRUE) {
    new_vn = copy_vyatta_node(vn);
    GNode *new_node = g_node_new(new_vn);
    g_node_insert_after(node, NULL, new_node);
    new_vn->_config._def = vn->_config._def;
  }
  else {
    new_vn = vn;
    char buf[MAX_LENGTH_DIR_PATH];
    strcpy(buf,new_vn->_data._path);
    
    strcat(buf,"/value:");
    if (vn_parent->_config._def.multi == FALSE) {
      char *tmp = (char*)g_quark_to_string(key_id); 
      char *slash = strchr(tmp,'/');
      if (slash == NULL) {
        strcat(buf,tmp);
      }
      else {
        do {         //escape '/' to %2F
          strncat(buf,tmp,slash - tmp);
          strncat(buf,"%2F",3);
          ++slash;
          tmp = slash;
        } while ((slash = strchr(slash,'/')) != NULL);
        strcat(buf,tmp);
      }
    }
    new_vn->_data._path = (char*)realloc(new_vn->_data._path,strlen(buf)+1);
    strcpy(new_vn->_data._path,buf);
  }
  new_vn->_data._value = TRUE;
  char *ttmp = (char*)g_quark_to_string(key_id);
  new_vn->_data._name = realloc(new_vn->_data._name,strlen(ttmp)+1);
  strcpy(new_vn->_data._name,ttmp);
  new_vn->_config._path = malloc(strlen(vn_parent->_config._path)+10);
  sprintf(new_vn->_config._path,"%s/node.tag",vn_parent->_config._path);

  new_vn->_data._operation = ((struct ValueData*)data)->_state;
  new_vn->_config._def = vn_parent->_config._def;
}


/**
 *
 **/
static GNode*
insert_sibling_in_order(GNode *parent, GNode *child)
{
  //find alphabetical order to insert child into sibling
  GNode *sibling = parent->children;
  while (sibling != NULL) {
    if (strcmp(((struct VyattaNode*)(child->data))->_data._name,
               ((struct VyattaNode*)(sibling->data))->_data._name) > 0) {
      break;
    }
    sibling = sibling->next;
  }
  GNode *new_node = g_node_insert_after(parent, sibling, child);
  return new_node;
}

/**
 *
 **/
static void
piecewise_copy(GNode *root_node, boolean test_mode)
{
  struct SrcDst sd;
  sd._src = get_tmpp(); //copy of merged config
  sd._dst = get_adirp(); //active config
  sd._test_mode = test_mode;
  
  //COPY FROM TOP DOWN
  g_node_traverse(root_node,
                  G_PRE_ORDER,
                  G_TRAVERSE_ALL,
                  -1,
                  (GNodeTraverseFunc)copy_func,
                  (gpointer)&sd);
  
  //delete needs to apply to changes only as src
  sd._src = get_cdirp(); //changes only config
  //DELETE FROM BOTTOM UP, stop on finding children
  g_node_traverse(root_node,
                  G_POST_ORDER,
                  G_TRAVERSE_ALL,
                  -1,
                  (GNodeTraverseFunc)delete_func,
                  (gpointer)&sd);
}

/**
 *
 *
 **/
static gboolean
copy_func(GNode *node, gpointer data)
{
  char buf[MAX_LENGTH_DIR_PATH];
  
  if (node == NULL) {
    return FALSE;
  }

  struct SrcDst *sd = (struct SrcDst*)data;
  char *path = ((struct VyattaNode*)(node->data))->_data._path;

  //might not work for terminating multinodes as the node.val won't be copied
  if (((struct VyattaNode*)(node->data))->_data._value == TRUE &&
      ((struct VyattaNode*)(node->data))->_config._def.tag == FALSE) {
    char *parent_path;
    char buf1[MAX_LENGTH_DIR_PATH];

    //THIS IS ONLY FOR NODE.VAL (or leafs, term multis)

    /* before copy also need to clear out def file in active directory
     * (will copy over current if found)
     * this is for the case where it is set by default, then unset at the
     * node--i.e. no longer a default value.
     */
    if (((struct VyattaNode*)(node->data))->_config._multi == FALSE) {
      //only for leaf
      char *parent_path
        = ((struct VyattaNode*)(node->parent->data))->_data._path;
      if (g_debug) {
        printf("rm %s%sdef\n", sd->_dst, parent_path);
        syslog(LOG_DEBUG, "rm %s%sdef", sd->_dst, parent_path);
        fflush(NULL);
      }
      if (sd->_test_mode == FALSE) {
        if (snprintf(buf, MAX_LENGTH_DIR_PATH, "%s%sdef",
                     sd->_dst, parent_path) < MAX_LENGTH_DIR_PATH) {
          sys_rm(buf);
        }
      }
    }

    parent_path = ((struct VyattaNode*)(node->parent->data))->_data._path;
    if (g_debug) {
      printf("cp %s%s{node.val,def} %s%s\n", sd->_src, parent_path,
             sd->_dst, parent_path);
      syslog(LOG_DEBUG, "cp %s%s{node.val,def} %s%s\n",
             sd->_src, parent_path, sd->_dst, parent_path);
      fflush(NULL);
    }
    if (sd->_test_mode == FALSE) {
      if (snprintf(buf, MAX_LENGTH_DIR_PATH, "%s%snode.val",
                   sd->_src, parent_path) < MAX_LENGTH_DIR_PATH
          &&
          snprintf(buf1, MAX_LENGTH_DIR_PATH, "%s%snode.val",
                   sd->_dst, parent_path) < MAX_LENGTH_DIR_PATH) {
        sys_cp(buf, buf1);
      }
      if (snprintf(buf, MAX_LENGTH_DIR_PATH, "%s%sdef",
                   sd->_src, parent_path) < MAX_LENGTH_DIR_PATH
          &&
          snprintf(buf1, MAX_LENGTH_DIR_PATH, "%s%sdef",
                   sd->_dst, parent_path) < MAX_LENGTH_DIR_PATH) {
        sys_cp(buf, buf1);
      }
    }
  } else {
    if (!IS_DELETE(((struct VyattaNode*)(node->data))->_data._operation)) {
      if (g_debug) {
        printf("mkdir_p %s%s\n", sd->_dst, path);
        syslog(LOG_DEBUG, "mkdir_p %s%s", sd->_dst, path);
        fflush(NULL);
      }
      if (sd->_test_mode == FALSE) {
        if (snprintf(buf, MAX_LENGTH_DIR_PATH, "%s%s", sd->_dst, path)
            < MAX_LENGTH_DIR_PATH) {
          sys_mkdir_p(buf);
        }
      }
    }
  }
  return FALSE;
}

/**
 *
 *
 **/
static gboolean
delete_func(GNode *node, gpointer data)
{
  if (node == NULL) {
    return FALSE;
  }

  struct SrcDst *sd = (struct SrcDst*)data;

  char *command = malloc(MAX_LENGTH_DIR_PATH);

  //DONT HAVE THE COMMAND BELOW BLOW AWAY WHITEOUT FILES!!!!!
  // need to remove opaque file.
  static const char format[]
    = "rm -f '%s%s'{*,.*} >&/dev/null;rmdir %s%s >&/dev/null ; /bin/true";
  // force delete as this is a delete operation with dependency
  static const char format_force_delete[]
    = "rm -f '%s%s'{*,.*} >&/dev/null;rmdir %s%s >&/dev/null ; /bin/true";

  static const char delete_format[]="rm %s%s../.wh.%s >&/dev/null"; 
  
  char *path = ((struct VyattaNode*)(node->data))->_data._path;

  //does this node have any children that have not been copied????
  //NEED RM -FV on changes only directory!!!! for normal removal!!!
  //WILL ONLY REMOVE DIRS WITHOUT CHILD DIRS--just what we want..
  //NEED TO PREVENT THE COMMAND BELOW FROM DELETING WHITEOUT FILES....

  if (IS_NOOP(((struct VyattaNode*)(node->data))->_data._operation)) {
    free(command);
    return FALSE; //see if we can skip this node here
  }


  /* DOESN'T QUITE FIX THE PROBLEM, THE PARENT IS CALLED (AND PROBABLY
   * SHOULDN'T BE)
   */
  if (!IS_DELETE(((struct VyattaNode*)(node->data))->_data._operation)) {
    sprintf(command,format,sd->_src,path,sd->_src,path);
    if (g_debug) {
      printf("%s\n",command);
      syslog(LOG_DEBUG,"%s\n",command);
      fflush(NULL);
    }
    if (sd->_test_mode == FALSE) {
      system(command);
    }
  }

  //if this is a deletion operation, need to remove
  if (IS_DELETE(((struct VyattaNode*)(node->data))->_data._operation) && 
      !IS_ACTIVE(((struct VyattaNode*)(node->data))->_data._operation)) {

    /* DO NOT PERFORM THIS STEP IF THERE ARE SUBDIRECTORIES (only the
     * whiteout file)
     */
    //remove .whiteout file in c directory if encountered in walk.
    sprintf(command, delete_format, sd->_src, path,
            ((struct VyattaNode*)(node->data))->_data._name);
    if (g_debug) {
      printf("%s\n",command);
      syslog(LOG_DEBUG,"%s\n",command);
      fflush(NULL);
    }
    if (sd->_test_mode == FALSE) {
      system(command);
    }
    //if delete then remove entry in active configuration!
    sprintf(command,format_force_delete,sd->_dst,path,sd->_dst,path);
    if (g_debug) {
      printf("%s\n",command);
      syslog(LOG_DEBUG,"%s\n",command);
      fflush(NULL);
    }
    if (sd->_test_mode == FALSE) {
      system(command);
    }
  }
  free(command);

  return FALSE;
}


/**
 *
 *
 **/
static gboolean
delete_wh_func(GNode *node, gpointer data)
{
  if (node == NULL) {
    return FALSE;
  }

  char abuf[MAX_LENGTH_DIR_PATH];
  static const char format0[]="rm -fr '%s' >&/dev/null ; /bin/true";
  struct SrcDst *sd = (struct SrcDst*)data;

  GNode *parent_node = node->parent;
  
  /* on node where operation is delete and parent is noop or active then
   * remove directory from active config
   * if this is a deletion operation, need to remove
   */
  if (parent_node != NULL) {
    NODE_OPERATION op = ((struct VyattaNode*)(node->data))->_data._operation;
    NODE_OPERATION pop
      = ((struct VyattaNode*)(parent_node->data))->_data._operation;
    if (IS_DELETE(op) && !IS_ACTIVE(op) && !IS_DELETE(pop)) {

      char *path = ((struct VyattaNode*)(node->data))->_data._path;
      sprintf(abuf,"%s%s",get_adirp(),path);
      //mkdir temp merge
      char command[MAX_LENGTH_DIR_PATH];
      sprintf(command,format0,abuf);
      if (g_debug) {
        printf("%s\n",command);
        syslog(LOG_DEBUG,"%s\n",command);
        fflush(NULL);
      }
      if (sd->_test_mode == FALSE) {
        system(command);
      }

    }
  }
  else {
    if (IS_DELETE(((struct VyattaNode*)(node->data))->_data._operation) &&
        !IS_ACTIVE(((struct VyattaNode*)(node->data))->_data._operation)) {
      char *path = ((struct VyattaNode*)(node->data))->_data._path;
      sprintf(abuf,"%s%s",get_adirp(),path);
      //mkdir temp merge
      char command[MAX_LENGTH_DIR_PATH];
      sprintf(command,format0,abuf);
      if (g_debug) {
        printf("%s\n",command);
        syslog(LOG_DEBUG,"%s\n",command);
        fflush(NULL);
      }
      if (sd->_test_mode == FALSE) {
        system(command);
      }
    }
  }
  return FALSE;
}

