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
#include <fstream>
#include <sstream>

#include <errno.h>
#include <sys/mount.h>

#include <cli_cstore.h>
#include <cstore/unionfs/cstore-unionfs.hpp>

using namespace cstore;

////// constants
// environment vars defining root dirs
const string UnionfsCstore::C_ENV_TMPL_ROOT = "VYATTA_CONFIG_TEMPLATE";
const string UnionfsCstore::C_ENV_WORK_ROOT = "VYATTA_TEMP_CONFIG_DIR";
const string UnionfsCstore::C_ENV_ACTIVE_ROOT
  = "VYATTA_ACTIVE_CONFIGURATION_DIR";
const string UnionfsCstore::C_ENV_CHANGE_ROOT = "VYATTA_CHANGES_ONLY_DIR";
const string UnionfsCstore::C_ENV_TMP_ROOT = "VYATTA_CONFIG_TMP";

// default root dirs/paths
const string UnionfsCstore::C_DEF_TMPL_ROOT
  = "/opt/vyatta/share/vyatta-cfg/templates";
const string UnionfsCstore::C_DEF_CFG_ROOT
  = "/opt/vyatta/config";
const string UnionfsCstore::C_DEF_ACTIVE_ROOT
  = UnionfsCstore::C_DEF_CFG_ROOT + "/active";
const string UnionfsCstore::C_DEF_CHANGE_PREFIX = "/tmp/changes_only_";
const string UnionfsCstore::C_DEF_WORK_PREFIX
  = UnionfsCstore::C_DEF_CFG_ROOT + "/tmp/new_config_";
const string UnionfsCstore::C_DEF_TMP_PREFIX
  = UnionfsCstore::C_DEF_CFG_ROOT + "/tmp/tmp_";

// markers
const string UnionfsCstore::C_MARKER_DEF_VALUE  = "def";
const string UnionfsCstore::C_MARKER_DEACTIVATE = ".disable";
const string UnionfsCstore::C_MARKER_CHANGED = ".modified";
const string UnionfsCstore::C_MARKER_UNSAVED = ".unsaved";
const string UnionfsCstore::C_COMMITTED_MARKER_FILE = "/tmp/.changes";
const string UnionfsCstore::C_COMMENT_FILE = ".comment";
const string UnionfsCstore::C_TAG_NAME = "node.tag";
const string UnionfsCstore::C_VAL_NAME = "node.val";
const string UnionfsCstore::C_DEF_NAME = "node.def";


////// static
static Cstore::MapT<char, string> _fs_escape_chars;
static Cstore::MapT<string, char> _fs_unescape_chars;
static void
_init_fs_escape_chars()
{
  _fs_escape_chars[-1] = "\%\%\%";
  _fs_escape_chars['%'] = "\%25";
  _fs_escape_chars['/'] = "\%2F";

  _fs_unescape_chars["\%\%\%"] = -1;
  _fs_unescape_chars["\%25"] = '%';
  _fs_unescape_chars["\%2F"] = '/';
}

static string
_escape_char(char c)
{
  Cstore::MapT<char, string>::iterator p = _fs_escape_chars.find(c);
  if (p != _fs_escape_chars.end()) {
    return _fs_escape_chars[c];
  } else {
    return string(1, c);
  }
}

static Cstore::MapT<string, string> _escape_path_name_cache;

static string
_escape_path_name(const string& path)
{
  Cstore::MapT<string, string>::iterator p
    = _escape_path_name_cache.find(path);
  if (p != _escape_path_name_cache.end()) {
    // found escaped string in cache. just return it.
    return _escape_path_name_cache[path];
  }

  // special case for empty string
  string npath = (path.size() == 0) ? _fs_escape_chars[-1] : "";
  for (size_t i = 0; i < path.size(); i++) {
    npath += _escape_char(path[i]);
  }

  // cache it before return
  _escape_path_name_cache[path] = npath;
  return npath;
}

static Cstore::MapT<string, string> _unescape_path_name_cache;

