/*
 * Copyright (C) 2011 Vyatta, Inc.
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

#ifndef _CNODE_UTIL_HPP_
#define _CNODE_UTIL_HPP_
#include <vector>

namespace cnode {

template<class N> class TreeNode {
public:
  typedef N node_type;
  typedef std::vector<N *> nodes_vec_type;
  typedef typename nodes_vec_type::iterator nodes_iter_type;

  TreeNode() : _parent(0) {}
  virtual ~TreeNode() { 
    for ( nodes_iter_type it = _child_nodes.begin(); it != _child_nodes.end(); ++it ) 
      delete * it;
    _child_nodes.clear();
  }

  const nodes_vec_type& getChildNodes() const { return _child_nodes; }
  size_t numChildNodes() const {
    return _child_nodes.size();
  }
  node_type *getParent() const { return _parent; }
  node_type *childAt(size_t idx) { return _child_nodes[idx]; }
  void setParent(node_type *p) { _parent = p; }
  void clearChildNodes() { _child_nodes.clear(); }
  void addChildNode(node_type *cnode) {
    _child_nodes.push_back(cnode);
    cnode->_parent = static_cast<node_type *>(this);
  }

  bool removeChildNode(node_type *cnode) {
    nodes_iter_type it = _child_nodes.begin();
    while (it != _child_nodes.end()) {
      if (*it == cnode) {
        _child_nodes.erase(it);
        return true;
      }
      ++it;
    }
    return false;
  }

  bool detachFromParent() {
    if (!_parent) {
      // not attached to tree
      return false;
    }
    if (_parent->removeChildNode(static_cast<node_type *>(this))) {
      _parent = 0;
      return true;
    }
    return false;
  }

  void detachFromChildren() {
    for (size_t i = 0; i < _child_nodes.size(); i++) {
      _child_nodes[i]->_parent = 0;
    }
    _child_nodes.clear();
  }

private:
  node_type *_parent;
  nodes_vec_type _child_nodes;
};

} // namespace cnode

#endif /* _CNODE_UTIL_HPP_ */

