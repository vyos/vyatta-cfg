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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <algorithm>
#include <sstream>
#include <memory>

#include <apt-pkg/version.h>
#include <apt-pkg/debversion.h>

#include <cli_cstore.h>
#include <cstore/cstore.hpp>
#include <cstore/unionfs/cstore-unionfs.hpp>
#include <cstore/cstore-varref.hpp>
#include <cnode/cnode.hpp>
#include <cnode/cnode-algorithm.hpp>
#include <cparse/cparse.hpp>
#include <commit/commit-algorithm.hpp>

namespace cstore { // begin namespace cstore

using namespace cnode;

////// constants
//// node status
const string Cstore::C_NODE_STATUS_DELETED = "deleted";
const string Cstore::C_NODE_STATUS_ADDED = "added";
const string Cstore::C_NODE_STATUS_CHANGED = "changed";
const string Cstore::C_NODE_STATUS_STATIC = "static";

//// env vars for shell
// current levels
const string Cstore::C_ENV_EDIT_LEVEL = "VYATTA_EDIT_LEVEL";
const string Cstore::C_ENV_TMPL_LEVEL = "VYATTA_TEMPLATE_LEVEL";

// shell-specific vars
const string Cstore::C_ENV_SHELL_PROMPT = "PS1";
const string Cstore::C_ENV_SHELL_CWORDS = "COMP_WORDS";
const string Cstore::C_ENV_SHELL_CWORD_COUNT = "COMP_CWORD";

// shell api vars
const string Cstore::C_ENV_SHAPI_COMP_VALS = "_cli_shell_api_comp_values";
const string Cstore::C_ENV_SHAPI_LCOMP_VAL = "_cli_shell_api_last_comp_val";
const string Cstore::C_ENV_SHAPI_COMP_HELP = "_cli_shell_api_comp_help";
const string Cstore::C_ENV_SHAPI_HELP_ITEMS = "_cli_shell_api_hitems";
const string Cstore::C_ENV_SHAPI_HELP_STRS = "_cli_shell_api_hstrs";

//// dirs/files
const string Cstore::C_ENUM_SCRIPT_DIR = "/opt/vyatta/share/enumeration";
const string Cstore::C_LOGFILE_STDOUT = "/var/log/vyatta/cfg-stdout.log";

//// sorting
const unsigned int Cstore::SORT_DEFAULT = 0;
const unsigned int Cstore::SORT_DEB_VERSION = 0;
const unsigned int Cstore::SORT_NONE = 1;

////// static
bool Cstore::_init = false;
MapT<unsigned int, Cstore::SortFuncT> Cstore::_sort_func_map;


////// constructors/destructors
/* this constructor just returns the generic environment string,
 * currently the two levels. implementation-specific environment
 * (e.g., unionfs stuff) is handled by derived class.
 *
 * note: currently using original semantics for the levels, i.e., they
 *       represent the actual physical paths, which involve fs-specific
 *       escaping. this should be changed to a "logical" representation
 *       so that their manipulation can be moved from derived class to
 *       this base class.
 */
Cstore::Cstore(string& env)
{
  init();

  string decl = "declare -x ";
  env = (decl + C_ENV_EDIT_LEVEL + "=/; ");
  env += (decl + C_ENV_TMPL_LEVEL + "=/;");
}


////// factory functions
// for "current session" (see UnionfsCstore constructor for details)
Cstore *
Cstore::createCstore(bool use_edit_level)
{
  return (new unionfs::UnionfsCstore(use_edit_level));
}

// for "specific session" (see UnionfsCstore constructor for details)
Cstore *
Cstore::createCstore(const string& session_id, string& env)
{
  return (new unionfs::UnionfsCstore(session_id, env));
}


////// public interface
/* check if specified "logical path" corresponds to a valid template.
 *   validate_vals: whether to validate "values" along specified path.
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateTmplPath(const Cpath& path_comps, bool validate_vals)
{
  // if we can get parsed tmpl, path is valid
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, validate_vals));
  return (def.get() != 0);
}

/* same as above but return parsed template. return 0 if invalid/failed.
 * note: if last path component is "value" (i.e., def.is_value), parsed
 * template is actually at "full path - 1". see get_parsed_tmpl() for details.
 */
tr1::shared_ptr<Ctemplate>
Cstore::parseTmpl(const Cpath& path_comps, bool validate_vals)
{
  return get_parsed_tmpl(path_comps, validate_vals);
}

/* get parsed template of specified path as a string-string map
 *   tmap: (output) parsed template.
 * return true if successful. otherwise return false.
 */
bool
Cstore::getParsedTmpl(const Cpath& path_comps, MapT<string, string>& tmap,
                      bool allow_val)
{
  /* currently this function is used outside actual CLI operations, mainly
   * from the perl API. since value validation is from the original CLI
   * implementation, it doesn't seem to behave correctly in such cases,
   * probably because "at string" is not set?
   *
   * anyway, not validating values in the following call.
   */
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false));
  if (!def.get()) {
    return false;
  }
  if (!allow_val && def->isValue()) {
    /* note: !allow_val means specified path must terminate at an actual
     *       "node", not a "value". so this fails since path ends in value.
     *       this emulates the original perl API behavior.
     */
    return false;
  }
  if (def->isValue()) {
    tmap["is_value"] = "1";
  }
  // make the map
  if (!def->isTypeless(1)) {
    tmap["type"] = def->getTypeName(1);
  }
  if (!def->isTypeless(2)) {
    tmap["type2"] = def->getTypeName(2);
  }
  if (def->getNodeHelp()) {
    tmap["help"] = def->getNodeHelp();
  }
  if (def->isMulti()) {
    tmap["multi"] = "1";
    if (def->getMultiLimit() > 0) {
      ostringstream s;
      s << def->getMultiLimit();
      tmap["limit"] = s.str();
    }
  } else if (def->isTag()) {
    tmap["tag"] = "1";
    if (def->getTagLimit() > 0) {
      ostringstream s;
      s << def->getTagLimit();
      tmap["limit"] = s.str();
    }
  } else if (def->getDefault()) {
    tmap["default"] = def->getDefault();
  }
  if (def->getEnumeration()) {
    tmap["enum"] = def->getEnumeration();
  }
  if (def->getAllowed()) {
    tmap["allowed"] = def->getAllowed();
  }
  if (def->getValHelp()) {
    tmap["val_help"] = def->getValHelp();
  }
  return true;
}

/* get names of all template child nodes of specified path.
 *   cnodes: (output) template child node names.
 * note: if specified path is at a "tag node", "node.tag" will be returned.
 */
void
Cstore::tmplGetChildNodes(const Cpath& path_comps,
                          vector<string>& cnodes)
{
  unique_ptr<SavePaths> save(create_save_paths());
  append_tmpl_path(path_comps);
  get_all_tmpl_child_node_names(cnodes);
  sort_nodes(cnodes);
}

/* delete specified "logical path" from "working config".
 * return true if successful. otherwise return false.
 */
bool
Cstore::deleteCfgPath(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false, terr));
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }

  if (!cfg_path_exists(path_comps, false, true)) {
    output_user("Nothing to delete (the specified %s does not exist)\n",
                (!def->isValue() || def->isTag()) ? "node" : "value");
    // treat as success
    return true;
  }

  /* path already validated and in working config.
   * cases:
   *   1. has default value
   *      => replace current value with default
   *   2. no default value
   *      => remove config path
   */
  if (def->getDefault()) {
    // case 1. construct path for value file.
    unique_ptr<SavePaths> save(create_save_paths());
    append_cfg_path(path_comps);
    if (def->isValue()) {
      // last comp is "value". need to go up 1 level.
      pop_cfg_path();
    }

    /* assume default value is valid (parser should have validated).
     * also call unmark_deactivated() in case the node being deleted was
     * also deactivated. note that unmark_deactivated() succeeds if it's
     * not marked deactivated. also mark "changed".
     */
    if (!(write_value(def->getDefault()) && mark_display_default()
          && unmark_deactivated() && mark_changed_with_ancestors())) {
      output_user("Failed to set default value during delete\n");
      return false;
    }
    return true;
  }

  /* case 2.
   * sub-cases:
   *   (1) last path comp is "value", i.e., tag (value of tag node),
   *       value of single-value node, or value of multi-value node.
   *       (a) value of single-value node
   *           => remove node
   *       (b) value of multi-value node
   *           => remove value. remove node if last value.
   *       (c) value of tag node (i.e., tag)
   *           => remove tag. remove node if last tag.
   *   (2) last path comp is "node", i.e., typeless node, tag node,
   *       single-value node, or multi-value node.
   *       => remove node
   */
  bool ret = false;
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  if (!def->isValue()) {
    // sub-case (2)
    ret = remove_node();
  } else {
    // last comp is value
    if (def->isTag()) {
      // sub-case (1c)
      ret = remove_tag();
    } else if (def->isMulti()) {
      // sub-case (1b)
      pop_cfg_path();
      ret = remove_value_from_multi(path_comps[path_comps.size() - 1]);
    } else {
      // sub-case (1a). delete node at 1 level up.
      pop_cfg_path();
      ret = remove_node();
    }
  }
  if (ret) {
    // mark changed
    ret = mark_changed_with_ancestors();
  }
  if (!ret) {
    output_user("Failed to delete specified config path\n");
  }
  return ret;
}

/* check if specified "logical path" is valid for "set" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateSetPath(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  // if we can get parsed tmpl, path is valid
  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, true, terr));
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }

  unique_ptr<SavePaths> save(create_save_paths());
  if (!def->isValue()) {
    if (!def->isTypeless()) {
      /* disallow setting value node without value
       * note: different from old behavior, which only disallow setting a
       *       single-value node without value. now all value nodes
       *       (single-value, multi-value, and tag) must be set with value.
       */
      string output = "Configuration path: ["+path_comps.to_string()+"] requires a value\n";
      output_user(output.c_str());
      return false;
    } else {
      /* typeless node
       * note: XXX the following is present in the original logic, perhaps
       *       to trigger check_syn() on the typeless node? is this really
       *       necessary?
       *       also, validate_val() uses current cfg path and tmpl path, so
       *       construct them before calling it.
       */
      append_cfg_path(path_comps);
      append_tmpl_path(path_comps);
      if (!validate_val(def, "")) {
        return false;
      }
    }
  }
  return true;
}

/* check if specified "logical path" is valid for "activate" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateActivatePath(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  tr1::shared_ptr<Ctemplate> def(validate_act_deact(path_comps, "activate"));
  if (!def.get()) {
    return false;
  }
  if (!cfgPathMarkedDeactivated(path_comps)) {
    output_user("Activate can only be performed on a node on which the "
                "deactivate\ncommand has been performed.\n");
    return false;
  }

  if (def->isTagValue() && def->getTagLimit() > 0) {
    // we are activating a tag, and there is a limit on number of tags.
    vector<string> cnodes;
    unique_ptr<SavePaths> save(create_save_paths());
    append_cfg_path(path_comps);
    string t;
    pop_cfg_path(t);
    // get child nodes, excluding deactivated ones.
    get_all_child_node_names(cnodes, false, false);
    if (def->getTagLimit() <= cnodes.size()) {
      // limit exceeded
      output_user("Cannot activate \"%s\": number of values exceeds limit "
                  "(%d allowed)\n", t.c_str(), def->getTagLimit());
      return false;
    }
  }
  return true;
}

/* check if specified "logical path" is valid for "deactivate" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateDeactivatePath(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  tr1::shared_ptr<Ctemplate> def(validate_act_deact(path_comps, "deactivate"));
  return (def.get() != 0);
}

/* check if specified "logical path" is valid for "edit" operation.
 * return false if invalid.
 * if valid, set "env" arg to the environment string needed for the "edit"
 * operation and return true.
 */