static string
_unescape_path_name(const string& path)
{
  Cstore::MapT<string, string>::iterator p
    = _unescape_path_name_cache.find(path);
  if (p != _unescape_path_name_cache.end()) {
    // found unescaped string in cache. just return it.
    return _unescape_path_name_cache[path];
  }

  // assume all escape patterns are 3-char
  string npath = "";
  for (size_t i = 0; i < path.size(); i++) {
    if ((path.size() - i) < 3) {
      npath += path.substr(i);
      break;
    }
    string s = path.substr(i, 3);
    Cstore::MapT<string, char>::iterator p = _fs_unescape_chars.find(s);
    if (p != _fs_unescape_chars.end()) {
      char c = _fs_unescape_chars[s];
      if (path.size() == 3 && c == -1) {
        // special case for empty string
        npath = "";
        break;
      }
      npath += string(1, _fs_unescape_chars[s]);
      // skip the escape sequence
      i += 2;
    } else {
      npath += path.substr(i, 1);
    }
  }
  // cache it before return
  _unescape_path_name_cache[path] = npath;
  return npath;
}


////// constructor/destructor
/* "current session" constructor.
 * this constructor sets up the object from environment.
 * used when environment is already set up, i.e., when operating on the
 * "current" config session. e.g., in the following scenarios
 *   configure commands
 *   perl module
 *   shell "current session" api
 *
 * note: this also applies when using the cstore in operational mode,
 *       in which case only the template root and the active root will be
 *       valid.
 */
UnionfsCstore::UnionfsCstore(bool use_edit_level)
{
  // set up root dir strings
  char *val;
  if ((val = getenv(C_ENV_TMPL_ROOT.c_str()))) {
    tmpl_path = val;
  } else {
    tmpl_path = C_DEF_TMPL_ROOT;
  }
  tmpl_root = tmpl_path; // save a copy of tmpl root
  if ((val = getenv(C_ENV_WORK_ROOT.c_str()))) {
    work_root = val;
  }
  if ((val = getenv(C_ENV_TMP_ROOT.c_str()))) {
    tmp_root = val;
  }
  if ((val = getenv(C_ENV_ACTIVE_ROOT.c_str()))) {
    active_root = val;
  } else {
    active_root = C_DEF_ACTIVE_ROOT;
  }
  if ((val = getenv(C_ENV_CHANGE_ROOT.c_str()))) {
    change_root = val;
  }

  /* note: the original perl API module does not use the edit levels
   *       from environment. only the actual CLI operations use them.
   *       so here make it an option.
   */
  mutable_cfg_path = "/";
  if (use_edit_level) {
    // set up path strings
    if ((val = getenv(C_ENV_EDIT_LEVEL.c_str()))) {
      mutable_cfg_path = val;
    }
    if ((val = getenv(C_ENV_TMPL_LEVEL.c_str())) && val[0] && val[1]) {
      /* no need to append root (i.e., "/"). level (if exists) always
       * starts with '/', so only append it if it is at least two chars
       * (i.e., it is not "/").
       */
      tmpl_path /= val;
    }
  }
  _init_fs_escape_chars();
}

/* "specific session" constructor.
 * this constructor sets up the object for the specified session ID and
 * returns an environment string that can be "evaled" to set up the
 * shell environment.
 *
 * used when the session environment needs to be established. this is
 * mainly for the shell functions that set up configuration sessions.
 * i.e., the "vyatta-cfg-cmd-wrapper" (on boot or for GUI etc.) and
 * the cfg completion script (when entering configure mode).
 *
 *   sid: session ID.
 *   env: (output) environment string.
 *
 * note: this does NOT set up the session. caller needs to use the
 *       explicit session setup/teardown functions as needed.
 */
