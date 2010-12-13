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

#ifndef _CSTORE_H_
#define _CSTORE_H_
#include <cstdarg>
#include <vector>
#include <string>
#include <tr1/unordered_map>

#include <cli_cstore.h>

/* declare perl internal functions. just need these two so don't include
 * all the perl headers.
 */
extern "C" void Perl_croak_nocontext(const char* pat, ...)
  __attribute__((noreturn))
  __attribute__((format(__printf__,1,2)))
  __attribute__((nonnull(1)));

extern "C" void* Perl_get_context(void)
  __attribute__((warn_unused_result));

#define ASSERT_IN_SESSION assert_internal(inSession(), \
                            "calling %s() without config session", \
                            __func__);


/* macros for saving/restoring paths.
 * note: this allows "nested" save/restore invocations but NOT recursive ones.
 */
#define SAVE_PATHS save_paths(&__func__)
#define RESTORE_PATHS restore_paths(&__func__)

using namespace std;

class Cstore {
public:
  Cstore() { init(); };
  Cstore(string& env);
  virtual ~Cstore() {};

  // types
  template<class K, class V>
    class MapT : public tr1::unordered_map<K, V> {};

  // constants
  static const string C_NODE_STATUS_DELETED;
  static const string C_NODE_STATUS_ADDED;
  static const string C_NODE_STATUS_CHANGED;
  static const string C_NODE_STATUS_STATIC;

  static const string C_ENV_EDIT_LEVEL;
  static const string C_ENV_TMPL_LEVEL;

  static const string C_ENV_SHELL_PROMPT;
  static const string C_ENV_SHELL_CWORDS;
  static const string C_ENV_SHELL_CWORD_COUNT;

  static const string C_ENV_SHAPI_COMP_VALS;
  static const string C_ENV_SHAPI_LCOMP_VAL;
  static const string C_ENV_SHAPI_COMP_HELP;
  static const string C_ENV_SHAPI_HELP_ITEMS;
  static const string C_ENV_SHAPI_HELP_STRS;

  static const string C_ENUM_SCRIPT_DIR;
  static const string C_LOGFILE_STDOUT;

  static const size_t MAX_CMD_OUTPUT_SIZE = 4096;

  // for sorting
  /* apparently unordered_map template does not work with "enum" type, so
   * change this to simply unsigned ints to allow unifying all map types,
   * i.e., "Cstore::MapT".
   */
  static const unsigned int SORT_DEFAULT;
  static const unsigned int SORT_DEB_VERSION;
  static const unsigned int SORT_NONE;

  ////// the public cstore interface
  //// functions implemented in this base class
  // these operate on template path
  bool validateTmplPath(const vector<string>& path_comps, bool validate_vals);
  bool validateTmplPath(const vector<string>& path_comps, bool validate_vals,
                        vtw_def& def);
  bool getParsedTmpl(const vector<string>& path_comps,
                     MapT<string, string>& tmap, bool allow_val = true);
  void tmplGetChildNodes(const vector<string>& path_comps,
                         vector<string>& cnodes);

  /******
   * functions for actual CLI operations:
   *   set
   *   delete
   *   activate
   *   deactivate
   *   rename
   *   copy
   *   comment
   *   discard
   *   move (currently this is only available in cfg-cmd-wrapper)
   *   edit-related commands (invoked from shell functions)
   *   completion-related (for completion script)
   *   session-related (setup, teardown, etc.)
   *   load
   *
   * these operate on the "working config" and the session and MUST NOT
   * be used by anything other than the listed operations.
   */
  // set
  bool validateSetPath(const vector<string>& path_comps);
  bool setCfgPath(const vector<string>& path_comps);
  // delete
  bool validateDeletePath(const vector<string>& path_comps, vtw_def& def);
  bool deleteCfgPath(const vector<string>& path_comps, const vtw_def& def);
  // activate (actually "unmark deactivated" since it is 2-state, not 3)
  bool validateActivatePath(const vector<string>& path_comps);
  bool unmarkCfgPathDeactivated(const vector<string>& path_comps);
  // deactivate
  bool validateDeactivatePath(const vector<string>& path_comps);
  bool markCfgPathDeactivated(const vector<string>& path_comps);
  // rename
  bool validateRenameArgs(const vector<string>& args);
  bool renameCfgPath(const vector<string>& args);
  // copy
  bool validateCopyArgs(const vector<string>& args);
  bool copyCfgPath(const vector<string>& args);
  // comment
  bool validateCommentArgs(const vector<string>& args, vtw_def& def);
  bool commentCfgPath(const vector<string>& args, const vtw_def& def);
  // discard
  bool discardChanges();
  // move
  bool validateMoveArgs(const vector<string>& args);
  bool moveCfgPath(const vector<string>& args);
  // edit-related
  bool getEditEnv(const vector<string>& path_comps, string& env);
  bool getEditUpEnv(string& env);
  bool getEditResetEnv(string& env);
  bool editLevelAtRoot() {
    return edit_level_at_root();
  };
  // completion-related
  bool getCompletionEnv(const vector<string>& comps, string& env);
  void getEditLevel(vector<string>& comps) {
    get_edit_level(comps);
  };
  // session-related
  virtual bool markSessionUnsaved() = 0;
  virtual bool unmarkSessionUnsaved() = 0;
  virtual bool sessionUnsaved() = 0;
  virtual bool sessionChanged() = 0;
  virtual bool setupSession() = 0;
  virtual bool teardownSession() = 0;
  virtual bool inSession() = 0;
  // commit
  bool unmarkCfgPathChanged(const vector<string>& path_comps);
  // load
  bool loadFile(const char *filename);