bool
Cstore::getEditEnv(const Cpath& path_comps, string& env)
{
  ASSERT_IN_SESSION;

  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false, terr));
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }
  /* "edit" is only allowed when path ends at a
   *   (1) "tag value"
   *   OR
   *   (2) "typeless node"
   */
  if (!def->isTagValue() && !def->isTypeless()) {
    // neither "tag value" nor "typeless node"
    output_user("The \"edit\" command cannot be issued "
                "at the specified level\n");
    return false;
  }
  if (!cfg_path_exists(path_comps, false, true)) {
    /* specified path does not exist.
     * follow the original implementation and do a "set".
     */
    if (!validateSetPath(path_comps)) {
      string output = "Configuration path: ["+path_comps.to_string()+"] is not valid\n";
      output_user(output.c_str());
      return false;
    }
    if (!set_cfg_path(path_comps, false)) {
      output_user("Failed to create the specified config path\n");
      return false;
    }
  }
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  append_tmpl_path(path_comps);
  get_edit_env(env);
  /* doing the save/restore above to be consistent with the rest of the API.
   * however, after the caller evals the returned environment string, the
   * levels in "this" will become out-of-sync with the environment. so
   * "this" should no longer be used and a new object should be created.
   *
   * this is only an issue if the calling process doesn't terminate. since
   * the function should only be used by the shell/completion, it's not a
   * problem (each invocation of my_cli_shell_api uses a new object anyway).
   */

  return true;
}

/* set "env" arg to the environment string needed for the "up" operation.
 * return true if successful.
 */
bool
Cstore::getEditUpEnv(string& env)
{
  ASSERT_IN_SESSION;

  /* "up" is based on current levels in environment. levels should already
   * be set up in constructor (with "use_edit_level" true).
   */
  if (edit_level_at_root()) {
    output_user("Already at the top level\n");
    return false;
  }

  string terr;
  Cpath path_comps;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false, terr));
  if (!def.get()) {
    // this should not happen since it's using existing levels
    output_user("%s\n", terr.c_str());
    return false;
  }
  unique_ptr<SavePaths> save(create_save_paths());
  if (def->isTagValue()) {
    // edit level is at "tag value". go up 1 extra level.
    pop_cfg_path();
    pop_tmpl_path();
  }
  pop_cfg_path();
  pop_tmpl_path();
  get_edit_env(env);
  // also see getEditEnv for comment on save/restore above

  return true;
}

/* set "env" arg to the environment string needed to reset edit levels.
 * return true if successful.
 */
bool
Cstore::getEditResetEnv(string& env)
{
  ASSERT_IN_SESSION;

  unique_ptr<SavePaths> save(create_save_paths());
  while (!edit_level_at_root()) {
    pop_cfg_path();
    pop_tmpl_path();
  }
  get_edit_env(env);
  // also see getEditEnv for comment on save/restore above

  return true;
}

void
Cstore::getAllowedVarRef(string& astr){
  size_t varloc = 0;
  size_t strloc = 0;
  vtw_type_e vtype = ERROR_TYPE;
  string exe_string = ""; 
  string varref = ""; 
  size_t first_paren, second_paren;
  while(true) {
    varloc = astr.find("$VAR", strloc); 
    if (varloc == string::npos){
      varloc = astr.size();
      exe_string += astr.substr(strloc, (varloc-strloc+1));
      break;
    }   
    exe_string += astr.substr(strloc, (varloc-strloc));
    strloc = varloc;
    first_paren = astr.find('(', strloc);
    second_paren = astr.find(')', strloc);
    varref = astr.substr((first_paren+1), (second_paren-first_paren-1));
    varref = string(getVarRef(varref.c_str(), vtype, false));
    strloc = second_paren+1;
    exe_string += varref;
  }
  astr = exe_string;
  return;
}

/* set "env" arg to the environment string needed for "completion".
 * return true if successful.
 *
 * note: comps must have at least 2 components, the "command" and the
 *       first path element (which can be empty string).
 */
bool
Cstore::getCompletionEnv(const Cpath& comps, string& env)
{
  ASSERT_IN_SESSION;

  string cmd = comps[0];
  string last_comp = comps.back();
  Cpath pcomps;
  for (size_t i = 1; i < (comps.size() - 1); i++) {
    pcomps.push(comps[i]);
  }

  bool exists_only = (cmd == "delete" || cmd == "show"
                      || cmd == "comment" || cmd == "activate"
                      || cmd == "deactivate");

  /* at this point, pcomps contains the command line arguments minus the
   * "command" and the last one.
   */
  unique_ptr<SavePaths> save(create_save_paths());
  bool is_typeless = true;
  bool is_leaf_value = false;
  bool is_value = false;
  tr1::shared_ptr<Ctemplate> def;
  if (pcomps.size() > 0) {
    def = get_parsed_tmpl(pcomps, false);
    if (!def.get()) {
      // invalid path
      return false;
    }
    if (exists_only && !cfg_path_exists(pcomps, false, true)) {
      // invalid path for the command (must exist)
      return false;
    }
    append_cfg_path(pcomps);
    append_tmpl_path(pcomps);
    is_typeless = def->isTypeless();
    is_leaf_value = def->isLeafValue();
    is_value = def->isValue();
  } else {
    /* we are at root. default values simulate a typeless node so nop.
     * note that in this case def is "empty", so must ensure that it's
     * not used.
     */
  }

  /* at this point, cfg and tmpl paths are constructed up to the comp
   * before last_comp, and def is parsed.
   */
  if (is_leaf_value) {
    // invalid path (this means the comp before last_comp is a leaf value)
    return false;
  }

  vector<string> comp_vals;
  string comp_string;
  string comp_help;
  vector<pair<string, string> > help_pairs;
  bool last_comp_val = true;
  if (is_typeless || is_value) {
    /* path so far is at a typeless node OR a tag value (tag already
     * checked above):
     *   completions: from tmpl children.
     *   help:
     *     values: same as completions.
     *     text: "help" from child templates.
     *
     * note: for such completions, we filter non-existent nodes if
     *       necessary.
     *
     * also, the "root" node case above will reach this block, so
     * must not use def in this block.
     */
    vector<string> ufvec;
    if (exists_only) {
      // only return existing config nodes
      get_all_child_node_names(ufvec, false, true);
    } else {
      // return all template children
      get_all_tmpl_child_node_names(ufvec);
    }
    for (size_t i = 0; i < ufvec.size(); i++) {
      if (last_comp == ""
          || ufvec[i].compare(0, last_comp.length(), last_comp) == 0) {
        comp_vals.push_back(ufvec[i]);
      }
    }
    if (comp_vals.size() == 0) {
      // no matches
      return false;
    }
    sort(comp_vals.begin(), comp_vals.end());

    /* loop below calls get_parsed_tmpl(), which takes the whole path.
     * so need to save current paths and reset them before (and restore them
     * after).
     */
    unique_ptr<SavePaths> save1(create_save_paths());
    reset_paths();
    for (size_t i = 0; i < comp_vals.size(); i++) {
      pair<string, string> hpair(comp_vals[i], "");
      pcomps.push(comp_vals[i]);
      tr1::shared_ptr<Ctemplate> cdef(get_parsed_tmpl(pcomps, false));
      if (cdef.get() && cdef->getNodeHelp()) {
        hpair.second = cdef->getNodeHelp();
      } else {
        hpair.second = "<No help text available>";
      }
      help_pairs.push_back(hpair);
      pcomps.pop();
    }
    // last comp is not value
    last_comp_val = false;
  } else {
    /* path so far is at a "value node".
     * note: follow the original implementation and don't filter
     *       non-existent values for such completions
     *
     * also, cannot be "root" node if we reach here, so def can be used.
     */
    // first, handle completions.
    if (def->isTag()) {
      // it's a "tag node". get completions from tag values.
      get_all_child_node_names(comp_vals, false, true);
    } else {
      // it's a "leaf value node". get completions from values.
      read_value_vec(comp_vals, false);
    }
    /* more possible completions from this node's template:
     *   "allowed"
     *   "enumeration"
     *   "$VAR(@) in ..."
     */
    if (!exists_only && (def->getEnumeration() || def->getAllowed())) {
      /* do "enumeration" or "allowed".
       * note: emulate original implementation and set up COMP_WORDS and
       *       COMP_CWORD environment variables. these are needed by some
       *       "allowed" scripts.
       */
      ostringstream cword_count;
      cword_count << (comps.size() - 1);
      string cmd_str = ("export " + C_ENV_SHELL_CWORD_COUNT + "="
                         + cword_count.str() + "; ");
      cmd_str += ("export " + C_ENV_SHELL_CWORDS + "=(");
      for (size_t i = 0; i < comps.size(); i++) {
        cmd_str += " '";
        cmd_str += comps[i];
        cmd_str += "'";
      }
      cmd_str += "); ";
      if (def->getEnumeration()) {
        cmd_str += (C_ENUM_SCRIPT_DIR + "/" + def->getEnumeration());
      } else {
        string astr = def->getAllowed();
        shell_escape_squotes(astr);
        getAllowedVarRef(astr);
        cmd_str += "_cstore_internal_allowed () { eval '";
        cmd_str += astr;
        cmd_str += "'; }; _cstore_internal_allowed";
      }

      char *buf = (char *) malloc(MAX_CMD_OUTPUT_SIZE);
      int ret = get_shell_command_output(cmd_str.c_str(), buf,
                                         MAX_CMD_OUTPUT_SIZE);
      if (ret > 0) {
        // '<' and '>' need to be escaped
        char *ptr = buf;
        while (*ptr) {
          if (*ptr == '<' || *ptr == '>') {
            comp_string += "\\";
          }
          comp_string += *ptr;
          ptr++;
        }
      }
      /* note that for "enumeration" and "allowed", comp_string is the
       * complete output of the command and it is to be evaled by the
       * shell into an array of values.
       */
      free(buf);
    } else if (!exists_only && def->getActions(syntax_act)) {
      // look for "self ref in values" from syntax
      const valstruct *vals
        = get_syntax_self_in_valstruct(def->getActions(syntax_act));
      if (vals) {
        if (vals->cnt == 0 && vals->val) {
          comp_vals.push_back(vals->val);
        } else if (vals->cnt > 0) {
          for (int i = 0; i < vals->cnt; i++) {
            if (vals->vals[i]) {
              comp_vals.push_back(vals->vals[i]);
            }
          }
        }
      }
    }

    // now handle help.
    if (def->getCompHelp()) {
      // "comp_help" exists.
      comp_help = def->getCompHelp();
      shell_escape_squotes(comp_help);
    }
    if (def->getValHelp()) {
      // has val_help. first separate individual lines.
      size_t start = 0, i = 0;
      vector<string> vhelps;
      for (i = 0; (def->getValHelp())[i]; i++) {
        if ((def->getValHelp())[i] == '\n') {
          vhelps.push_back(string(&((def->getValHelp())[start]), i - start));
          start = i + 1;
        }
      }
      if (start < i) {
        vhelps.push_back(string(&((def->getValHelp())[start]), i - start));
      }

      // process each line
      for (i = 0; i < vhelps.size(); i++) {
        size_t sc;
        if ((sc = vhelps[i].find(';')) == vhelps[i].npos) {
          // no ';'
          if (i == 0 && !def->isTypeless(1)) {
            // first val_help. pair with "type".
            help_pairs.push_back(pair<string, string>(
                                   def->getTypeName(1), vhelps[i]));
          }
          if (i == 1 && !def->isTypeless(2)) {
            // second val_help. pair with second "type".
            help_pairs.push_back(pair<string, string>(
                                   def->getTypeName(2), vhelps[i]));
          }
        } else {
          // ';' at index sc
          help_pairs.push_back(pair<string, string>(
                                 vhelps[i].substr(0, sc),
                                 vhelps[i].substr(sc + 1)));
        }
      }
    } else if (!def->isTypeless(1) && def->getNodeHelp()) {
      // simple case. just use "type" and "help"
      help_pairs.push_back(pair<string, string>(def->getTypeName(1),
                                                def->getNodeHelp()));
    }
  }

  /* from this point on cannot use def (since the "root" node case
   * can reach here).
   */

  // this var is the array of possible completions
  env = (C_ENV_SHAPI_COMP_VALS + "=(");
  for (size_t i = 0; i < comp_vals.size(); i++) {
    shell_escape_squotes(comp_vals[i]);
    env += ("'" + comp_vals[i] + "' ");
  }
  /* as mentioned above, comp_string is the complete command output.
   * let the shell eval it into the array since we don't want to
   * re-implement the shell interpretation here.
   *
   * note that as a result, we will not be doing the filtering here.
   * instead, the completion script will do the filtering on
   * the resulting comp_values array. should be straightforward since
   * there's no "existence filtering", only "prefix filtering".
   */
  env += (comp_string + "); ");

  /* this var indicates whether the last comp is "value"
   * follow original implementation: if last comp is value, completion
   * script needs to do the following.
   *   use comp_help if exists
   *   prefix filter comp_values
   *   replace any <*> in comp_values with ""
   *   convert help items to data representation
   */
  env += (C_ENV_SHAPI_LCOMP_VAL + "=");
  env += (last_comp_val ? "true; " : "false; ");

  // this var is the "comp_help" string
  env += (C_ENV_SHAPI_COMP_HELP + "='" + comp_help + "'; ");

  // this var is the array of "help items", i.e., type names, etc.
  string hitems = (C_ENV_SHAPI_HELP_ITEMS + "=(");
  // this var is the array of "help strings" corresponding to the items
  string hstrs = (C_ENV_SHAPI_HELP_STRS + "=(");
  for (size_t i = 0; i < help_pairs.size(); i++) {
    string hi = help_pairs[i].first;
    string hs = help_pairs[i].second;
    shell_escape_squotes(hi);
    shell_escape_squotes(hs);
    // get rid of leading/trailing "space" chars in help string
    while (hi.size() > 0 && isspace(hi[0])) {
      hi.erase(0, 1);
    }
    while (hs.size() > 0 && isspace(hs[0])) {
      hs.erase(0, 1);
    }
    while (hi.size() > 0 && isspace(hi[hi.size() - 1])) {
      hi.erase(hi.size() - 1);
    }
    while (hs.size() > 0 && isspace(hs[hs.size() - 1])) {
      hs.erase(hs.size() - 1);
    }
    hitems += ("'" + hi + "' ");
    hstrs += ("'" + hs + "' ");
  }
  env += (hitems + "); " + hstrs + "); ");
  return true;
}