UnionfsCstore::UnionfsCstore(const string& sid, string& env)
  : Cstore(env)
{
  tmpl_root = C_DEF_TMPL_ROOT;
  tmpl_path = tmpl_root;
  active_root = C_DEF_ACTIVE_ROOT;
  work_root = (C_DEF_WORK_PREFIX + sid);
  change_root = (C_DEF_CHANGE_PREFIX + sid);
  tmp_root = (C_DEF_TMP_PREFIX + sid);

  string declr = " declare -x -r "; // readonly vars
  env += " umask 002; {";
  env += (declr + C_ENV_ACTIVE_ROOT + "=" + active_root.file_string());
  env += (declr + C_ENV_CHANGE_ROOT + "=" + change_root.file_string() + ";");
  env += (declr + C_ENV_WORK_ROOT + "=" + work_root.file_string() + ";");
  env += (declr + C_ENV_TMP_ROOT + "=" + tmp_root.file_string() + ";");
  env += (declr + C_ENV_TMPL_ROOT + "=" + tmpl_root.file_string() + ";");
  env += " } >&/dev/null || true";

  // set up path strings using level vars
  char *val;
  mutable_cfg_path = "/";
  if ((val = getenv(C_ENV_EDIT_LEVEL.c_str()))) {
    mutable_cfg_path = val;
  }
  if ((val = getenv(C_ENV_TMPL_LEVEL.c_str())) && val[0] && val[1]) {
    // see comment in the other constructor
    tmpl_path /= val;
  }

  _init_fs_escape_chars();
}

UnionfsCstore::~UnionfsCstore()
{
}