  /******
   * these functions are observers of the current "working config" or
   * "active config" during a config session.
   * MOST users of the cstore API should be using these functions (most
   * likely during a commit operation).
   *
   * note that these MUST NOT worry about "deactivated" state.
   * for these functions, deactivated nodes are equivalent to having been
   * deleted. in other words, these functions are NOT "deactivate-aware".
   *
   * also, the functions that can be used to observe "active config" can
   * be used outside a config session as well (only when observing active
   * config, of course).
   */
  // observers for "working config" (by default) OR "active config"
  bool cfgPathExists(const vector<string>& path_comps,
                     bool active_cfg = false);
  void cfgPathGetChildNodes(const vector<string>& path_comps,
                            vector<string>& cnodes, bool active_cfg = false);
  bool cfgPathGetValue(const vector<string>& path_comps, string& value,
                       bool active_cfg = false);
  bool cfgPathGetValues(const vector<string>& path_comps,
                        vector<string>& values, bool active_cfg = false);
  bool cfgPathGetComment(const vector<string>& path_comps, string& comment,
                         bool active_cfg = false);
  bool cfgPathDefault(const vector<string>& path_comps,
                      bool active_cfg = false);

  /* observers for working AND active configs (at the same time).
   * MUST ONLY be used during config session.
   */
  bool cfgPathDeleted(const vector<string>& path_comps);
  bool cfgPathAdded(const vector<string>& path_comps);
  bool cfgPathChanged(const vector<string>& path_comps);
  void cfgPathGetDeletedChildNodes(const vector<string>& path_comps,
                                   vector<string>& cnodes);
  void cfgPathGetDeletedValues(const vector<string>& path_comps,
                               vector<string>& dvals);
  void cfgPathGetChildNodesStatus(const vector<string>& path_comps,
                                  MapT<string, string>& cmap) {
    get_child_nodes_status(path_comps, cmap, NULL);
  };
  void cfgPathGetChildNodesStatus(const vector<string>& path_comps,
                                  MapT<string, string>& cmap,
                                  vector<string>& sorted_keys) {
    get_child_nodes_status(path_comps, cmap, &sorted_keys);
  };

  /* observers for "effective config". can be used both during a config
   * session and outside a config session. more detailed information
   * can be found in the source file.
   */
  bool cfgPathEffective(const vector<string>& path_comps);
  void cfgPathGetEffectiveChildNodes(const vector<string>& path_comps,
                                     vector<string>& cnodes);
  bool cfgPathGetEffectiveValue(const vector<string>& path_comps,
                                string& value);
  bool cfgPathGetEffectiveValues(const vector<string>& path_comps,
                                 vector<string>& values);

  /******
   * "deactivate-aware" observers of the current working or active config.
   * these are the only functions that are allowed to see the "deactivate"
   * state of config nodes.
   *
   * these functions MUST ONLY be used by operations that NEED to distinguish
   * between deactivated nodes and deleted nodes. below is the list
   * of operations that are allowed to use these functions:
   *   * configuration output (show, save, load)
   *
   * operations that are not on the above list MUST NOT use these
   * "deactivate-aware" functions.
   *
   * note: the last argument "include_deactivated" for the DA functions
   *       is for implementation convenience and does not need to be
   *       passed in when calling them.
   */
  // working or active config
  bool cfgPathDeactivated(const vector<string>& path_comps,
                          bool active_cfg = false);
  bool cfgPathMarkedDeactivated(const vector<string>& path_comps,
                                bool active_cfg = false);
  bool cfgPathExistsDA(const vector<string>& path_comps,
                       bool active_cfg = false,
                       bool include_deactivated = true);
  void cfgPathGetChildNodesDA(const vector<string>& path_comps,
                              vector<string>& cnodes,
                              bool active_cfg = false,
                              bool include_deactivated = true);
  bool cfgPathGetValueDA(const vector<string>& path_comps, string& value,
                         bool active_cfg = false,
                         bool include_deactivated = true);
  bool cfgPathGetValuesDA(const vector<string>& path_comps,
                          vector<string>& values,
                          bool active_cfg = false,
                          bool include_deactivated = true);
  // working AND active configs
  void cfgPathGetDeletedChildNodesDA(const vector<string>& path_comps,
                                     vector<string>& cnodes,
                                     bool include_deactivated = true);
  void cfgPathGetDeletedValuesDA(const vector<string>& path_comps,
                                 vector<string>& dvals,
                                 bool include_deactivated = true);
  void cfgPathGetChildNodesStatusDA(const vector<string>& path_comps,
                                    MapT<string, string>& cmap) {
    get_child_nodes_status_da(path_comps, cmap, NULL);
  };
  void cfgPathGetChildNodesStatusDA(const vector<string>& path_comps,
                                    MapT<string, string>& cmap,
                                    vector<string>& sorted_keys) {
    get_child_nodes_status_da(path_comps, cmap, &sorted_keys);
  };