/* set specified "logical path" in "working config".
 * return true if successful. otherwise return false.
 * note: assume specified path is valid (i.e., validateSetPath()).
 */
bool
Cstore::setCfgPath(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  return set_cfg_path(path_comps, true);
}

/* check if specified "arguments" is valid for "rename" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateRenameArgs(const Cpath& args)
{
  ASSERT_IN_SESSION;

  return validate_rename_copy(args, "rename");
}

/* check if specified "arguments" is valid for "copy" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateCopyArgs(const Cpath& args)
{
  ASSERT_IN_SESSION;

  return validate_rename_copy(args, "copy");
}

/* check if specified "arguments" is valid for "move" operation
 * return true if valid. otherwise return false.
 */
bool
Cstore::validateMoveArgs(const Cpath& args)
{
  ASSERT_IN_SESSION;

  Cpath epath;
  Cpath nargs;
  if (!conv_move_args_for_rename(args, epath, nargs)) {
    output_user("Invalid move command\n");
    return false;
  }
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(epath);
  append_tmpl_path(epath);
  return validate_rename_copy(nargs, "move");
}

/* perform rename in "working config" according to specified args.
 * return true if successful. otherwise return false.
 * note: assume args are already validated (i.e., validateRenameArgs()).
 */
bool
Cstore::renameCfgPath(const Cpath& args)
{
  ASSERT_IN_SESSION;

  const char *otagnode = args[0];
  const char *otagval = args[1];
  const char *ntagval = args[4];
  unique_ptr<SavePaths> save(create_save_paths());
  push_cfg_path(otagnode);
  if (!rename_child_node(otagval, ntagval)) {
    return false;
  }
  /* also mark the new "tag value" changed since one possible scenario is that
   * the "new" tag value was there before but is being deleted, and something
   * else is being renamed to the same tag value. one side effect of this is
   * that if the subtree under the new tag value is completely identical
   * before and after this delete/rename sequence, then it will be marked
   * "changed" even though nothing changed.
   */
  push_cfg_path(ntagval);
  return mark_changed_with_ancestors();
}

/* perform copy in "working config" according to specified args.
 * return true if successful. otherwise return false.
 * note: assume args are already validated (i.e., validateCopyArgs()).
 */
bool
Cstore::copyCfgPath(const Cpath& args)
{
  ASSERT_IN_SESSION;

  const char *otagnode = args[0];
  const char *otagval = args[1];
  const char *ntagval = args[4];
  push_cfg_path(otagnode);
  /* also mark changed. note that it's marking the "tag node" but not the
   * new "tag value" since it is being "added" anyway.
   */
  bool ret = (copy_child_node(otagval, ntagval)
              && mark_changed_with_ancestors());
  pop_cfg_path();
  return ret;
}

/* perform "comment" in working config according to specified args.
 * return true if valid. otherwise return false.
 */
bool
Cstore::commentCfgPath(const Cpath& args)
{
  ASSERT_IN_SESSION;

  /* separate path from comment.
   * follow the original implementation: the last arg is the comment, and
   * everything else is part of the path.
   */
  Cpath path_comps(args);
  string comment;
  path_comps.pop(comment);

  // check the path
  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false, terr));
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }
  // here we want to include deactivated nodes
  if (!cfg_path_exists(path_comps, false, true)) {
    output_user("The specified config node does not exist\n");
    return false;
  }
  if (def->isLeafValue()) {
    /* XXX differ from the original implementation, which allows commenting
     *     on a "value" BUT silently "promote" the comment to the parent
     *     "node". this will probably create confusion for the user.
     *
     *     just disallow such cases here.
     */
    output_user("Cannot comment on config values\n");
    return false;
  }
  if (def->isTagNode()) {
    /* XXX follow original implementation and disallow comment on a
     *     "tag node". this is because "show" does not display such
     *     comments (see bug 5794).
     */
    output_user("Cannot add comment at this level\n");
    return false;
  }
  if (comment.find_first_of('*') != string::npos) {
    // don't allow '*'. this is due to config files using C-style /**/
    // comments. this probably belongs to lower-level, but we are enforcing
    // it here.
    output_user("Cannot use the '*' character in a comment\n");
    return false;
  }
  if (comment.find("CONFIGURATION COMMENTED OUT DURING MIGRATION BELOW") != string::npos){
    // Don't allow users to set configuration migration comments
    return false;
  }
  if (comment.find("CONFIGURATION COMMENTED OUT DURING MIGRATION ABOVE") != string::npos){
    // Don't allow users to set configuration migration comments
    return false;
  }

  bool ret = false;
  {
    unique_ptr<SavePaths> save(create_save_paths());
    append_cfg_path(path_comps);
    if (comment == "") {
      // follow original impl: empty comment => remove it
      ret = remove_comment();
      if (!ret) {
        output_user("Failed to remove comment for specified config node\n");
      }
    } else {
      ret = set_comment(comment);
      if (!ret) {
        output_user("Failed to add comment for specified config node\n");
      }
    }
  }
  if (ret) {
    // mark the root as changed for "comment"
    ret = mark_changed_with_ancestors();
  }
  return ret;
}

/* discard all changes in working config.
 * return true if successful. otherwise return false.
 */
bool
Cstore::discardChanges()
{
  ASSERT_IN_SESSION;

  // just call underlying implementation
  unsigned long long num_removed = 0;
  if (discard_changes(num_removed)) {
    if (num_removed > 0) {
      output_user("Changes have been discarded\n");
    } else {
      output_user("No changes have been discarded\n");
    }
    return true;
  }
  return false;
}

/* perform move in "working config" according to specified args.
 * return true if successful. otherwise return false.
 * note: assume args are already validated (i.e., validateMoveArgs()).
 */
bool
Cstore::moveCfgPath(const Cpath& args)
{
  ASSERT_IN_SESSION;

  Cpath epath;
  Cpath nargs;
  if (!conv_move_args_for_rename(args, epath, nargs)) {
    output_user("Invalid move command\n");
    return false;
  }
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(epath);
  append_tmpl_path(epath);
  return renameCfgPath(nargs);
}

/* check if specified "logical path" exists in working config (i.e., the union)
 * or active config (i.e., the original).
 * return true if it exists. otherwise return false.
 */
bool
Cstore::cfgPathExists(const Cpath& path_comps, bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  return cfg_path_exists(path_comps, active_cfg, false);
}

// same as above but "deactivate-aware" 
bool
Cstore::cfgPathExistsDA(const Cpath& path_comps, bool active_cfg,
                        bool include_deactivated)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  return cfg_path_exists(path_comps, active_cfg, include_deactivated);
}

/* check if specified "logical path" has been deleted in working config.
 */
bool
Cstore::cfgPathDeleted(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  // whether it's in active but not in working
  return (cfg_path_exists(path_comps, true, false)
          && !cfg_path_exists(path_comps, false, false));
}

/* check if specified "logical path" has been added in working config.
 */
