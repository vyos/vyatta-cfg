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

class CfgNode {
public:
  CfgNode(Cstore& cstore, std::vector<string>& path_comps,
          bool active = false, bool recursive = true);
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

  const std::string& getName() const { return _name; }
  const std::string& getValue() const { return _value; }
  const std::vector<std::string>& getValues() const { return _values; }
  const std::string& getComment() const { return _comment; }
  const std::vector<CfgNode *>& getTagValues() const { return _tag_values; }
  const std::vector<CfgNode *>& getChildNodes() const { return _child_nodes; }

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
  std::string _name;
  std::string _value;
  std::vector<std::string> _values;
  std::string _comment;
  std::vector<CfgNode *> _tag_values;
  std::vector<CfgNode *> _child_nodes;
};

} // namespace cnode

#endif /* _CNODE_HPP_ */

