#ifndef CLI_OBJ_H
#define CLI_OBJ_H

#include "cli_val.h"

#ifdef __cplusplus
extern "C" {
#endif

/* names of VYATTA env vars */
#define ENV_EDIT_LEVEL "VYATTA_EDIT_LEVEL"
#define ENV_TEMPLATE_LEVEL "VYATTA_TEMPLATE_LEVEL"
#define ENV_A_DIR "VYATTA_ACTIVE_CONFIGURATION_DIR"
#define ENV_C_DIR "VYATTA_CHANGES_ONLY_DIR"
#define ENV_M_DIR "VYATTA_TEMP_CONFIG_DIR"
#define ENV_T_DIR "VYATTA_CONFIG_TEMPLATE"
#define ENV_TMP_DIR "VYATTA_CONFIG_TMP"
#define DEF_A_DIR "/opt/vyatta/config/active"
#define DEF_T_DIR "/opt/vyatta/share/ofr/template"

/* the string to use as $(@), must be set 
   before call to expand_string */
char* get_at_string(void);
void free_at_string(void);

boolean is_in_delete_action(void);
boolean is_in_commit(void);
boolean is_in_exec(void);
void set_in_exec(boolean b);

valstruct* get_cli_value_ptr(void);

first_seg* get_f_seg_a_ptr(void);
first_seg* get_f_seg_c_ptr(void);
first_seg* get_f_seg_m_ptr(void);

const char* get_tdirp(void);
const char* get_cdirp(void);
const char* get_adirp(void);
const char* get_mdirp(void);
const char* get_tmpp(void);

void init_paths(boolean for_commit);

#ifdef __cplusplus
}
#endif

#endif /* CLI_OBJ_H */