bool
Cstore::cfgPathAdded(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  // whether it's not in active but in working
  return (!cfg_path_exists(path_comps, true, false)
          && cfg_path_exists(path_comps, false, false));
}

/* check if specified "logical path" has been "changed" in working config.
 * XXX the definition of "changed" is different from the original
 *     perl API implementation isChanged(), which was inconsistent between
 *     "deleted" and "deactivated".
 *
 *     original logic (with $disable arg not defined) returns true in
 *     either of the 2 cases below:
 *       (1) node is BEING deactivated or activated
 *       (2) node appears in changes_only dir
 *     which means it returns false for nodes being deleted but true
 *     for nodes being deactivated.
 *
 *     new logic returns true if any of the following is true
 *     (remember this functions is NOT "deactivate-aware")
 *       (1) cfgPathDeleted()
 *       (2) cfgPathAdded()
 *       (3) cfg_node_changed()
 */
bool
Cstore::cfgPathChanged(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  if (cfgPathDeleted(path_comps) || cfgPathAdded(path_comps)) {
    return true;
  }
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return cfg_node_changed();
}

/* get names of "deleted" child nodes of specified path during commit
 * operation. names are returned in cnodes.
 */
void
Cstore::cfgPathGetDeletedChildNodes(const Cpath& path_comps,
                                    vector<string>& cnodes)
{
  ASSERT_IN_SESSION;

  cfgPathGetDeletedChildNodesDA(path_comps, cnodes, false);
}

// same as above but "deactivate-aware"
void
Cstore::cfgPathGetDeletedChildNodesDA(const Cpath& path_comps,
                                      vector<string>& cnodes,
                                      bool include_deactivated)
{
  ASSERT_IN_SESSION;

  vector<string> acnodes;
  cfgPathGetChildNodesDA(path_comps, acnodes, true, include_deactivated);
  vector<string> wcnodes;
  cfgPathGetChildNodesDA(path_comps, wcnodes, false, include_deactivated);
  MapT<string, bool> cmap;
  for (size_t i = 0; i < wcnodes.size(); i++) {
    cmap[wcnodes[i]] = true;
  }
  for (size_t i = 0; i < acnodes.size(); i++) {
    if (cmap.find(acnodes[i]) == cmap.end()) {
      // in active but not in working
      cnodes.push_back(acnodes[i]);
    }
  }
  sort_nodes(cnodes);
}

/* get "deleted" values of specified "multi node" during commit
 * operation. values are returned in dvals. if specified path is not
 * a "multi node", it's a nop.
 *
 * NOTE: this function does not consider the "value ordering". the "deleted"
 *       status is purely based on the presence/absence of a value.
 */
void
Cstore::cfgPathGetDeletedValues(const Cpath& path_comps, vector<string>& dvals)
{
  ASSERT_IN_SESSION;

  cfgPathGetDeletedValuesDA(path_comps, dvals, false);
}

// same as above but DA
void
Cstore::cfgPathGetDeletedValuesDA(const Cpath& path_comps,
                                  vector<string>& dvals,
                                  bool include_deactivated)
{
  ASSERT_IN_SESSION;

  vector<string> ovals;
  vector<string> nvals;
  if (!cfgPathGetValuesDA(path_comps, ovals, true, include_deactivated)
      || !cfgPathGetValuesDA(path_comps, nvals, false, include_deactivated)) {
    return;
  }
  MapT<string, bool> dmap;
  for (size_t i = 0; i < nvals.size(); i++) {
    dmap[nvals[i]] = true;
  }
  for (size_t i = 0; i < ovals.size(); i++) {
    if (dmap.find(ovals[i]) == dmap.end()) {
      // in active but not in working
      dvals.push_back(ovals[i]);
    }
  }
}

/* check whether specified path is "deactivated" in working config or
 * active config.
 * a node is "deactivated" if the node itself or any of its ancestors is
 * "marked deactivated".
 */
bool
Cstore::cfgPathDeactivated(const Cpath& path_comps, bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  Cpath ppath;
  for (size_t i = 0; i < path_comps.size(); i++) {
    ppath.push(path_comps[i]);
    if (cfgPathMarkedDeactivated(ppath, active_cfg)) {
      // an ancestor or itself is marked deactivated
      return true;
    }
  }
  return false;
}

/* check whether specified path is "marked deactivated" in working config or
 * active config.
 * a node is "marked deactivated" if a deactivate operation has been
 * performed on the node.
 */
bool
Cstore::cfgPathMarkedDeactivated(const Cpath& path_comps, bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return marked_deactivated(active_cfg);
}

/* get names of child nodes of specified path in working config or active
 * config. names are returned in cnodes.
 */
void
Cstore::cfgPathGetChildNodes(const Cpath& path_comps, vector<string>& cnodes,
                             bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  cfgPathGetChildNodesDA(path_comps, cnodes, active_cfg, false);
}

// same as above but "deactivate-aware" 
void
Cstore::cfgPathGetChildNodesDA(const Cpath& path_comps, vector<string>& cnodes,
                               bool active_cfg, bool include_deactivated)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  if (!include_deactivated && cfgPathDeactivated(path_comps, active_cfg)) {
    /* this node is deactivated (an ancestor or this node itself is
     * marked deactivated) and we don't want to include deactivated. nop.
     */
    return;
  }
  {
    unique_ptr<SavePaths> save(create_save_paths());
    append_cfg_path(path_comps);
    get_all_child_node_names(cnodes, active_cfg, include_deactivated);
  }
  sort_nodes(cnodes);
}

/* get value of specified single-value node.
 *   value: (output) node value.
 *   active_cfg: whether to get value from active config.
 * return false if fails (invalid node, doesn't exist, read fails, etc.).
 * otherwise return true.
 */
bool
Cstore::cfgPathGetValue(const Cpath& path_comps, string& value,
                        bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  return cfgPathGetValueDA(path_comps, value, active_cfg, false);
}

// same as above but "deactivate-aware" 
bool
Cstore::cfgPathGetValueDA(const Cpath& path_comps, string& value,
                          bool active_cfg, bool include_deactivated)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false));
  if (!def.get()) {
    // invalid node
    return false;
  }
  /* note: the behavior here is different from original perl API, which
   *       does not check if specified node is indeed single-value. so if
   *       the function is erroneously used on a multi-value node, the
   *       original API will return a single string that includes all values.
   *       this new function will return failure in such cases.
   */
  if (!def->isSingleLeafNode()) {
    // specified path is not a single-value node
    return false;
  }
  if (!cfg_path_exists(path_comps, active_cfg, include_deactivated)) {
    // specified node doesn't exist
    return false;
  }
  vector<string> vvec;
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  if (read_value_vec(vvec, active_cfg)) {
    if (vvec.size() >= 1) {
      // if for some reason we got multiple values, just take the first one.
      value = vvec[0];
      return true;
    }
  }
  return false;
}

/* get values of specified multi-value node.
 *   values: (output) node values.
 *   active_cfg: whether to get values from active config.
 * return false if fails (invalid node, doesn't exist, etc.).
 * otherwise return true.
 */
bool
Cstore::cfgPathGetValues(const Cpath& path_comps, vector<string>& values,
                         bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  return cfgPathGetValuesDA(path_comps, values, active_cfg, false);
}

// same as above but "deactivate-aware" 
bool
Cstore::cfgPathGetValuesDA(const Cpath& path_comps, vector<string>& values,
                           bool active_cfg, bool include_deactivated)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false));
  if (!def.get()) {
    // invalid node
    return false;
  }
  /* note: the behavior here is different from original perl API, which
   *       does not check if specified node is indeed multi-value. so if
   *       the function is erroneously used on a single-value node, the
   *       original API will return the node's value. this new function
   *       will return failure in such cases.
   */
  if (!def->isMultiLeafNode()) {
    // specified path is not a multi-value node
    return false;
  }
  if (!cfg_path_exists(path_comps, active_cfg, include_deactivated)) {
    // specified node doesn't exist
    return false;
  }
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return read_value_vec(values, active_cfg);
}

/* get comment of specified node.
 *   comment: (output) node comment.
 *   active_cfg: whether to get comment from active config.
 * return false if fails (invalid node, doesn't exist, etc.).
 * otherwise return true.
 */
bool
Cstore::cfgPathGetComment(const Cpath& path_comps, string& comment,
                          bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return get_comment(comment, active_cfg);
}

/* return whether specified path is "default". if a node is "default", it
 * is currently not shown by the "show" command unless "-all" is specified.
 *   active_cfg: whether to observe active config.
 */
bool
Cstore::cfgPathDefault(const Cpath& path_comps, bool active_cfg)
{
  if (!active_cfg) {
    ASSERT_IN_SESSION;
  }

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return marked_display_default(active_cfg);
}

/* the following functions are observers of the "effective" config.
 * they can be used
 *   (1) outside a config session (e.g., op mode, daemons, callbacks, etc.).
 *   OR
 *   (2) during a config session
 *
 * HOWEVER, NOTE that the definition of "effective" is different under these
 * two scenarios.
 *   (1) when used outside a config session, "effective" == "active".
 *       in other words, in such cases the effective config is the same
 *       as the running config.
 *
 *   (2) when used during a config session, a config path (leading to either
 *       a "node" or a "value") is "effective" if ANY of the following
 *       is true.
 *         (a) active && working
 *             path is in both active and working configs, i.e., unchanged.
 *         (b) !active && working && committed
 *             path is not in active, has been set in working, AND has
 *             already been committed, i.e., "commit" has successfully
 *             processed the addition/update of the path.
 *         (c) active && !working && !committed
 *             path is in active, has been deleted from working, AND
 *             has not been committed yet, i.e., "commit" (per priority) has
 *             not processed the deletion of the path yet, or it has been
 *             processed but failed.
 *
 *       note: during commit, deactivate has the same effect as delete. so
 *             in such cases, as far as these functions are concerned,
 *             deactivated nodes don't exist.
 *
 * originally, these functions are exclusively for use during config
 * sessions. however, for some usage scenarios, it is useful to have a set
 * of API functions that can be used both during and outside config
 * sessions. therefore, definition (1) is added above for convenience.
 *
 * for example, a developer can use these functions in a script that can
 * be used both during a commit action and outside config mode, as long as
 * the developer is clearly aware of the difference between the above two
 * definitions.
 *
 * note that when used outside a config session (i.e., definition (1)),
 * these functions are equivalent to the observers for the "active" config.
 *
 * to avoid any confusion, when possible (e.g., in a script that is
 * exclusively used in op mode), developers should probably use those
 * "active" observers explicitly when outside a config session instead
 * of these "effective" observers.
 *
 * it is also important to note that when used outside a config session,
 * due to race conditions, it is possible that the "observed" active config
 * becomes out-of-sync with the config that is actually "in effect".
 * specifically, this happens when two things occur simultaneously:
 *   (a) an observer function is called from outside a config session.
 *   AND
 *   (b) someone invokes "commit" inside a config session (any session).
 *
 * this is because "commit" only updates the active config at the end after
 * all commit actions have been executed, so before the update happens,
 * some config nodes have already become "effective" but are not yet in the
 * "active config" and therefore are not observed by these functions.
 *
 * note that this is only a problem when the caller is outside config mode.
 * in such cases, the caller (which could be an op-mode command, a daemon,
 * a callback script, etc.) already must be able to handle config changes
 * that can happen at any time. if "what's configured" is more important,
 * using the "active config" should be fine as long as it is relatively
 * up-to-date. if the actual "system state" is more important, then the
 * caller should probably just check the system state in the first place
 * (instead of using these config observers).
 *
 * one possible solution is for these "effective" observers to obtain the
 * global commit lock before returning their observations. this has not
 * been implemented yet since the impact of this issue is not clear at
 * the moment.
 */