////// public virtual functions declared in base class
bool
UnionfsCstore::markSessionUnsaved()
{
  b_fs::path marker = work_root / C_MARKER_UNSAVED;
  if (b_fs_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker.file_string())) {
    output_internal("failed to mark unsaved [%s]\n",
                    marker.file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmarkSessionUnsaved()
{
  b_fs::path marker = work_root / C_MARKER_UNSAVED;
  if (!b_fs_exists(marker)) {
    // not marked. treat as success.
    return true;
  }
  try {
    b_fs::remove(marker);
  } catch (...) {
    output_internal("failed to unmark unsaved [%s]\n",
                    marker.file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::sessionUnsaved()
{
  b_fs::path marker = work_root / C_MARKER_UNSAVED;
  return b_fs_exists(marker);
}

bool
UnionfsCstore::sessionChanged()
{
  b_fs::path marker = work_root / C_MARKER_CHANGED;
  return b_fs_exists(marker);
}

/* set up the session associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::setupSession()
{
  if (!b_fs_exists(work_root)) {
    // session doesn't exist. create dirs.
    try {
      b_fs::create_directories(work_root);
      b_fs::create_directories(change_root);
      b_fs::create_directories(tmp_root);
      if (!b_fs_exists(active_root)) {
        // this should only be needed on boot
        b_fs::create_directories(active_root);
      }
    } catch (...) {
      output_internal("setup session failed to create session directories\n");
      return false;
    }

    // union mount
    string mopts = ("dirs=" + change_root.file_string() + "=rw:"
                    + active_root.file_string() + "=ro");
    if (mount("unionfs", work_root.file_string().c_str(), "unionfs", 0,
              mopts.c_str()) != 0) {
      output_internal("setup session mount failed [%s][%s]\n",
                      strerror(errno), work_root.file_string().c_str());
      return false;
    }
  } else if (!b_fs_is_directory(work_root)) {
    output_internal("setup session not dir [%s]\n",
                    work_root.file_string().c_str());
    return false;
  }
  return true;
}

/* tear down the session associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::teardownSession()
{
  // check if session exists
  string wstr = work_root.file_string();
  if (wstr.empty() || wstr.find(C_DEF_WORK_PREFIX) != 0
      || !b_fs_exists(work_root) || !b_fs_is_directory(work_root)) {
    // no session
    output_internal("teardown invalid session [%s]\n", wstr.c_str());
    return false;
  }

  // unmount the work root (union)
  if (umount(wstr.c_str()) != 0) {
    output_internal("teardown session umount failed [%s][%s]\n",
                    strerror(errno), wstr.c_str());
    return false;
  }

  // remove session directories
  bool ret = false;
  try {
    if (b_fs::remove_all(work_root) != 0
        && b_fs::remove_all(change_root) != 0
        && b_fs::remove_all(tmp_root) != 0) {
      ret = true;
    }
  } catch (...) {
  }
  if (!ret) {
    output_internal("failed to remove session directories\n");
  }
  return ret;
}

/* whether an actual config session is associated with this object.
 * the session comes from either the environment or the session ID
 * (see the two different constructors).
 */
bool
UnionfsCstore::inSession()
{
  string wstr = work_root.file_string();
  return (!wstr.empty() && wstr.find(C_DEF_WORK_PREFIX) == 0
          && b_fs_exists(work_root) && b_fs_is_directory(work_root));
}


////// virtual functions defined in base class
/* check if current tmpl_path is a valid tmpl dir.
 * return true if valid. otherwise return false.
 */
bool
UnionfsCstore::tmpl_node_exists()
{
  return (b_fs_exists(tmpl_path) && b_fs_is_directory(tmpl_path));
}

/* parse template at current tmpl_path and return an allocated Ctemplate
 * pointer if successful. otherwise return 0.
 */
Ctemplate *
UnionfsCstore::tmpl_parse()
{
  tr1::shared_ptr<vtw_def> def(new vtw_def);
  vtw_def *_def = def.get();
  b_fs::path tp = tmpl_path / C_DEF_NAME;
  if (_def && b_fs_exists(tp) && b_fs_is_regular(tp)
      && parse_def(_def, tp.file_string().c_str(), 0) == 0) {
    // succes
    return (new Ctemplate(def));
  }
  return 0;
}

bool
UnionfsCstore::cfg_node_exists(bool active_cfg)
{
  b_fs::path p = (active_cfg ? get_active_path() : get_work_path());
  return (b_fs_exists(p) && b_fs_is_directory(p));
}

bool
UnionfsCstore::add_node()
{
  bool ret = true;
  try {
    if (!b_fs::create_directory(get_work_path())) {
      // already exists. shouldn't call this function.
      ret = false;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to add node [%s]\n",
                    get_work_path().file_string().c_str());
  }
  return ret;
}

bool
UnionfsCstore::remove_node()
{
  if (!b_fs_exists(get_work_path()) || !b_fs_is_directory(get_work_path())) {
    output_internal("remove non-existent node [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  bool ret = false;
  try {
    if (b_fs::remove_all(get_work_path()) != 0) {
      ret = true;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to remove node [%s]\n",
                    get_work_path().file_string().c_str());
  }
  return ret;
}

void
UnionfsCstore::get_all_child_node_names_impl(vector<string>& cnodes,
                                             bool active_cfg)
{
  b_fs::path p = (active_cfg ? get_active_path() : get_work_path());
  get_all_child_dir_names(p, cnodes);

  /* XXX special cases to emulate original perl API behavior.
   *     original perl listNodes() and listOrigNodes() return everything
   *     under a node (except for ".*"), including "node.val" and "def".
   *
   *     perl API should operate at abstract level and should not access
   *     such implementation-specific details. however, currently
   *     things like config output depend on this behavior, so this
   *     function needs to return them for now.
   *
   *     use a whilelist-approach, i.e., only add the following:
   *       node.val
   *       def
   *
   * FIXED: perl scripts have been changed to eliminate the use of "def"
   * and "node.val", so they no longer need to be returned.
   */
}

bool
UnionfsCstore::read_value_vec(vector<string>& vvec, bool active_cfg)
{
  b_fs::path vpath = (active_cfg ? get_active_path() : get_work_path());
  vpath /= C_VAL_NAME;

  string ostr;
  if (!read_whole_file(vpath, ostr)) {
    return false;
  }

  /* XXX original implementation used to remove a trailing '\n' after
   *     a read. it was only necessary because it was adding a '\n' when
   *     writing the file. don't remove anything now since we shouldn't
   *     be writing it any more.
   */
  // separate values using newline as delimiter
  size_t start_idx = 0, idx = 0;
  for (; idx < ostr.size(); idx++) {
    if (ostr[idx] == '\n') {
      // got a value
      vvec.push_back(ostr.substr(start_idx, (idx - start_idx)));
      start_idx = idx + 1;
    }
  }
  if (start_idx < ostr.size()) {
    vvec.push_back(ostr.substr(start_idx, (idx - start_idx)));
  } else {
    // last char is a newline => another empty value
    vvec.push_back("");
  }
  return true;
}

bool
UnionfsCstore::write_value_vec(const vector<string>& vvec, bool active_cfg)
{
  b_fs::path wp = (active_cfg ? get_active_path() : get_work_path());
  wp /= C_VAL_NAME;

  if (b_fs_exists(wp) && !b_fs_is_regular(wp)) {
    // not a file
    output_internal("failed to write node value (file) [%s]\n",
                    wp.file_string().c_str());
    return false;
  }

  string ostr = "";
  for (size_t i = 0; i < vvec.size(); i++) {
    if (i > 0) {
      // subsequent values require delimiter
      ostr += "\n";
    }
    ostr += vvec[i];
  }

  if (!write_file(wp.file_string().c_str(), ostr)) {
    output_internal("failed to write node value (write) [%s]\n",
                    wp.file_string().c_str());
    return false;
  }

  return true;
}

bool
UnionfsCstore::rename_child_node(const string& oname, const string& nname)
{
  b_fs::path opath = get_work_path() / oname;
  b_fs::path npath = get_work_path() / nname;
  if (!b_fs_exists(opath) || !b_fs_is_directory(opath)
      || b_fs_exists(npath)) {
    output_internal("cannot rename node [%s,%s,%s]\n",
                    get_work_path().file_string().c_str(),
                    oname.c_str(), nname.c_str());
    return false;
  }
  bool ret = true;
  try {
    /* somehow b_fs::rename() can't be used here as it considers the operation
     * "Invalid cross-device link" and fails with an exception, probably due
     * to unionfs in some way.
     * do it the hard way.
     */
    recursive_copy_dir(opath, npath);
    if (b_fs::remove_all(opath) == 0) {
      ret = false;
    }
  } catch (...) {
    ret = false;
  }
  if (!ret) {
    output_internal("failed to rename node [%s,%s]\n",
                    opath.file_string().c_str(),
                    npath.file_string().c_str());
  }
  return ret;
}

bool
UnionfsCstore::copy_child_node(const string& oname, const string& nname)
{
  b_fs::path opath = get_work_path() / oname;
  b_fs::path npath = get_work_path() / nname;
  if (!b_fs_exists(opath) || !b_fs_is_directory(opath)
      || b_fs_exists(npath)) {
    output_internal("cannot copy node [%s,%s,%s]\n",
                    get_work_path().file_string().c_str(),
                    oname.c_str(), nname.c_str());
    return false;
  }
  try {
    recursive_copy_dir(opath, npath);
  } catch (...) {
    output_internal("failed to copy node [%s,%s,%s]\n",
                    get_work_path().file_string().c_str(),
                    oname.c_str(), nname.c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::mark_display_default()
{
  b_fs::path marker = get_work_path() / C_MARKER_DEF_VALUE;
  if (b_fs_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker.file_string())) {
    output_internal("failed to mark default [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_display_default()
{
  b_fs::path marker = get_work_path() / C_MARKER_DEF_VALUE;
  if (!b_fs_exists(marker)) {
    // not marked. treat as success.
    return true;
  }
  try {
    b_fs::remove(marker);
  } catch (...) {
    output_internal("failed to unmark default [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::marked_display_default(bool active_cfg)
{
  b_fs::path marker = (active_cfg ? get_active_path() : get_work_path())
                      / C_MARKER_DEF_VALUE;
  return b_fs_exists(marker);
}

bool
UnionfsCstore::marked_deactivated(bool active_cfg)
{
  b_fs::path p = (active_cfg ? get_active_path() : get_work_path());
  b_fs::path marker = p / C_MARKER_DEACTIVATE;
  return b_fs_exists(marker);
}

bool
UnionfsCstore::mark_deactivated()
{
  b_fs::path marker = get_work_path() / C_MARKER_DEACTIVATE;
  if (b_fs_exists(marker)) {
    // already marked. treat as success.
    return true;
  }
  if (!create_file(marker.file_string())) {
    output_internal("failed to mark deactivated [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_deactivated()
{
  b_fs::path marker = get_work_path() / C_MARKER_DEACTIVATE;
  if (!b_fs_exists(marker)) {
    // not deactivated. treat as success.
    return true;
  }
  try {
    b_fs::remove(marker);
  } catch (...) {
    output_internal("failed to unmark deactivated [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  return true;
}

bool
UnionfsCstore::unmark_deactivated_descendants()
{
  bool ret = false;
  do {
    // sanity check
    if (!b_fs_is_directory(get_work_path())) {
      break;
    }

    try {
      vector<b_fs::path> markers;
      b_fs::recursive_directory_iterator di(get_work_path());
      for (; di != b_fs::recursive_directory_iterator(); ++di) {
        if (!b_fs_is_regular(di->path())
            || di->path().filename() != C_MARKER_DEACTIVATE) {
          // not marker
          continue;
        }
        if (di->path().parent_path() == get_work_path()) {
          // don't unmark the node itself
          continue;
        }
        markers.push_back(di->path());
      }
      for (size_t i = 0; i < markers.size(); i++) {
        b_fs::remove(markers[i]);
      }
    } catch (...) {
      break;
    }
    ret = true;
  } while (0);
  if (!ret) {
    output_internal("failed to unmark deactivated descendants [%s]\n",
                    get_work_path().file_string().c_str());
  }
  return ret;
}

// mark current work path and all ancestors as "changed"
bool
UnionfsCstore::mark_changed_with_ancestors()
{
  b_fs::path opath = mutable_cfg_path; // use a copy
  bool done = false;
  while (!done) {
    b_fs::path marker = work_root;
    if (opath.has_parent_path()) {
      marker /= opath;
      pop_path(opath);
    } else {
      done = true;
    }
    if (!b_fs_exists(marker) || !b_fs_is_directory(marker)) {
      // don't do anything if the node is not there
      continue;
    }
    marker /= C_MARKER_CHANGED;
    if (b_fs_exists(marker)) {
      // reached a node already marked => done
      break;
    }
    if (!create_file(marker.file_string())) {
      output_internal("failed to mark changed [%s]\n",
                      marker.file_string().c_str());
      return false;
    }
  }
  return true;
}

/* remove all "changed" markers under the current work path. this is used,
 * e.g., at the end of "commit" to reset a subtree.
 */
bool
UnionfsCstore::unmark_changed_with_descendants()
{
  try {
    vector<b_fs::path> markers;
    b_fs::recursive_directory_iterator di(get_work_path());
    for (; di != b_fs::recursive_directory_iterator(); ++di) {
      if (!b_fs_is_regular(di->path())
          || di->path().filename() != C_MARKER_CHANGED) {
        // not marker
        continue;
      }
      markers.push_back(di->path());
    }
    for (size_t i = 0; i < markers.size(); i++) {
      b_fs::remove(markers[i]);
    }
  } catch (...) {
    output_internal("failed to unmark changed with descendants [%s]\n",
                    get_work_path().file_string().c_str());
    return false;
  }
  return true;
}

// remove the comment at the current work path
bool
UnionfsCstore::remove_comment()
{
  b_fs::path cfile = get_work_path() / C_COMMENT_FILE;
  if (!b_fs_exists(cfile)) {
    return false;
  }
  try {
    b_fs::remove(cfile);
  } catch (...) {
    output_internal("failed to remove comment [%s]\n",
                    cfile.file_string().c_str());
    return false;
  }
  return true;
}

// set comment at the current work path
bool
UnionfsCstore::set_comment(const string& comment)
{
  b_fs::path cfile = get_work_path() / C_COMMENT_FILE;
  return write_file(cfile.file_string(), comment);
}

// discard all changes in working config
bool
UnionfsCstore::discard_changes(unsigned long long& num_removed)
{
  // need to keep unsaved marker
  bool unsaved = sessionUnsaved();
  bool ret = true;

  vector<b_fs::path> files;
  vector<b_fs::path> directories;
  try {
    // iterate through all entries in change root
    b_fs::directory_iterator di(change_root);
    for (; di != b_fs::directory_iterator(); ++di) {
      if (b_fs_is_directory(di->path())) {
        directories.push_back(di->path());
      } else {
        files.push_back(di->path());
      }
    }

    // remove and count
    num_removed = 0;
    for (size_t i = 0; i < files.size(); i++) {
      b_fs::remove(files[i]);
      num_removed++;
    }
    for (size_t i = 0; i < directories.size(); i++) {
      num_removed += b_fs::remove_all(directories[i]);
    }
  } catch (...) {
    output_internal("discard failed [%s]\n",
                    change_root.file_string().c_str());
    ret = false;
  }

  if (unsaved) {
    // restore unsaved marker
    num_removed--;
    markSessionUnsaved();
  }
  return ret;
}

// get comment at the current work or active path
bool
UnionfsCstore::get_comment(string& comment, bool active_cfg)
{
  b_fs::path cfile = (active_cfg ? get_active_path() : get_work_path());
  cfile /= C_COMMENT_FILE;
  return read_whole_file(cfile, comment);
}

// whether current work path is "changed"
bool
UnionfsCstore::cfg_node_changed()
{
  b_fs::path marker = get_work_path() / C_MARKER_CHANGED;
  return b_fs_exists(marker);
}

/* XXX currently "committed marking" is done inside commit.
 *     TODO move "committed marking" out of commit and into low-level
 *     implementation (here).
 */
/* return whether current "cfg path" has been committed, i.e., whether
 * the set or delete operation on the path has been processed by commit.
 *   is_set: whether the operation is set (for sanity check as there can
 *           be only one operation on the path).
 */
bool
UnionfsCstore::marked_committed(const Ctemplate *def, bool is_set)
{
  b_fs::path cpath = mutable_cfg_path;
  string com_str = cpath.file_string() + "/";
  if (def->isLeafValue()) {
    // path includes leaf value. construct the right string.
    string val = _unescape_path_name(cpath.filename());
    cpath = cpath.parent_path();
    /* XXX current commit implementation escapes value strings for
     *     single-value nodes but not for multi-value nodes for some
     *     reason. the following match current behavior.
     */
    if (!def->isMulti()) {
      val = _escape_path_name(val);
    }
    com_str = cpath.file_string() + "/value:" + val;
  }
  com_str = (is_set ? "+ " : "- ") + com_str;
  return committed_marker_exists(com_str);
}

bool
UnionfsCstore::validate_val_impl(const Ctemplate *def, char *value)
{
  /* XXX filesystem paths/accesses are completely embedded in var ref lib.
   *     for now, treat the lib as a unionfs-specific implementation.
   *     generalizing it will need a rewrite.
   * set the handle to be used during validate_value() for var ref
   * processing. this is a global var in cli_new.c.
   */
  var_ref_handle = (void *) this;
  bool ret = validate_value(def->getDef(), value);
  var_ref_handle = NULL;
  return ret;
}

void
UnionfsCstore::get_edit_level(vector<string>& pcomps) {
  b_fs::path opath = mutable_cfg_path; // use a copy
  while (opath.has_parent_path()) {
    pcomps.insert(pcomps.begin(), pop_path(opath));
  }
}

string
UnionfsCstore::cfg_path_to_str() {
  string cpath = mutable_cfg_path.file_string();
  if (cpath.length() == 0) {
    cpath = "/";
  }
  return cpath;
}

string
UnionfsCstore::tmpl_path_to_str() {
  // return only the mutable part
  string tpath = tmpl_path.file_string();
  tpath.erase(0, tmpl_root.file_string().length());
  if (tpath.length() == 0) {
    tpath = "/";
  }
  return tpath;
}


////// private functions
void
UnionfsCstore::push_path(b_fs::path& old_path, const string& new_comp)
{
  string comp = _escape_path_name(new_comp);
  old_path /= comp;
}

string
UnionfsCstore::pop_path(b_fs::path& path)
{
  string ret = _unescape_path_name(path.filename());
  /* note: contrary to documentation, remove_filename() does not remove
   *       trailing slash.
   */
  path = path.parent_path();
  return ret;
}

void
UnionfsCstore::get_all_child_dir_names(b_fs::path root, vector<string>& nodes)
{
  if (!b_fs_exists(root) || !b_fs_is_directory(root)) {
    // not a valid root. nop.
    return;
  }
  try {
    b_fs::directory_iterator di(root);
    for (; di != b_fs::directory_iterator(); ++di) {
      // must be directory
      if (!b_fs_is_directory(di->path())) {
        continue;
      }
      // name cannot start with "."
      string cname = di->path().filename();
      if (cname.length() < 1 || cname[0] == '.') {
        continue;
      }
      // found one
      nodes.push_back(_unescape_path_name(cname));
    }
  } catch (...) {
    return;
  }
}

bool
UnionfsCstore::write_file(const string& file, const string& data)
{
  if (data.size() > C_UNIONFS_MAX_FILE_SIZE) {
    output_internal("write_file too large\n");
    return false;
  }
  try {
    // make sure the path exists
    b_fs::path fpath(file);
    b_fs::create_directories(fpath.parent_path());

    // write the file
    ofstream fout;
    fout.exceptions(ofstream::failbit | ofstream::badbit);
    fout.open(file.c_str(), ios_base::out | ios_base::trunc);
    fout << data;
    fout.close();
  } catch (...) {
    return false;
  }
  return true;
}

bool
UnionfsCstore::read_whole_file(const b_fs::path& fpath, string& data)
{
  /* must exist, be a regular file, and smaller than limit (we're going
   * to read the whole thing).
   */
  if (!b_fs_exists(fpath) || !b_fs_is_regular(fpath)) {
    return false;
  }
  try {
    if (b_fs::file_size(fpath) > C_UNIONFS_MAX_FILE_SIZE) {
      output_internal("read_whole_file too large\n");
      return false;
    }

    stringbuf sbuf;
    ifstream fin(fpath.file_string().c_str());
    fin >> &sbuf;
    fin.close();
    /* note: if file contains just a newline => (eof() && fail())
     *       so only checking bad() and eof() (we want whole file).
     */
    if (fin.bad() || !fin.eof()) {
      // read failed
      return false;
    }
    data = sbuf.str();
  } catch (...) {
    return false;
  }
  return true;
}

/* return whether specified "commited marker" exists in the
 * "committed marker file".
 */
bool
UnionfsCstore::committed_marker_exists(const string& marker)
{
  bool ret = false;
  try {
    ifstream fin(C_COMMITTED_MARKER_FILE.c_str());
    while (!fin.eof() && !fin.bad() && !fin.fail()) {
      string line;
      getline(fin, line);
      if (line == marker) {
        ret = true;
        break;
      }
    }
    fin.close();
  } catch (...) {
    ret = false;
  }
  return ret;
}

/* recursively copy source directory to destination.
 * will throw exception (from b_fs) if fail.
 */
void
UnionfsCstore::recursive_copy_dir(const b_fs::path& src, const b_fs::path& dst)
{
  string src_str = src.file_string();
  string dst_str = dst.file_string();
  b_fs::create_directory(dst);

  b_fs::recursive_directory_iterator di(src);
  for (; di != b_fs::recursive_directory_iterator(); ++di) {
    b_fs::path opath = di->path();
    string nname = opath.file_string();
    nname.replace(0, src_str.length(), dst_str);
    b_fs::path npath = nname;
    if (b_fs_is_directory(opath)) {
      b_fs::create_directory(npath);
    } else {
      b_fs::copy_file(opath, npath);
    }
  }
}