  // util functions
  static void sortNodes(vector<string>& nvec,
                        unsigned int sort_alg = SORT_DEFAULT) {
    sort_nodes(nvec, sort_alg);
  };

  /* these are internal API functions and operate on current cfg and
   * tmpl paths during cstore operations. they are only used to work around
   * the limitations of the original CLI library implementation and MUST NOT
   * be used by anyone other than the original CLI library.
   */
  char *getVarRef(const string& ref_str, vtw_type_e& type, bool from_active);
  bool setVarRef(const string& ref_str, const string& value, bool to_active);

protected:
  ////// functions for subclasses
  void output_user(const char *fmt, ...);
  void output_user_err(const char *fmt, ...);
  void output_internal(const char *fmt, ...);
  void exit_internal(const char *fmt, ...);
  void assert_internal(bool cond, const char *fmt, ...);

private:
  ////// member class
  // for variable reference
  class VarRef;

  ////// virtual
  /* "path modifiers"
   * note: only these functions are allowed to permanently change the paths.
   *       all other functions must "preserve" the paths (but can change paths
   *       "during" an invocation), i.e., the paths after invocation must be
   *       the same as before invocation.
   */
  // begin path modifiers
  virtual void push_tmpl_path(const string& path_comp) = 0;
  virtual void push_tmpl_path_tag() = 0;
  virtual string pop_tmpl_path() = 0;
  virtual void push_cfg_path(const string& path_comp) = 0;
  virtual void push_cfg_path_val() = 0;
  virtual string pop_cfg_path() = 0;
  virtual void append_cfg_path(const vector<string>& path_comps) = 0;
  virtual void reset_paths() = 0;
  virtual void save_paths(const void *handle = NULL) = 0;
  virtual void restore_paths(const void *handle = NULL) = 0;
  virtual bool cfg_path_at_root() = 0;
  virtual bool tmpl_path_at_root() = 0;
  // end path modifiers

  // these operate on current tmpl path
  virtual bool tmpl_node_exists() = 0;
  virtual bool tmpl_parse(vtw_def& def) = 0;

  // these operate on current work path (or active with "active_cfg")
  virtual bool remove_node() = 0;
  virtual void get_all_child_node_names_impl(vector<string>& cnodes,
                                             bool active_cfg = false) = 0;
  virtual void get_all_tmpl_child_node_names(vector<string>& cnodes) = 0;
  virtual bool write_value_vec(const vector<string>& vvec,
                               bool active_cfg = false) = 0;
  virtual bool add_node() = 0;
  virtual bool rename_child_node(const string& oname, const string& nname) = 0;
  virtual bool copy_child_node(const string& oname, const string& nname) = 0;
  virtual bool mark_display_default() = 0;
  virtual bool unmark_display_default() = 0;
  virtual bool mark_deactivated() = 0;
  virtual bool unmark_deactivated() = 0;
  virtual bool unmark_deactivated_descendants() = 0;
  virtual bool mark_changed_with_ancestors() = 0;
  virtual bool unmark_changed_with_descendants() = 0;
  virtual bool remove_comment() = 0;
  virtual bool set_comment(const string& comment) = 0;
  virtual bool discard_changes(unsigned long long& num_removed) = 0;

  // observers for current work path
  virtual bool cfg_node_changed() = 0;

  // observers for current work path or active path
  virtual bool read_value_vec(vector<string>& vvec, bool active_cfg) = 0;
  virtual bool cfg_node_exists(bool active_cfg) = 0;
  virtual bool marked_deactivated(bool active_cfg) = 0;
  virtual bool get_comment(string& comment, bool active_cfg) = 0;
  virtual bool marked_display_default(bool active_cfg) = 0;