// return whether specified path is "effective".
bool
Cstore::cfgPathEffective(const Cpath& path_comps)
{
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false));
  if (!def.get()) {
    // invalid path
    return false;
  }

  bool in_active = cfg_path_exists(path_comps, true, false);
  if (!inSession()) {
    // not in a config session. use active config only.
    return in_active;
  }

  bool in_work = cfg_path_exists(path_comps, false, false);
  return commit::isCommitPathEffective(*this, path_comps, def,
                                       in_active, in_work);
}

/* get names of "effective" child nodes of specified path during commit
 * operation. see above function for definition of "effective".
 * names are returned in cnodes.
 */
void
Cstore::cfgPathGetEffectiveChildNodes(const Cpath& path_comps,
                                      vector<string>& cnodes)
{
  if (!inSession()) {
    // not in a config session. use active config only.
    cfgPathGetChildNodes(path_comps, cnodes, true);
    return;
  }

  // get a union of active and working
  MapT<string, bool> cmap;
  vector<string> acnodes;
  vector<string> wcnodes;
  cfgPathGetChildNodes(path_comps, acnodes, true);
  cfgPathGetChildNodes(path_comps, wcnodes, false);
  for (size_t i = 0; i < acnodes.size(); i++) {
    cmap[acnodes[i]] = true;
  }
  for (size_t i = 0; i < wcnodes.size(); i++) {
    cmap[wcnodes[i]] = true;
  }

  // get only the effective ones from the union
  Cpath ppath(path_comps);
  MapT<string, bool>::iterator it = cmap.begin();
  for (; it != cmap.end(); ++it) {
    string c = (*it).first;
    ppath.push(c);
    if (cfgPathEffective(ppath)) {
      cnodes.push_back(c);
    }
    ppath.pop();
  }
  sort_nodes(cnodes);
}

/* get the "effective" value of specified path during commit operation.
 *   value: (output) node value
 * return true if successful. otherwise return false.
 */
bool
Cstore::cfgPathGetEffectiveValue(const Cpath& path_comps, string& value)
{
  if (!inSession()) {
    // not in a config session. use active config only.
    return cfgPathGetValue(path_comps, value, true);
  }

  Cpath ppath(path_comps);
  string oval, nval;
  bool oret = cfgPathGetValue(path_comps, oval, true);
  bool nret = cfgPathGetValue(path_comps, nval, false);
  bool ret = false;
  // all 4 combinations of oret and nret are covered below
  if (nret) {
    // got new value
    ppath.push(nval);
    if (cfgPathEffective(ppath)) {
      // nval already effective
      value = nval;
      ret = true;
    } else if (!oret) {
      // no oval. failure.
    } else {
      // oval still effective
      value = oval;
      ret = true;
    }
  } else if (oret) {
    // got oval only
    ppath.push(oval);
    if (cfgPathEffective(ppath)) {
      // oval still effective
      value = oval;
      ret = true;
    }
  }
  return ret;
}

/* get the "effective" values of specified path during commit operation.
 *   values: (output) node values
 * return true if successful. otherwise return false.
 */
bool
Cstore::cfgPathGetEffectiveValues(const Cpath& path_comps,
                                  vector<string>& values)
{
  if (!inSession()) {
    // not in a config session. use active config only.
    cfgPathGetValues(path_comps, values, true);
    return (values.size() > 0);
  }

  // get a union of active and working
  MapT<string, bool> vmap;
  vector<string> ovals;
  vector<string> nvals;
  cfgPathGetValues(path_comps, ovals, true);
  cfgPathGetValues(path_comps, nvals, false);
  for (size_t i = 0; i < ovals.size(); i++) {
    vmap[ovals[i]] = true;
  }
  for (size_t i = 0; i < nvals.size(); i++) {
    vmap[nvals[i]] = true;
  }

  // get only the effective ones from the union
  Cpath ppath(path_comps);
  MapT<string, bool>::iterator it = vmap.begin();
  for (; it != vmap.end(); ++it) {
    string c = (*it).first;
    ppath.push(c);
    if (cfgPathEffective(ppath)) {
      values.push_back(c);
    }
    ppath.pop();
  }
  return (values.size() > 0);
}

/* get the value string that corresponds to specified variable ref string.
 *   ref_str: var ref string (e.g., "./cost/@").
 *   type: (output) the node type.
 *   from_active: if true, value string should come from "active config".
 *                otherwise from "working config".
 * return a pointer to the value string if successful (caller must free).
 * otherwise return NULL.
 */
char *
Cstore::getVarRef(const char *ref_str, vtw_type_e& type, bool from_active)
{
  unique_ptr<SavePaths> save(create_save_paths());
  VarRef vref(this, ref_str, from_active);
  string val;
  vtw_type_e t;
  if (vref.getValue(val, t)) {
    type = t;
    // follow original implementation. caller is supposed to free this.
    return strdup(val.c_str());
  }
  return NULL;
}

/* set the node corresponding to specified variable ref string to specified
 * value.
 *   ref_str: var ref string (e.g., "../encrypted-password/@").
 *   value: value to be set.
 *   to_active: if true, set in "active config".
 *              otherwise in "working config".
 * return true if successful. otherwise return false.
 */
bool
Cstore::setVarRef(const char *ref_str, const char *value, bool to_active)
{
  /* XXX functions in cli_new only performs "set var ref" operations (e.g.,
   *     '$VAR(@) = ""', which sets current node's value to empty string)
   *     during "commit", i.e., if a "set var ref" is specified in
   *     "syntax:", it will not be performed during "set" (but will be
   *     during commit).
   *
   * XXX also the behavior here follows the original implementation and
   *     has these limitations:
   *     * it does not check if the type of the specified value is
   *       correct, e.g., it may write a txt value to a u32 node if
   *       that's what the template specifies.
   *     * it only supports only single-value leaf nodes.
   */
  unique_ptr<SavePaths> save(create_save_paths());
  VarRef vref(this, ref_str, to_active);
  Cpath pcomps;
  if (vref.getSetPath(pcomps)) {
    reset_paths();
    tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(pcomps, false));
    if (def.get() && def->isSingleLeafNode()) {
      // currently only support single-value node
      append_cfg_path(pcomps);
      if (write_value(value, to_active)) {
        return true;
      }
    }
  }
  return false;
}

/* perform deactivate operation on a node, i.e., make the node
 * "marked deactivated".
 * note: assume all validations have been peformed (see activate.cpp).
 *       also, when marking a node as deactivated, all of its descendants
 *       that had been marked deactivated are unmarked.
 */
bool
Cstore::markCfgPathDeactivated(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  if (cfgPathDeactivated(path_comps)) {
    output_user("The specified configuration node is already deactivated\n");
    // treat as success
    return true;
  }

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  // note: also mark changed
  return (mark_deactivated() && unmark_deactivated_descendants()
          && mark_changed_with_ancestors());
}

/* perform activate operation on a node, i.e., make the node no longer
 * "marked deactivated".
 * note: assume all validations have been peformed (see activate.cpp).
 */
bool
Cstore::unmarkCfgPathDeactivated(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  // note: also mark changed
  return (unmark_deactivated() && mark_changed_with_ancestors());
}

// load specified config file
bool
Cstore::loadFile(const char *filename)
{
  if (!inSession()) {
    output_user("Cannot load config outside configuration session\n");
    // exit handled by assert below
  }
  ASSERT_IN_SESSION;

  FILE *fin = fopen(filename, "r");
  if (!fin) {
    output_user("Failed to open specified config file\n");
    return false;
  }

  // get the config tree from the file
  CfgNode *froot = cparse::parse_file(fin, *this);
  fclose(fin);
  if (!froot) {
    output_user("Failed to parse specified config file\n");
    return false;
  }

  // get the config tree from the active config
  Cpath args;
  CfgNode aroot(*this, args, true, true);

  // get the "commands diff" between the two
  vector<Cpath> del_list;
  vector<Cpath> set_list;
  vector<Cpath> com_list;
  get_cmds_diff(aroot, *froot, del_list, set_list, com_list);

  delete froot;
  // "apply" the changes to the working config
  for (size_t i = 0; i < del_list.size(); i++) {
    if (!deleteCfgPath(del_list[i])) {
      print_path_vec("Delete [", "] failed\n", del_list[i], "'");
    }
  }
  for (size_t i = 0; i < set_list.size(); i++) {
    if (!validateSetPath(set_list[i]) || !setCfgPath(set_list[i])) {
      print_path_vec("Set [", "] failed\n", set_list[i], "'");
    }
  }
  for (size_t i = 0; i < com_list.size(); i++) {
    if (!commentCfgPath(com_list[i])) {
      string comment = string(com_list[i][com_list[i].size()-1]);
      if (comment.find("CONFIGURATION COMMENTED OUT DURING MIGRATION BELOW") == string::npos
       && comment.find("CONFIGURATION COMMENTED OUT DURING MIGRATION ABOVE") == string::npos) {
        print_path_vec("Comment [", "] failed\n", com_list[i], "'");
      }
    }
  }

  return true;
}

/* "changed" status handling.
 * the "changed" status is used during commit to check if a node has been
 * changed. note that if a node is "changed", all of its ancestors are also
 * considered changed (this follows the original logic).
 *
 * the original backend implementation only uses the "changed" marker at
 * "root" to indicate whether the whole config has changed. for the rest
 * of the config hierarchy, the original implementation treated all nodes
 * that are present in the unionfs "changes only" directory as "changed".
 *
 * this worked until the introduction of "deactivate". since deactivated
 * nodes are also present in the "changes only" directory, the backend
 * treat them as "changed". on the other hand, deleted nodes don't appear
 * in "changes only", so they are _not_ treated as "changed". this creates
 * problems in various parts of the backend.
 *
 * the new CLI backend/library "marks" all changed nodes explicitly, and the
 * "changed" status depends on such markers. the marking is done using the
 * pure virtual mark_changed_with_ancestors() function, which is provided
 * by the low-level implementation, so it does not have to be done as a
 * "per-node file marker" as long as the low-level implementation can
 * correctly answer the "changed" query for a given path.
 *
 * note that "changed" nodes does not include "added" and "deleted" nodes.
 * for the convenience of implementation, the backend must always query
 * for "changed" nodes *after* "added" and "deleted" nodes. in other
 * words, the backend will only treat a node as "changed" if it is neither
 * "added" nor "deleted". currently there are only two places that perform
 * changed status query: cfgPathGetChildNodesStatus() and
 * cfgPathGetChildNodesStatusDA(). see those two functions for the usage.
 *
 * what this means is that the backend can choose to either mark or not
 * mark "added"/"deleted" nodes as "changed" at its convenience. for
 * example, "set" and "delete" always do the marking, but "rename" and
 * "copy" do not.
 *
 * changed status queries are provided by the cfg_node_changed() function,
 * and changed markers can be removed by unmarkCfgPathChanged() below (used
 * by "commit").
 */

