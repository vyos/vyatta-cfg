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

#ifndef _CSTORE_UNIONFS_H_
#define _CSTORE_UNIONFS_H_
#include <vector>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <boost/filesystem.hpp>

#include <cli_cstore.h>
#include <cstore/cstore.hpp>

namespace cstore { // begin namespace cstore

namespace b_fs = boost::filesystem;

class UnionfsCstore : public Cstore {
public:
  UnionfsCstore(bool use_edit_level);
  UnionfsCstore(const string& session_id, string& env);
  virtual ~UnionfsCstore();

  ////// public virtual functions declared in base class
  bool markSessionUnsaved();
  bool unmarkSessionUnsaved();
  bool sessionUnsaved();
  bool sessionChanged();
  bool setupSession();
  bool teardownSession();
  bool inSession();

private:
  // constants
  static const string C_ENV_TMPL_ROOT;
  static const string C_ENV_WORK_ROOT;
  static const string C_ENV_ACTIVE_ROOT;
  static const string C_ENV_CHANGE_ROOT;
  static const string C_ENV_TMP_ROOT;

  static const string C_DEF_TMPL_ROOT;
  static const string C_DEF_CFG_ROOT;
  static const string C_DEF_ACTIVE_ROOT;
  static const string C_DEF_CHANGE_PREFIX;
  static const string C_DEF_WORK_PREFIX;
  static const string C_DEF_TMP_PREFIX;

  static const string C_MARKER_DEF_VALUE;
  static const string C_MARKER_DEACTIVATE;
  static const string C_MARKER_CHANGED;
  static const string C_MARKER_UNSAVED;
  static const string C_COMMITTED_MARKER_FILE;
  static const string C_COMMENT_FILE;
  static const string C_TAG_NAME;
  static const string C_VAL_NAME;
  static const string C_DEF_NAME;

  /* max size for a file.
   * currently this includes value file and comment file.
   */
  static const size_t C_UNIONFS_MAX_FILE_SIZE = 262144;

  // root dirs (constant)
  b_fs::path work_root;   // working root (union)
  b_fs::path active_root; // active root (readonly part of union)
  b_fs::path change_root; // change root (r/w part of union)
  b_fs::path tmp_root;   // temp root
  b_fs::path tmpl_root;   // template root

  // path buffers
  b_fs::path mutable_cfg_path;  // mutable part of config path
  b_fs::path tmpl_path;         // whole template path
  Cstore::MapT<const void *, pair<b_fs::path, b_fs::path> > saved_paths;
    // saved mutable part of cfg path and whole template path

  ////// virtual functions defined in base class
  // begin path modifiers
  void push_tmpl_path(const string& new_comp) {
    push_path(tmpl_path, new_comp);
  };
  void push_tmpl_path_tag() {
    /* not using push_path => not "escaped".
     * NOTE: this is the only interface that can push tmpl_path "unescaped".
     *       this means the pop_tmpl_path() function potentially may return
     *       incorrect value if used in combination with this function.
     *       however, since current C_TAG_NAME doesn't contain any escape
     *       sequences, this cannot happen for now.
     */
    tmpl_path /= C_TAG_NAME;
  };
  string pop_tmpl_path() {
    return pop_path(tmpl_path);
  };
  void push_cfg_path(const string& new_comp) {
    push_path(mutable_cfg_path, new_comp);
  };
  string pop_cfg_path() {
    return pop_path(mutable_cfg_path);
  };
  void append_cfg_path(const vector<string>& path_comps) {
    for (size_t i = 0; i < path_comps.size(); i++) {
      push_cfg_path(path_comps[i]);
    }
  };
  void reset_paths() {
    tmpl_path = tmpl_root;
    mutable_cfg_path = "";
  };
  void save_paths(const void *handle = NULL) {
    pair<b_fs::path, b_fs::path> p;
    p.first = mutable_cfg_path;
    p.second = tmpl_path;
    saved_paths[handle] = p;
  };
  void restore_paths(const void *handle = NULL) {
    Cstore::MapT<const void *, pair<b_fs::path, b_fs::path> >::iterator it
      = saved_paths.find(handle);
    if (it == saved_paths.end()) {
      exit_internal("restore_paths: handle not found\n");
    }
    pair<b_fs::path, b_fs::path> p = it->second;
    mutable_cfg_path = p.first;
    tmpl_path = p.second;
  };
  bool cfg_path_at_root() {
    return (!mutable_cfg_path.has_parent_path());
  };
  bool tmpl_path_at_root() {
    return (tmpl_path.file_string() == tmpl_root.file_string());
  };
  // end path modifiers

