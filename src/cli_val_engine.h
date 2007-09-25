
/************************************************************************

 Module: cli
 
 **** License ****
 Version: VPL 1.0
 
 The contents of this file are subject to the Vyatta Public License
 Version 1.0 ("License"); you may not use this file except in
 compliance with the License. You may obtain a copy of the License at
 http://www.vyatta.com/vpl
 
 Software distributed under the License is distributed on an "AS IS"
 basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 the License for the specific language governing rights and limitations
 under the License.
 
 This code was originally developed by Vyatta, Inc.
 Portions created by Vyatta are Copyright (C) 2007 Vyatta, Inc.
 All Rights Reserved.
 
 Author: Oleg Moskalenko
 Date: 2007
 Description: "new" cli handler for the reference variables
 
 **** End License ****

*************************************************************************/ 

#if !defined(__CLI_VAL_ENGINE__)
#define __CLI_VAL_ENGINE__

#include "cli_path_utils.h"
#include "cli_val.h"

/*******************
 * Type definitions
 *
 *******************/

typedef enum {

  CLIND_CMD_UNKNOWN=0,               /* ??? */
  CLIND_CMD_PARENT,                  /* .. */
  CLIND_CMD_SELF_NAME,               /* . */
  CLIND_CMD_CHILD,                   /* <name> */
  CLIND_CMD_NEIGHBOR,                /* ../<name> */
  CLIND_CMD_VALUE,                   /* @ */
  CLIND_CMD_PARENT_VALUE,            /* ../@ */
  CLIND_CMD_MULTI_VALUE              /* @@ */

} clind_cmd_type;

typedef struct {

  clind_cmd_type type;
  char value[1025];

} clind_cmd;

typedef struct {

  vtw_type_e val_type;
  char* value;

} clind_val;  

/********************************
 * Main command-handling method:
 *
 ********************************/

int clind_config_engine_apply_command_path(clind_path_ref cfg_path,
					   clind_path_ref tmpl_path,
					   clind_path_ref cmd_path,
					   int check_existence,
					   clind_val *res,
					   const char* root_cfg_path,
					   const char* root_tmpl_path,
					   int return_value_file_name);




#endif /* __CLI_VAL_ENGINE__*/