/* unmark "changed" status of specified path in working config.
 * this is used, e.g., at the end of "commit" to reset a subtree.
 * note: unmarking a node means all of its descendants are also unmarked,
 *       i.e., they become "unchanged".
 * return true if successful. otherwise return false.
 */
bool
Cstore::unmarkCfgPathChanged(const Cpath& path_comps)
{
  ASSERT_IN_SESSION;

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return unmark_changed_with_descendants();
}

// execute the specified actions
bool
Cstore::executeTmplActions(char *at_str, const Cpath& path,
                           const Cpath& disp_path, const vtw_node *actions,
                           const vtw_def *def)
{
  string sdisp = " ";
  sdisp += disp_path.to_string();
  sdisp += " ";
  set_at_string(at_str);

  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path);
  append_tmpl_path(path);

  var_ref_handle = (void *) this;
  // const_cast for legacy code
  bool ret = execute_list(const_cast<vtw_node *>(actions), def,
                          sdisp.c_str());
  var_ref_handle = NULL;
  return ret;
}

bool
Cstore::cfgPathMarkedCommitted(const Cpath& path_comps, bool is_delete)
{
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return marked_committed(is_delete);
}

bool
Cstore::markCfgPathCommitted(const Cpath& path_comps, bool is_delete)
{
  unique_ptr<SavePaths> save(create_save_paths());
  append_cfg_path(path_comps);
  return mark_committed(is_delete);
}


////// protected functions
Cstore::SavePaths::~SavePaths() {
}

void
Cstore::output_user(const char *fmt, ...)
{
  va_list alist;
  va_start(alist, fmt);
  voutput_user(out_stream, stdout, fmt, alist);
  va_end(alist);
}

void
Cstore::output_user_err(const char *fmt, ...)
{
  va_list alist;
  va_start(alist, fmt);
  voutput_user(err_stream, stderr, fmt, alist);
  va_end(alist);
}

void
Cstore::output_internal(const char *fmt, ...)
{
  va_list alist;
  va_start(alist, fmt);
  voutput_internal(fmt, alist);
  va_end(alist);
}

void
Cstore::exit_internal(const char *fmt, ...)
{
  va_list alist;
  va_start(alist, fmt);
  vexit_internal(fmt, alist);
  va_end(alist);
}

void
Cstore::assert_internal(bool cond, const char *fmt, ...)
{
  if (cond) {
    return;
  }

  va_list alist;
  va_start(alist, fmt);
  vexit_internal(fmt, alist);
  va_end(alist);
}


////// private functions
bool
Cstore::sort_func_deb_version(string a, string b)
{
  return debVS.CmpVersion(a, b) < 0;
}

void
Cstore::sort_nodes(vector<string>& nvec, unsigned int sort_alg)
{
  MapT<unsigned int, Cstore::SortFuncT>::iterator p
    = _sort_func_map.find(sort_alg);
  if (p == _sort_func_map.end()) {
    return;
  }
  sort(nvec.begin(), nvec.end(), p->second);
}

/* try to append the logical path to template path.
 *   is_tag: (output) whether the last component is a "tag".
 * return false if logical path is not valid. otherwise return true.
 *
 * note: if the last comp is already "node.tag", is_tag won't be set.
 *       currently this should only happen when get_parsed_tmpl() "borrows"
 *       comps from the current tmpl path, in which case this is not
 *       a problem.
 */
bool
Cstore::append_tmpl_path(const Cpath& path_comps, bool& is_tag)
{
  for (size_t i = 0; i < path_comps.size(); i++) {
    is_tag = false;
    push_tmpl_path(path_comps[i]);
    if (tmpl_node_exists()) {
      // got exact match. continue to next component.
      continue;
    }
    // not exact match. check if it's a tag.
    pop_tmpl_path();
    push_tmpl_path_tag();
    if (tmpl_node_exists()) {
      // got tag match. continue to next component.
      is_tag = true;
      continue;
    }
    // not a valid path
    return false;
  }
  return true;
}

typedef MapT<Cpath, tr1::shared_ptr<Ctemplate>, CpathHash> TmplCacheT;
static TmplCacheT _tmpl_cache;

/* check whether specified "logical path" is valid template path.
 * then template at the path is parsed.
 *   path_comps: path components.
 *   validate_vals: whether to validate all "values" along specified path.
 *   error: (output) error message if failed.
 * return parsed template if successful. otherwise return 0.
 * note:
 *   also, if last path component is value (i.e., isValue()), the template
 *   parsed is actually at "full path - 1".
 */
tr1::shared_ptr<Ctemplate>
Cstore::get_parsed_tmpl(const Cpath& path_comps, bool validate_vals,
                        string& error)
{
  tr1::shared_ptr<Ctemplate> rtmpl;
  // default error message
  error = "Configuration path: ["+path_comps.to_string()+"] is not valid\n";

  bool do_caching = false;
  if (tmpl_path_at_root()) {
    if (path_comps.size() == 0) {
      // empty path not valid
      return rtmpl;
    }
    // we are starting from root => caching applies
    do_caching = true;
    TmplCacheT::iterator p = _tmpl_cache.find(path_comps);
    if (p != _tmpl_cache.end()) {
      // return cached
      return p->second;
    }
  }

  unique_ptr<SavePaths> save(create_save_paths());

  /* need at least 1 comp to work. 2 comps if last comp is value.
   * so pop tmpl_path and prepend them. note that path_comps remain
   * constant.
   */
  Cpath *pcomps = const_cast<Cpath *>(&path_comps);
  Cpath new_path_comps;
  size_t p_size = path_comps.size();
  if (p_size < 2) {
    Cpath tmp;
    for (unsigned int i = 0; i < 2 && (i + p_size) < 2; i++) {
      if (!tmpl_path_at_root()) {
        string last;
        pop_tmpl_path(last);
        tmp.push(last);
      }
    }
    while (tmp.size() > 0) {
      new_path_comps.push(tmp.back());
      tmp.pop();
    }
    new_path_comps /= path_comps;
    pcomps = &new_path_comps;
  }
  do {
    /* cases for template path:
     * (1) valid path ending in "actual node", i.e., typeless node, tag node,
     *     single-value node, or multi-value node:
     *     => tmpl at full path. e.g.:
     *        typeless node:     "service ssh allow-root"
     *        tag node:          "interfaces ethernet"
     *        single-value node: "system gateway-address"
     *        multi-value node:  "system name-server"
     * (2) valid path ending in "value", i.e., tag (value of tag node), or
     *     value of single-/multi-value node:
     *     => tmpl at "full path - 1". e.g.:
     *        "value" of tag node:        "interfaces ethernet eth0"
     *        value of single-value node: "system gateway-address 1.1.1.1"
     *        value of multi-value node:  "system name-server 2.2.2.2"
     * (3) invalid path
     *     => no tmpl
     */
    // first scan up to "full path - 1"
    bool valid = true;
    for (size_t i = 0; i < (pcomps->size() - 1); i++) {
      if ((*pcomps)[i][0] == 0) {
        // only the last component is potentially allowed to be empty str
        valid = false;
        break;
      }
      bool is_tag;
      if (append_tmpl_path((*pcomps)[i], is_tag)) {
        if (is_tag && validate_vals) {
          /* last comp is tag and want to validate value.
           * note: validate_val() will use the current tmpl path and cfg path.
           *       so need both at the "node" level before calling it.
           *       at this point cfg path is not pushed yet so no need to
           *       pop it.
           */
          pop_tmpl_path();
          tr1::shared_ptr<Ctemplate> ttmpl(tmpl_parse());
          if (!validate_val(ttmpl, (*pcomps)[i])) {
            // invalid value
            error = "Value validation failed";
            valid = false;
            break;
          }
          // restore tmpl path
          append_tmpl_path((*pcomps)[i], is_tag);
        }
        /* cfg path is not used here but is needed by validate_val(), so
         * need to keep it in sync with tmpl path.
         */
        push_cfg_path((*pcomps)[i]);
      } else {
        // not a valid path
        valid = false;
        break;
      }
    }
    if (!valid) {
      // case (3)
      break;
    }
    /* we are valid up to "full path - 1". now process last path component.
     * first, if we are case (2), we should find a "value node" at this point.
     * note: this is only possible if there are more than 1 component. otherwise
     * we haven't done anything yet.
     */
    if (pcomps->size() > 1) {
      tr1::shared_ptr<Ctemplate> ttmpl(tmpl_parse());
      if (ttmpl.get()) {
        if (ttmpl->isTag() || ttmpl->isMulti() || !ttmpl->isTypeless()) {
          // case (2). last component is "value".
          if (validate_vals) {
            // validate value
            if (!validate_val(ttmpl, (*pcomps)[pcomps->size() - 1])) {
              // invalid value
              error = "Value validation failed";
              break;
            }
          }
          rtmpl = ttmpl;
          rtmpl->setIsValue(true);
          break;
        }
      }
      // if no valid template or not a value, it's not case (2) so continue.
    }
    // now check last component
    if ((*pcomps)[pcomps->size() - 1][0] == 0) {
      // only value is potentially allowed to be empty str
      break;
    }
    push_tmpl_path((*pcomps)[pcomps->size() - 1]);
    // no need to push cfg path (only needed for validate_val())
    if (tmpl_node_exists()) {
      // case (1). last component is "node".
      rtmpl.reset(tmpl_parse());
      if (!rtmpl.get()) {
        exit_internal("get_parsed_tmpl: failed to parse tmpl [%s]\n",
                      tmpl_path_to_str().c_str());
      }
      rtmpl->setIsValue(false);
      break;
    }
    // case (3) (fall through)
  } while (0);

  if (do_caching && rtmpl.get()) {
    // only cache if we got a valid template
    _tmpl_cache[path_comps] = rtmpl;
  }
  return rtmpl;
}

/* check if specified "logical path" is valid for "activate" or
 * "deactivate" operation.
 * return parsed template if valid. otherwise return 0.
 */
tr1::shared_ptr<Ctemplate>
Cstore::validate_act_deact(const Cpath& path_comps, const char *op)
{
  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(path_comps, false, terr));
  tr1::shared_ptr<Ctemplate> none;
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return none;
  }
  {
    /* XXX this is a temporary workaround for bug 5708, which should be
     *     addressed _after_ the "default value"-related issues have been
     *     resolved (see bug for more details). once those are resolved,
     *     this workaround should be removed and the bug fixed properly.
     */
    if (!def->isTag() && !def->isTypeless()) {
      output_user("Cannot %s a leaf configuration node\n", op);
      return none;
    }
  }
  if (def->isLeafValue()) {
    /* last component is a value of a single- or multi-value node (i.e.,
     * a leaf value) => not allowed
     */
    output_user("Cannot %s a leaf configuration value\n", op);
    return none;
  }
  if (!cfg_path_exists(path_comps, false, true)) {
    output_user("Nothing to %s (the specified %s does not exist)\n",
                op, (!def->isValue() || def->isTag()) ? "node" : "value");
    return none;
  }
  return def;
}

