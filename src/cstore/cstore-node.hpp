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

#ifndef _CSTORE_NODE_H_
#define _CSTORE_NODE_H_
#include <vector>
#include <string>
#include <map>

#include <cstore/cstore.hpp>

using namespace std;

typedef enum {
  ST_DELETED,
  ST_ADDED,
  ST_CHANGED,
  ST_STATIC
} NodeStatusT;

class CstoreCfgNode {
public:
  CstoreCfgNode(Cstore& cstore, vector<string>& path_comps,
                bool active_only = false, const string& name = "",
                NodeStatusT status = ST_STATIC);
  ~CstoreCfgNode() {};

  // diff prefixes
  static const string PFX_DIFF_ADD;
  static const string PFX_DIFF_DEL;
  static const string PFX_DIFF_UPD;
  static const string PFX_DIFF_NONE;

  // deactivate prefixes
  static const string PFX_DEACT_D;
  static const string PFX_DEACT_DP;
  static const string PFX_DEACT_AP;
  static const string PFX_DEACT_NONE;

  void setTagName(const string& tname) { _tag_name = tname; };
  bool isTag() { return _is_tag; };
  void show_as_root(bool show_default = false, bool hide_secret = false);

private:
  static bool _init;
  static Cstore::MapT<string, NodeStatusT> _st_map;
  static void init() {
    if (_init) {
      return;
    }
    _st_map[Cstore::C_NODE_STATUS_DELETED] = ST_DELETED;
    _st_map[Cstore::C_NODE_STATUS_ADDED] = ST_ADDED;
    _st_map[Cstore::C_NODE_STATUS_CHANGED] = ST_CHANGED;
    _st_map[Cstore::C_NODE_STATUS_STATIC] = ST_STATIC;
    _init = true;
  };

  Cstore *_cstore;
  string _name;
  NodeStatusT _status;
  const char *_pfx_deact;
  bool _is_tag;
  bool _is_leaf;
  bool _is_multi;
  bool _is_value;
  bool _is_default;
  bool _is_invalid;
  bool _is_empty;
  bool _active_only;
  string _tag_name;
  string _value;
  vector<string> _values;
  vector<NodeStatusT> _values_status;
  string _comment;
  NodeStatusT _comment_status;
  vector<CstoreCfgNode *> _tag_values;
  vector<CstoreCfgNode *> _child_nodes;

  void show(int level, bool show_def, bool hide_secret);
  void print_indent(int level) {
    print_indent(level, _status);
  };
  void print_indent(int level, NodeStatusT st, bool force_changed = false);
  void print_prefix(NodeStatusT st);
  const char *get_prefix_diff(NodeStatusT st);
  void print_comment(int level);
  void print_value_str(const char *vstr, bool hide_secret);
};

#endif /* _CSTORE_NODE_H_ */

