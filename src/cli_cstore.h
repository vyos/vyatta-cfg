/*
 * Copyright (C) 2010 Vyatta, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CLI_VAL_CSTORE_H_
#define _CLI_VAL_CSTORE_H_
#ifdef __cplusplus
extern "C" {
#endif

/* this header file contains all definitions/declarations in the original
 * CLI implementation that are needed by the cstore library.
 */

/* types */
typedef int boolean;

typedef enum {
  ERROR_TYPE,
  INT_TYPE,
  IPV4_TYPE,
  IPV4NET_TYPE,
  IPV6_TYPE,
  IPV6NET_TYPE,
  MACADDR_TYPE,
  DOMAIN_TYPE,  /*end of addr types */
  TEXT_TYPE,
  BOOL_TYPE,
  PRIORITY_TYPE
} vtw_type_e;

typedef struct {
  vtw_type_e val_type;
  char      *val;
  int        cnt;  /* >0 means multivalue */
  char     **vals; /* We might union with val */
  vtw_type_e *val_types; /* used with vals and multitypes */
  boolean    free_me;
} valstruct;

typedef enum {
  LIST_OP,  /* right is next, left is list elem */
  HELP_OP,  /* right is help string, left is elem */
  EXEC_OP, /* left command string, right help string */
  PATTERN_OP,  /* left to var, right to pattern */
  OR_OP,
  AND_OP,
  NOT_OP,
  COND_OP,   /* aux field specifies cond type (GT, GE, etc.)*/
  VAL_OP,    /* for strings used in other nodes */
  VAR_OP,    /* string points to var */
  B_QUOTE_OP, /* string points to operand to be executed */
  ASSIGN_OP  /* left to var, right to exp */
} vtw_oper_e;

typedef struct vtw_node{
  vtw_oper_e       vtw_node_oper;
  struct vtw_node *vtw_node_left;
  struct vtw_node *vtw_node_right;
  char            *vtw_node_string;
  int              vtw_node_aux;
  vtw_type_e       vtw_node_type;
  valstruct        vtw_node_val; /* we'll union it later */
} vtw_node;

typedef struct {
  vtw_node *vtw_list_head;
  vtw_node *vtw_list_tail;
} vtw_list;

typedef enum {
  delete_act,
  create_act,
  activate_act,
  update_act,
  syntax_act,
  commit_act,
  begin_act,
  end_act,
  top_act
} vtw_act_type;

typedef struct {
  vtw_type_e def_type;
  vtw_type_e def_type2;
  char      *def_type_help;
  char      *def_node_help;
  char      *def_default;
  unsigned int def_priority;
  char      *def_priority_ext;
  char      *def_enumeration;
  char      *def_comp_help;
  char      *def_allowed;
  char      *def_val_help;
  unsigned int def_tag;
  unsigned int def_multi;
  boolean    tag;
  boolean    multi;
  vtw_list   actions[top_act];
} vtw_def;

/* extern variables */
extern void *var_ref_handle;
extern FILE *out_stream;
extern FILE *err_stream;

/* note that some functions may be used outside the actual CLI operations,
 * so output may not have been initialized. nop in such cases.
 */
#define OUTPUT_USER(fmt, args...) do \
  { \
    if (out_stream) { \
      fprintf(out_stream, fmt , ##args); \
    } \
  } while (0);

/* functions */
const valstruct *get_syntax_self_in_valstruct(const vtw_node *vnode);
int get_shell_command_output(const char *cmd, char *buf,
                             unsigned int buf_size);
int parse_def(vtw_def *defp, const char *path, boolean type_only);
boolean validate_value(const vtw_def *def, char *value);
boolean execute_list(vtw_node *cur, const vtw_def *def, const char *outbuf);
const char *type_to_name(vtw_type_e type);
int initialize_output(const char *op);
void bye(const char *msg, ...) __attribute__((format(printf, 1, 2), noreturn));
int redirect_output(void);
int restore_output(void);

/* functions from cli_objects */
char *get_at_string(void);
void set_in_commit(boolean b);
void set_at_string(char* s);
void set_in_delete_action(boolean b);

#ifdef __cplusplus
}
#endif
#endif /* _CLI_VAL_CSTORE_H_ */