  // these operate on current tmpl path
  bool tmpl_node_exists();
  Ctemplate *tmpl_parse();

  // these operate on current work path
  bool add_node();
  bool remove_node();
  void get_all_child_node_names_impl(vector<string>& cnodes, bool active_cfg);
  void get_all_tmpl_child_node_names(vector<string>& cnodes) {
    get_all_child_dir_names(tmpl_path, cnodes);
  };
  bool write_value_vec(const vector<string>& vvec, bool active_cfg);
  bool rename_child_node(const string& oname, const string& nname);
  bool copy_child_node(const string& oname, const string& nname);
  bool mark_display_default();
  bool unmark_display_default();
  bool mark_deactivated();
  bool unmark_deactivated();
  bool unmark_deactivated_descendants();
  bool mark_changed_with_ancestors();
  bool unmark_changed_with_descendants();
  bool remove_comment();
  bool set_comment(const string& comment);
  bool discard_changes(unsigned long long& num_removed);

  // observers for work path
  bool cfg_node_changed();

  // observers for work path or active path
  bool cfg_node_exists(bool active_cfg);
  bool read_value_vec(vector<string>& vvec, bool active_cfg);
  bool marked_deactivated(bool active_cfg);
  bool get_comment(string& comment, bool active_cfg);
  bool marked_display_default(bool active_cfg);

  // observers during commit operation
  bool marked_committed(const Ctemplate *def, bool is_set);

  // these operate on both current tmpl and work paths
  bool validate_val_impl(const Ctemplate *def, char *value);

  // observers for "edit/tmpl levels" (for "edit"-related operations).
  // note that these should be moved to base class in the future.
  string get_edit_level_path() {
    return cfg_path_to_str();
  };
  string get_tmpl_level_path() {
    return tmpl_path_to_str();
  };
  void get_edit_level(vector<string>& path_comps);
  bool edit_level_at_root() {
    return cfg_path_at_root();
  };

  // for testing/debugging
  string cfg_path_to_str();
  string tmpl_path_to_str();

  ////// private functions
  b_fs::path get_work_path() { return (work_root / mutable_cfg_path); };
  b_fs::path get_active_path() { return (active_root / mutable_cfg_path); };
  b_fs::path get_change_path() { return (change_root / mutable_cfg_path); };
  void push_path(b_fs::path& old_path, const string& new_comp);
  string pop_path(b_fs::path& path);
  void get_all_child_dir_names(b_fs::path root, vector<string>& nodes);
  bool write_file(const string& file, const string& data);
  bool create_file(const string& file) {
    return write_file(file, "");
  };
  bool read_whole_file(const b_fs::path& file, string& data);
  bool committed_marker_exists(const string& marker);
  void recursive_copy_dir(const b_fs::path& src, const b_fs::path& dst);

  // boost fs operations wrappers
  bool b_fs_exists(const b_fs::path& path) {
    try { return b_fs::exists(path); } catch (...) { return false; }
  };
  bool b_fs_is_directory(const b_fs::path& path) {
    try { return b_fs::is_directory(path); } catch (...) { return false; }
  };
  bool b_fs_is_regular(const b_fs::path& path) {
    try { return b_fs::is_regular(path); } catch (...) { return false; }
  };
};

} // end namespace cstore

#endif /* _CSTORE_UNIONFS_H_ */

