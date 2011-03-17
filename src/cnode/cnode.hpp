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

#ifndef _CNODE_HPP_
#define _CNODE_HPP_
#include <vector>
#include <string>

#include <cstore/cstore.hpp>

namespace cnode {

using namespace cstore;

class CfgNode {
public:
  CfgNode(Cpath& path_comps, char *name, char *val, char *comment,
          int deact, Cstore *cstore, bool tag_if_invalid = false);
  CfgNode(Cstore& cstore, Cpath& path_comps, bool active = false,
          bool recursive = true);
  ~CfgNode() {};

  bool isTag() const { return _is_tag; }
  bool isLeaf() const { return _is_leaf; }
  bool isMulti() const { return _is_multi; }
  bool isValue() const { return _is_value; }
  bool isDefault() const { return _is_default; }
  bool isDeactivated() const { return _is_deactivated; }
  bool isLeafTypeless() const { return _is_leaf_typeless; }
  bool isInvalid() const { return _is_invalid; }
  bool isEmpty() const { return _is_empty; }
  bool exists() const { return _exists; }

  const string& getName() const { return _name; }
  const string& getValue() const { return _value; }
  const vector<string>& getValues() const { return _values; }
  const string& getComment() const { return _comment; }
  const vector<CfgNode *>& getChildNodes() const { return _child_nodes; }

  void addMultiValue(char *val) { _values.push_back(val); }
  void addChildNode(CfgNode *cnode) {
    _child_nodes.push_back(cnode);
    _is_empty = false;
  }
  void setValue(char *val) { _value = val; }

private:
  bool _is_tag;
  bool _is_leaf;
  bool _is_multi;
  bool _is_value;
  bool _is_default;
  bool _is_deactivated;
  bool _is_leaf_typeless;
  bool _is_invalid;
  bool _is_empty;
  bool _exists;
  string _name;
  string _value;
  vector<string> _values;
  string _comment;
  vector<CfgNode *> _child_nodes;
};

} // namespace cnode

#endif /* _CNODE_HPP_ */