/* check if specified args is valid for "rename" or "copy" operation.
 * return true if valid. otherwise return false.
 */
bool
Cstore::validate_rename_copy(const Cpath& args, const char *op)
{
  if (args.size() != 5 || strcmp(args[2], "to") != 0) {
    output_user("Invalid %s command\n", op);
    return false;
  }
  const char *otagnode = args[0];
  const char *otagval = args[1];
  const char *ntagnode = args[3];
  const char *ntagval = args[4];
  if (strcmp(otagnode, ntagnode) != 0) {
    output_user("Cannot %s from \"%s\" to \"%s\"\n", op, otagnode, ntagnode);
    return false;
  }

  // check the old path
  Cpath ppath;
  ppath.push(otagnode);
  ppath.push(otagval);
  string terr;
  tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(ppath, false, terr));
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }
  if (!def->isTagValue()) {
    // can only rename "tagnode tagvalue"
    output_user("Cannot %s under \"%s\"\n", op, otagnode);
    return false;
  }
  if (!cfg_path_exists(ppath, false, true)) {
    output_user("Configuration \"%s %s\" does not exist\n", otagnode, otagval);
    return false;
  }

  // check the new path
  ppath.pop();
  ppath.push(ntagval);
  if (cfg_path_exists(ppath, false, true)) {
    output_user("Configuration \"%s %s\" already exists\n", ntagnode, ntagval);
    return false;
  }
  def = get_parsed_tmpl(ppath, true, terr);
  if (!def.get()) {
    output_user("%s\n", terr.c_str());
    return false;
  }
  return true;
}

// convert args for "move" to be used for equivalent "rename" operation
bool
Cstore::conv_move_args_for_rename(const Cpath& args, Cpath& edit_path_comps,
                                  Cpath& rn_args)
{
  /* note:
   *   "move interfaces ethernet eth2 vif 100 to 200"
   *   is equivalent to
   *   "edit interfaces ethernet eth2" + "rename vif 100 to vif 200"
   *
   *   set the extra levels and then just validate as rename
   */
  size_t num_args = args.size();
  if (num_args < 4) {
    // need at least 4 args
    return false;
  }
  for (size_t i = 0; i < (num_args - 4); i++) {
    edit_path_comps.push(args[i]);
  }
  rn_args.push(args[num_args - 4]); // vif
  rn_args.push(args[num_args - 3]); // 100
  rn_args.push(args[num_args - 2]); // to
  rn_args.push(args[num_args - 4]); // vif
  rn_args.push(args[num_args - 1]); // 200
  return true;
}

/* check if specified "logical path" exists in working config or
 * active config.
 *   v_def: ptr to parsed template. NULL if none.
 * return true if it exists. otherwise return false.
 */
bool
Cstore::cfg_path_exists(const Cpath& path_comps, bool active_cfg,
                        bool include_deactivated)
{
  bool ret = false;
  {
    unique_ptr<SavePaths> save(create_save_paths());
    append_cfg_path(path_comps);
    // first check if it's a "node".
    ret = cfg_node_exists(active_cfg);
    if (!ret) {
      // doesn't exist as a node. maybe a value?
      pop_cfg_path();
      ret = cfg_value_exists(path_comps[path_comps.size() - 1], active_cfg);
    }
  }
  if (ret && !include_deactivated
      && cfgPathDeactivated(path_comps, active_cfg)) {
    // don't include deactivated
    ret = false;
  }
  return ret;
}

/* set specified "logical path" in "working config".
 *   output: whether to generate output
 * return true if successful. otherwise return false.
 * note: assume specified path is valid (i.e., validateSetPath()).
 */
bool
Cstore::set_cfg_path(const Cpath& path_comps, bool output)
{
  Cpath ppath;
  tr1::shared_ptr<Ctemplate> def;
  bool ret = true;
  bool path_exists = true;
  // do the set from the top down
  for (size_t i = 0; i < path_comps.size(); i++) {
    // partial path
    ppath.push(path_comps[i]);

    // get template at this level
    def = get_parsed_tmpl(ppath, false);
    if (!def.get()) {
      output_internal("paths[%s,%s]\n", cfg_path_to_str().c_str(),
                      tmpl_path_to_str().c_str());
      for (size_t i = 0; i < ppath.size(); i++) {
        output_internal("  [%s]\n", ppath[i]);
      }
      exit_internal("failed to get tmpl during set. not validate first?\n");
    }

    // nop if this level already in working (including deactivated)
    if (cfg_path_exists(ppath, false, true)) {
      continue;
    }

    // paths have not been changed up to this point. now save them.
    unique_ptr<SavePaths> save(create_save_paths());

    path_exists = false;

    // this level not in working. set it.
    append_cfg_path(ppath);
    append_tmpl_path(ppath);

    if (!def->isValue()) {
      // this level is a "node"
      if (!add_node()) {
        ret = false;
        break;
      }
      if (!def->isTag() && !create_default_children(ppath)) {
        // failed to create default child nodes for a typeless node
        ret = false;
        break;
      }
    } else if (def->isTag()) {
      // this level is a "tag value".
      // add the tag, taking the max tag limit into consideration.
      if (!add_tag(def->getTagLimit()) || !create_default_children(ppath)) {
        ret = false;
        break;
      }
    } else {
      // this level is a "value" of a single-/multi-value node.
      // go up 1 level to get the node.
      pop_cfg_path();
      if (def->isMulti()) {
        // value of multi-value node.
        // add the value, taking the max multi limit into consideration.
        if (!add_value_to_multi(def->getMultiLimit(), ppath.back())) {
          ret = false;
          break;
        }
      } else {
        // value of single-value node
        if (!write_value(ppath.back())) {
          ret = false;
          break;
        }
      }
    }
    if (!mark_changed_with_ancestors()) {
      ret = false;
      break;
    }
  }

  if (ret && def->isValue() && def->getDefault()) {
    unique_ptr<SavePaths> save(create_save_paths());
    /* a node with default has been explicitly set. needs to be marked
     * as non-default for display purposes.
     *
     * note: when the new value is the same as the old value, the behavior
     *       is different from before, which was a nop. the new behavior is
     *       that the value will remain unchanged, but the "default status"
     *       will be changed, i.e., it will be marked as non-default.
     */
    append_cfg_path(path_comps);
    pop_cfg_path();
    // only do it if it's previously marked default
    if (marked_display_default(false)) {
      if ((ret = unmark_display_default())) {
        /* XXX work around current commit's unionfs implementation problem.
         * current commit's unionfs implementation looks at the "changes
         * only" directory (i.e., the r/w portion of the union mount), which
         * is wrong.
         *
         * all config information should be obtained from two directories:
         * "active" and "working", e.g., instead of looking at whiteout
         * files in "changes only" to find deleted nodes, nodes that are in
         * "active" but not in "working" have been deleted.
         *
         * in this particular case, commit looks at "changes only" to read
         * the node.val file. however, since the value didn't change (only
         * the "default status" changed), node.val doesn't appear in
         * "changes only". here we re-write the file to force it into
         * "changes only" so that commit can work correctly.
         */
        vector<string> vvec;
        read_value_vec(vvec, false);
        write_value_vec(vvec);

        // pretend it didn't exist since we changed the status
        path_exists = false;
        // also mark changed
        ret = mark_changed_with_ancestors();
      }
    }
  }
  if (path_exists) {
    // whole path already exists
    if (output) {
      string userout = "Configuration path: ["+path_comps.to_string()+"] already exists\n";
      output_user(userout.c_str());
    }
    // treat as success
  }
  return ret;
}

/* this is the equivalent of the listNodeStatus() from the original
 * perl API. it provides the "status" ("deleted", "added", "changed",
 * or "static") of each child node of specified path.
 *   cmap: (output) contains the status of child nodes.
 *   sorted_keys: (output) contains sorted keys. call with NULL if not needed.
 *
 * note: this function is NOT "deactivate-aware".
 */
void
Cstore::get_child_nodes_status(const Cpath& path_comps,
                               MapT<string, string>& cmap,
                               vector<string> *sorted_keys)
{
  // get a union of active and working
  MapT<string, bool> umap;
  vector<string> acnodes;
  vector<string> wcnodes;
  cfgPathGetChildNodes(path_comps, acnodes, true);
  cfgPathGetChildNodes(path_comps, wcnodes, false);
  for (size_t i = 0; i < acnodes.size(); i++) {
    umap[acnodes[i]] = true;
  }
  for (size_t i = 0; i < wcnodes.size(); i++) {
    umap[wcnodes[i]] = true;
  }

  // get the status of each one
  Cpath ppath(path_comps);
  MapT<string, bool>::iterator it = umap.begin();
  for (; it != umap.end(); ++it) {
    string c = (*it).first;
    ppath.push(c);
    if (sorted_keys) {
      sorted_keys->push_back(c);
    }
    // note: "changed" includes "deleted" and "added", so check those first.
    if (cfgPathDeleted(ppath)) {
      cmap[c] = C_NODE_STATUS_DELETED;
    } else if (cfgPathAdded(ppath)) {
      cmap[c] = C_NODE_STATUS_ADDED;
    } else if (cfgPathChanged(ppath)) {
      cmap[c] = C_NODE_STATUS_CHANGED;
    } else {
      cmap[c] = C_NODE_STATUS_STATIC;
    }
    ppath.pop();
  }
  if (sorted_keys) {
    sort_nodes(*sorted_keys);
  }
}

/* DA version of the above function.
 *   cmap: (output) contains the status of child nodes.
 *   sorted_keys: (output) contains sorted keys. call with NULL if not needed.
 *
 * note: this follows the original perl API listNodeStatus() implementation.
 */
void
Cstore::get_child_nodes_status_da(const Cpath& path_comps,
                                  MapT<string, string>& cmap,
                                  vector<string> *sorted_keys)
{
  // process deleted nodes first
  vector<string> del_nodes;
  cfgPathGetDeletedChildNodesDA(path_comps, del_nodes);
  for (size_t i = 0; i < del_nodes.size(); i++) {
    if (sorted_keys) {
      sorted_keys->push_back(del_nodes[i]);
    }
    cmap[del_nodes[i]] = C_NODE_STATUS_DELETED;
  }

  // get all nodes in working config
  vector<string> work_nodes;
  cfgPathGetChildNodesDA(path_comps, work_nodes, false);
  Cpath ppath(path_comps);
  for (size_t i = 0; i < work_nodes.size(); i++) {
    ppath.push(work_nodes[i]);
    if (sorted_keys) {
      sorted_keys->push_back(work_nodes[i]);
    }
    /* note: in the DA version here, we do NOT check the deactivate state
     *       when considering the state of the child nodes (which include
     *       deactivated ones). the reason is that this DA function is used
     *       for config output-related operations and should return whether
     *       each node is actually added/deleted from the config independent
     *       of its deactivate state.
     *
     *       for "added" state, can't use cfgPathAdded() since it's not DA.
     *
     *       for "changed" state, can't use cfgPathChanged() since it's not DA.
     *
     *       deleted ones already handled above.
     */
    if (!cfg_path_exists(ppath, true, true)
        && cfg_path_exists(ppath, false, true)) {
      cmap[work_nodes[i]] = C_NODE_STATUS_ADDED;
    } else {
      unique_ptr<SavePaths> save(create_save_paths());
      append_cfg_path(ppath);
      if (cfg_node_changed()) {
        cmap[work_nodes[i]] = C_NODE_STATUS_CHANGED;
      } else {
        cmap[work_nodes[i]] = C_NODE_STATUS_STATIC;
      }
    }

    ppath.pop();
  }
  if (sorted_keys) {
    sort_nodes(*sorted_keys);
  }
}

