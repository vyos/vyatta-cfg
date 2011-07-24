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

#include <cli_cstore.h>
#include <cstore/util.hpp>
#include <cstore/cpath.hpp>
#include <cstore/ctemplate.hpp>

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

// forward decl
namespace cnode {
class CfgNode;
}
namespace commit {
class PrioNode;
}

namespace cstore { // begin namespace cstore

using namespace std;

class Cstore {
public:
  Cstore() { init(); };
  Cstore(string& env);
  virtual ~Cstore() {};

  // factory functions
  static Cstore *createCstore(bool use_edit_level = false);
  static Cstore *createCstore(const string& session_id, string& env);

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
   * i.e., "cstore::MapT".
   */
  static const unsigned int SORT_DEFAULT;
  static const unsigned int SORT_DEB_VERSION;
  static const unsigned int SORT_NONE;

  ////// the public cstore interface
  //// functions implemented in this base class
  // these operate on template path
  bool validateTmplPath(const Cpath& path_comps, bool validate_vals);
  tr1::shared_ptr<Ctemplate> parseTmpl(const Cpath& path_comps,
                                       bool validate_vals);
  bool getParsedTmpl(const Cpath& path_comps, MapT<string, string>& tmap,
                     bool allow_val = true);
  void tmplGetChildNodes(const Cpath& path_comps, vector<string>& cnodes);

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
  bool validateSetPath(const Cpath& path_comps);
  bool setCfgPath(const Cpath& path_comps);
  // delete
  bool deleteCfgPath(const Cpath& path_comps);
  // activate (actually "unmark deactivated" since it is 2-state, not 3)
  bool validateActivatePath(const Cpath& path_comps);
  bool unmarkCfgPathDeactivated(const Cpath& path_comps);
  // deactivate
  bool validateDeactivatePath(const Cpath& path_comps);
  bool markCfgPathDeactivated(const Cpath& path_comps);
  // rename
  bool validateRenameArgs(const Cpath& args);
  bool renameCfgPath(const Cpath& args);
  // copy
  bool validateCopyArgs(const Cpath& args);
  bool copyCfgPath(const Cpath& args);
  // comment
  bool commentCfgPath(const Cpath& args);
  // discard
  bool discardChanges();
  // move
  bool validateMoveArgs(const Cpath& args);
  bool moveCfgPath(const Cpath& args);
  // edit-related
  bool getEditEnv(const Cpath& path_comps, string& env);
  bool getEditUpEnv(string& env);
  bool getEditResetEnv(string& env);
  bool editLevelAtRoot() {
    return edit_level_at_root();
  };
  // completion-related
  bool getCompletionEnv(const Cpath& comps, string& env);
  void getEditLevel(Cpath& comps) {
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
  bool unmarkCfgPathChanged(const Cpath& path_comps);
  bool executeTmplActions(char *at_str, const Cpath& path,
                          const Cpath& disp_path, const vtw_node *actions,
                          const vtw_def *def);
  bool cfgPathMarkedCommitted(const Cpath& path_comps, bool is_delete);
  bool markCfgPathCommitted(const Cpath& path_comps, bool is_delete);
  virtual bool clearCommittedMarkers() = 0;
  virtual bool commitConfig(commit::PrioNode& pnode) = 0;
  virtual bool getCommitLock() = 0;
    /* note: the getCommitLock() function must guarantee lock release/cleanup
     * upon process termination (either normally or abnormally). there is no
     * separate call for releasing the lock.
     */
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
  bool cfgPathExists(const Cpath& path_comps, bool active_cfg = false);
  void cfgPathGetChildNodes(const Cpath& path_comps, vector<string>& cnodes,
                            bool active_cfg = false);
  bool cfgPathGetValue(const Cpath& path_comps, string& value,
                       bool active_cfg = false);
  bool cfgPathGetValues(const Cpath& path_comps, vector<string>& values,
                        bool active_cfg = false);
  bool cfgPathGetComment(const Cpath& path_comps, string& comment,
                         bool active_cfg = false);
  bool cfgPathDefault(const Cpath& path_comps, bool active_cfg = false);

  /* observers for working AND active configs (at the same time).
   * MUST ONLY be used during config session.
   */
  bool cfgPathDeleted(const Cpath& path_comps);
  bool cfgPathAdded(const Cpath& path_comps);
  bool cfgPathChanged(const Cpath& path_comps);
  void cfgPathGetDeletedChildNodes(const Cpath& path_comps,
                                   vector<string>& cnodes);
  void cfgPathGetDeletedValues(const Cpath& path_comps,
                               vector<string>& dvals);
  void cfgPathGetChildNodesStatus(const Cpath& path_comps,
                                  MapT<string, string>& cmap) {
    get_child_nodes_status(path_comps, cmap, NULL);
  };
  void cfgPathGetChildNodesStatus(const Cpath& path_comps,
                                  MapT<string, string>& cmap,
                                  vector<string>& sorted_keys) {
    get_child_nodes_status(path_comps, cmap, &sorted_keys);
  };

  /* observers for "effective config". can be used both during a config
   * session and outside a config session. more detailed information
   * can be found in the source file.
   */
  bool cfgPathEffective(const Cpath& path_comps);
  void cfgPathGetEffectiveChildNodes(const Cpath& path_comps,
                                     vector<string>& cnodes);
  bool cfgPathGetEffectiveValue(const Cpath& path_comps, string& value);
  bool cfgPathGetEffectiveValues(const Cpath& path_comps,
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
  bool cfgPathDeactivated(const Cpath& path_comps, bool active_cfg = false);
  bool cfgPathMarkedDeactivated(const Cpath& path_comps,
                                bool active_cfg = false);
  bool cfgPathExistsDA(const Cpath& path_comps, bool active_cfg = false,
                       bool include_deactivated = true);
  void cfgPathGetChildNodesDA(const Cpath& path_comps, vector<string>& cnodes,
                              bool active_cfg = false,
                              bool include_deactivated = true);
  bool cfgPathGetValueDA(const Cpath& path_comps, string& value,
                         bool active_cfg = false,
                         bool include_deactivated = true);
  bool cfgPathGetValuesDA(const Cpath& path_comps, vector<string>& values,
                          bool active_cfg = false,
                          bool include_deactivated = true);
  // working AND active configs
  void cfgPathGetDeletedChildNodesDA(const Cpath& path_comps,
                                     vector<string>& cnodes,
                                     bool include_deactivated = true);
  void cfgPathGetDeletedValuesDA(const Cpath& path_comps,
                                 vector<string>& dvals,
                                 bool include_deactivated = true);
  void cfgPathGetChildNodesStatusDA(const Cpath& path_comps,
                                    MapT<string, string>& cmap) {
    get_child_nodes_status_da(path_comps, cmap, NULL);
  };
  void cfgPathGetChildNodesStatusDA(const Cpath& path_comps,
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
  char *getVarRef(const char *ref_str, vtw_type_e& type, bool from_active);
  bool setVarRef(const char *ref_str, const char *value, bool to_active);

protected:
  class SavePaths {
  public:
    virtual ~SavePaths() = 0;
  };

  ////// functions for subclasses
  static void output_user(const char *fmt, ...);
  static void output_user_err(const char *fmt, ...);
  static void output_internal(const char *fmt, ...);
  static void exit_internal(const char *fmt, ...);
  static void assert_internal(bool cond, const char *fmt, ...);

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
  virtual void push_tmpl_path(const char *path_comp) = 0;
  virtual void push_tmpl_path_tag() = 0;
  virtual void pop_tmpl_path() = 0;
  virtual void pop_tmpl_path(string& last) = 0;
  virtual void push_cfg_path(const char *path_comp) = 0;
  virtual void pop_cfg_path() = 0;
  virtual void pop_cfg_path(string& last) = 0;
  virtual void append_cfg_path(const Cpath& path_comps) = 0;
  virtual void reset_paths(bool to_root = true) = 0;
  virtual auto_ptr<SavePaths> create_save_paths() = 0;
  virtual bool cfg_path_at_root() = 0;
  virtual bool tmpl_path_at_root() = 0;
  // end path modifiers

  // these operate on current tmpl path
  virtual bool tmpl_node_exists() = 0;
  virtual Ctemplate *tmpl_parse() = 0;

  // these operate on current work path (or active with "active_cfg")
  virtual bool remove_node() = 0;
  virtual void get_all_child_node_names_impl(vector<string>& cnodes,
                                             bool active_cfg = false) = 0;
  virtual void get_all_tmpl_child_node_names(vector<string>& cnodes) = 0;
  virtual bool write_value_vec(const vector<string>& vvec,
                               bool active_cfg = false) = 0;
  virtual bool add_node() = 0;
  virtual bool rename_child_node(const char *oname, const char *nname) = 0;
  virtual bool copy_child_node(const char *oname, const char *nname) = 0;
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
  virtual void get_edit_level(Cpath& path_comps) = 0;
  virtual bool edit_level_at_root() = 0;

  // functions for commit operation
  virtual bool marked_committed(bool is_delete) = 0;
  virtual bool mark_committed(bool is_delete) = 0;

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
  bool append_tmpl_path(const Cpath& path_comps, bool& is_tag);
  bool append_tmpl_path(const Cpath& path_comps) {
    bool dummy;
    return append_tmpl_path(path_comps, dummy);
  };
  bool append_tmpl_path(const char *path_comp, bool& is_tag) {
    Cpath p;
    p.push(path_comp);
    return append_tmpl_path(p, is_tag);
  };
  bool append_tmpl_path(const char *path_comp) {
    bool dummy;
    return append_tmpl_path(path_comp, dummy);
  };
  // end path modifiers

  // these require full path
  // (note: get_parsed_tmpl also uses current tmpl path)
  tr1::shared_ptr<Ctemplate> get_parsed_tmpl(const Cpath& path_comps,
                                             bool validate_vals,
                                             string& error);
  tr1::shared_ptr<Ctemplate> get_parsed_tmpl(const Cpath& path_comps,
                                             bool validate_vals) {
    string dummy;
    return get_parsed_tmpl(path_comps, validate_vals, dummy);
  };
  tr1::shared_ptr<Ctemplate> validate_act_deact(const Cpath& path_comps,
                                                const char *op);
  bool validate_rename_copy(const Cpath& args, const char *op);
  bool conv_move_args_for_rename(const Cpath& args, Cpath& edit_path_comps,
                                 Cpath& rn_args);
  bool cfg_path_exists(const Cpath& path_comps, bool active_cfg,
                       bool include_deactivated);
  bool set_cfg_path(const Cpath& path_comps, bool output);
  void get_child_nodes_status(const Cpath& path_comps,
                              MapT<string, string>& cmap,
                              vector<string> *sorted_keys);
  void get_child_nodes_status_da(const Cpath& path_comps,
                                 MapT<string, string>& cmap,
                                 vector<string> *sorted_keys);

  // these operate on current work path (or active with "active_cfg")
  bool remove_tag();
  bool remove_value_from_multi(const string& value);
  bool write_value(const string& value, bool active_cfg = false) {
    vector<string> vvec(1, value);
    return write_value_vec(vvec, active_cfg);
  };
  bool add_tag(unsigned int tlimit);
  bool add_value_to_multi(unsigned int mlimit, const string& value);
  bool add_child_node(const string& name) {
    push_cfg_path(name.c_str());
    bool ret = add_node();
    pop_cfg_path();
    return ret;
  };
  void get_all_child_node_names(vector<string>& cnodes, bool active_cfg,
                                bool include_deactivated);

  // observers for work path or active path
  bool cfg_value_exists(const string& value, bool active_cfg);

  // these operate on both current tmpl and work paths
  bool validate_val(const tr1::shared_ptr<Ctemplate>& def, const char *value);
  bool create_default_children(const Cpath& path_comps); /* this requires
    * path_comps but DOES operate on current work path.
    */
  void get_edit_env(string& env);

  // util functions
  string get_shell_prompt(const string& level);
  void shell_escape_squotes(string& str);
  void print_path_vec(const char *pre, const char *post,
                      const Cpath& pvec, const char *quote);

  // output functions
  static void voutput_user(FILE *out, FILE *dout, const char *fmt,
                           va_list alist);
  static void voutput_internal(const char *fmt, va_list alist);
  static void vexit_internal(const char *fmt, va_list alist);
};

} // end namespace cstore

#endif /* _CSTORE_H_ */