  // observers during commit operation
  virtual bool marked_committed(const vtw_def& def, bool is_set) = 0;

  // these operate on both current tmpl and work paths
  virtual bool validate_val_impl(vtw_def *def, char *value) = 0;

  // observers for "edit/tmpl levels" (for "edit"-related operations)
  /* note that these should be handled in the base class since they
   * should not be implementation-specific. however, current definitions
   * of these "levels" environment vars require filesystem-specific
   * "escapes", so handle them in derived class.
   *
   * revisit when the env vars are redefined.
   *
   * these operate on current "work" or "tmpl" path, i.e., cfg/tmpl path
   * needs to be set up before calling these.
   */
  virtual string get_edit_level_path() = 0;
  virtual string get_tmpl_level_path() = 0;
  virtual void get_edit_level(vector<string>& path_comps) = 0;
  virtual bool edit_level_at_root() = 0;

  // these are for testing/debugging
  virtual string cfg_path_to_str() = 0;
  virtual string tmpl_path_to_str() = 0;

  ////// implemented
  // for sorting
  typedef bool (*SortFuncT)(std::string, std::string);
  static MapT<unsigned int, SortFuncT> _sort_func_map;

  static bool sort_func_deb_version(string a, string b);
  static void sort_nodes(vector<string>& nvec,
                         unsigned int sort_alg = SORT_DEFAULT);

  // init
  static bool _init;
  static void init() {
    if (_init) {
      return;
    }
    _init = true;
    _sort_func_map[SORT_DEB_VERSION] = &sort_func_deb_version;
  }

  // begin path modifiers (only these can change path permanently)
  bool append_tmpl_path(const vector<string>& path_comps, bool& is_tag);
  bool append_tmpl_path(const vector<string>& path_comps) {
    bool dummy;
    return append_tmpl_path(path_comps, dummy);
  };
  bool append_tmpl_path(const string& path_comp, bool& is_tag) {
    return append_tmpl_path(vector<string>(1, path_comp), is_tag);
  };
  bool append_tmpl_path(const string& path_comp) {
    bool dummy;
    return append_tmpl_path(path_comp, dummy);
  };
  // end path modifiers

  // these require full path
  // (note: get_parsed_tmpl also uses current tmpl path)
  bool get_parsed_tmpl(const vector<string>& path_comps, bool validate_vals,
                       vtw_def& def, string& error);
  bool get_parsed_tmpl(const vector<string>& path_comps, bool validate_vals,
                       vtw_def& def) {
    string dummy;
    return get_parsed_tmpl(path_comps, validate_vals, def, dummy);
  };
  bool validate_act_deact(const vector<string>& path_comps, const string& op,
                          vtw_def& def);
  bool validate_rename_copy(const vector<string>& args, const string& op);
  bool conv_move_args_for_rename(const vector<string>& args,
                                 vector<string>& edit_path_comps,
                                 vector<string>& rn_args);
  bool cfg_path_exists(const vector<string>& path_comps,
                       bool active_cfg, bool include_deactivated);
  bool set_cfg_path(const vector<string>& path_comps, bool output);
  void get_child_nodes_status(const vector<string>& path_comps,
                              Cstore::MapT<string, string>& cmap,
                              vector<string> *sorted_keys);
  void get_child_nodes_status_da(const vector<string>& path_comps,
                                 Cstore::MapT<string, string>& cmap,
                                 vector<string> *sorted_keys);

  // these operate on current work path (or active with "active_cfg")
  bool remove_tag();
  bool remove_value_from_multi(const string& value);
  bool write_value(const string& value, bool active_cfg = false) {
    vector<string> vvec(1, value);
    return write_value_vec(vvec, active_cfg);
  };
  bool add_tag(const vtw_def& def);
  bool add_value_to_multi(const vtw_def& def, const string& value);
  bool add_child_node(const string& name) {
    push_cfg_path(name);
    bool ret = add_node();
    pop_cfg_path();
    return ret;
  };
  void get_all_child_node_names(vector<string>& cnodes, bool active_cfg,
                                bool include_deactivated);

  // observers for work path or active path
  bool cfg_value_exists(const string& value, bool active_cfg);

  // these operate on both current tmpl and work paths
  bool validate_val(const vtw_def *def, const string& value);
  bool create_default_children();
  void get_edit_env(string& env);

  // util functions
  string get_shell_prompt(const string& level);
  void shell_escape_squotes(string& str);
  void print_str_vec(const char *pre, const char *post,
                     const vector<string>& vec, const char *quote);

  // output functions
  void voutput_user(FILE *out, FILE *dout, const char *fmt, va_list alist);
  void voutput_internal(const char *fmt, va_list alist);
  void vexit_internal(const char *fmt, va_list alist);
};

#endif /* _CSTORE_H_ */