/* remove tag at current work path and its subtree.
 * if specified tag is the last one, also remove the tag node.
 * return true if successful. otherwise return false.
 * note: assume current work path is a tag.
 */
bool
Cstore::remove_tag()
{
  if (!remove_node()) {
    return false;
  }

  // go up one level and check if that was the last tag
  bool ret = true;
  string c;
  pop_cfg_path(c);
  vector<string> cnodes;
  // get child nodes, including deactivated ones.
  get_all_child_node_names(cnodes, false, true);
  if (cnodes.size() == 0) {
    // it was the last tag. remove the node as well.
    if (!remove_node()) {
      ret = false;
    }
  }
  push_cfg_path(c.c_str());
  return ret;
}

/* remove specified value from the multi-value node at current work path.
 * if specified value is the last one, also remove the multi-value node.
 * return true if successful. otherwise return false.
 * note: assume current work path is a multi-value node and specified value is
 *       configured for the node.
 */
bool
Cstore::remove_value_from_multi(const string& value)
{
  // get current values
  vector<string> vvec;
  if (!read_value_vec(vvec, false)) {
    return false;
  }

  // remove the value
  size_t bc = vvec.size();
  vector<string> nvec(vvec.begin(), remove(vvec.begin(), vvec.end(), value));
  size_t ac = nvec.size();

  // sanity check
  if (ac == bc) {
    // nothing removed
    return false;
  }
  if (ac == 0) {
    // was the last value. remove the node.
    return remove_node();
  } else {
    // write the new values out
    return write_value_vec(nvec);
  }
}

/* check whether specified value exists at current work path.
 * note: assume current work path is a value node.
 */
bool
Cstore::cfg_value_exists(const string& value, bool active_cfg)
{
  // get current values
  vector<string> vvec;
  if (!read_value_vec(vvec, active_cfg)) {
    return false;
  }

  return (find(vvec.begin(), vvec.end(), value) != vvec.end());
}

/* validate value at current template path.
 *   def: pointer to parsed template.
 *   val: value to be validated.
 * return true if valid. otherwise return false.
 * note: current template and cfg paths both point to the node,
 *       not the value.
 */
bool
Cstore::validate_val(const tr1::shared_ptr<Ctemplate>& def, const char *value)
{
  if (!def.get()) {
    exit_internal("validate_val: no tmpl [%s]\n", tmpl_path_to_str().c_str());
  }

  // validate_value() may change "value". make a copy first.
  unique_ptr<char> vbuf(strdup(value));

  /* set the handle to be used during validate_value() for var ref
   * processing. this is a global var in cli_new.c.
   */
  var_ref_handle = (void *) this;
  bool ret = validate_value(def->getDef(), vbuf.get());
  var_ref_handle = NULL;

  return ret;
}

/* add tag at current work path.
 * return true if successful. otherwise return false.
 * note: assume current work path is a new tag and path from root to parent
 *       already exists.
 */
bool
Cstore::add_tag(unsigned int tlimit)
{
  string t;
  pop_cfg_path(t);
  vector<string> cnodes;
  // get child nodes, excluding deactivated ones.
  get_all_child_node_names(cnodes, false, false);
  bool ret = false;
  do {
    if (tlimit > 0 && tlimit <= cnodes.size()) {
      // limit exceeded
      output_user("Cannot set node \"%s\": number of values exceeds limit"
                  "(%d allowed)\n", t.c_str(), tlimit);
      break;
    }
    /* XXX the original implementation contains special case where the
     *     previous tag should be replaced. this is probably unnecessary since
     *     "rename" can be used for tag node anyway. also the implementation
     *     used -1 as the limit for the special case, which can't work since
     *     the limit is unsigned. ignore the special case for now.
     */
    // neither of the above. just add the tag.
    ret = add_child_node(t);
  } while (0);
  push_cfg_path(t.c_str());
  return ret;
}

/* add specified value to the multi-value node at current work path.
 * return true if successful. otherwise return false.
 * note: assume current work path is a multi-value node and specified value is
 *       not configured for the node.
 */
bool
Cstore::add_value_to_multi(unsigned int mlimit, const string& value)
{
  // get current values
  vector<string> vvec;
  // ignore return value here. if it failed, vvec is empty.
  read_value_vec(vvec, false);

  /* note: XXX the original limit-checking logic uses the same count as tag
   *       node, which is wrong since multi-node values are not stored as
   *       directories in the original implementation.
   *
   *       also, original logic only applies limit when def_multi > 1.
   *       this was probably to support the special case in the design
   *       when def_multi is 1 to make it behave like a single-value
   *       node (i.e., a subsequent set replaces the value). however,
   *       the implementation uses "-1" as the special case (but def_multi
   *       is unsigned anyway). see also "add_tag()".
   *
   *       for now just apply the limit for anything >= 1.
   */
  if (mlimit >= 1 && vvec.size() >= mlimit) {
    // limit exceeded
    output_user("Cannot set value \"%s\": number of values exceeded "
                "(%d allowed)\n", value.c_str(), mlimit);
    return false;
  }

  // append the value
  vvec.push_back(value);
  return write_value_vec(vvec);
}

/* this uses the get_all_child_node_names_impl() from the underlying
 * implementation but provides the option to exclude deactivated nodes.
 */
void
Cstore::get_all_child_node_names(vector<string>& cnodes, bool active_cfg,
                                 bool include_deactivated)
{
  vector<string> nodes;
  get_all_child_node_names_impl(nodes, active_cfg);
  for (size_t i = 0; i < nodes.size(); i++) {
    if (!include_deactivated) {
      push_cfg_path(nodes[i].c_str());
      bool skip = marked_deactivated(active_cfg);
      pop_cfg_path();
      if (skip) {
        continue;
      }
    }
    cnodes.push_back(nodes[i]);
  }
}

/* create all child nodes of current work path that have default values
 *   path_comps: path components. MUST match the work path and is only
 *               needed for the get_parsed_tmpl() call.
 * return true if successful. otherwise return false.
 * note: assume current work path has just been created so no child
 *       nodes exist.
 */
bool
Cstore::create_default_children(const Cpath& path_comps)
{
  vector<string> tcnodes;
  get_all_tmpl_child_node_names(tcnodes);

  bool ret = true;
  Cpath pcomps(path_comps);
  // need to save/reset/restore paths for get_parsed_tmpl()
  unique_ptr<SavePaths> save(create_save_paths());
  reset_paths();
  for (size_t i = 0; i < tcnodes.size(); i++) {
    pcomps.push(tcnodes[i]);
    tr1::shared_ptr<Ctemplate> def(get_parsed_tmpl(pcomps, false));
    if (def.get() && def->getDefault()) {
      // has default value. set it.
      append_cfg_path(pcomps);
      if (!add_node() || !write_value(def->getDefault())
          || !mark_display_default()) {
        ret = false;
      }
      reset_paths();
    }
    pcomps.pop();
    if (!ret) {
      break;
    }
  }
  return ret;
}

/* return environment string for "edit"-related operations based on current
 * work/tmpl paths.
 */
void
Cstore::get_edit_env(string& env)
{
  Cpath lvec;
  get_edit_level(lvec);
  string lvl;
  for (size_t i = 0; i < lvec.size(); i++) {
    if (lvl.length() > 0) {
      lvl += " ";
    }
    lvl += lvec[i];
  }

  env = ("export " + C_ENV_EDIT_LEVEL + "='" + get_edit_level_path() + "';");
  env += (" export " + C_ENV_TMPL_LEVEL + "='" + get_tmpl_level_path() + "';");
  env += (" export " + C_ENV_SHELL_PROMPT + "='"
          + get_shell_prompt(lvl) + "';");
}

// return shell prompt string for specified edit level
string
Cstore::get_shell_prompt(const string& level)
{
  string lvl = level;
  if (lvl.length() > 0) {
    lvl = " " + lvl;
  }
  return ("[edit" + lvl + "]\\n\\u@\\h# ");
}

// escape the single quotes in the string for shell
void
Cstore::shell_escape_squotes(string& str)
{
  size_t sq = 0;
  while ((sq = str.find('\'', sq)) != str.npos) {
    str.replace(sq, 1, "'\\''");
    sq += 4;
  }
}

// print a vector of strings
void
Cstore::print_path_vec(const char *pre, const char *post,
                       const Cpath& pvec, const char *quote)
{
  output_user("%s", pre);
  for (size_t i = 0; i < pvec.size(); i++) {
    if (i > 0) {
      output_user(" ");
    }
    output_user("%s%s%s", quote, pvec[i], quote);
  }
  output_user("%s", post);
}

void
Cstore::voutput_user(FILE *out, FILE *dout, const char *fmt, va_list alist)
{
  if (out) {
    vfprintf(out, fmt, alist);
  } else if (dout) {
    vfprintf(dout, fmt, alist);
  } else {
    vprintf(fmt, alist);
  }
}

void
Cstore::voutput_internal(const char *fmt, va_list alist)
{
  int fdout = -1;
  FILE *fout = NULL;
  do {
    if ((fdout = open(C_LOGFILE_STDOUT.c_str(),
                      O_WRONLY | O_CREAT, 0660)) == -1) {
      break;
    }
    if (lseek(fdout, 0, SEEK_END) == ((off_t) -1)) {
      break;
    }
    if ((fout = fdopen(fdout, "a")) == NULL) {
      break;
    }
    vfprintf(fout, fmt, alist);
  } while (0);
  if (fout) {
    fclose(fout);
    // fdout is implicitly closed
  } else if (fdout >= 0) {
    close(fdout);
  }
}

void
Cstore::vexit_internal(const char *fmt, va_list alist)
{
  char buf[256];
  vsnprintf(buf, 256, fmt, alist);
  output_internal("%s\n", buf);
  fprintf(stderr, "DEBUG vexit_internal: %s\n", buf); // DEBUG
  if (Perl_get_context()) {
    /* we're in a perl context. do a croak to provide more information.
     * note that the message should not end in "\n", or the croak message
     * will be truncated for some reason.
     */
    Perl_croak_nocontext("%s", buf);
    // does not return
  } else {
    // output error message and exit
    output_user_err("%s\n", buf);
    exit(1);
  }
}

} // end namespace cstore

