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
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>

#include <cli_cstore.h>
#include <cnode/cnode.hpp>

using namespace cnode;


////// constructors/destructors
CfgNode::CfgNode(Cpath& path_comps, char *name, char *val, char *comment,
                 int deact, Cstore *cstore, bool tag_if_invalid)
  : _is_tag(false), _is_leaf(false), _is_multi(false), _is_value(false),
    _is_default(false), _is_deactivated(false), _is_leaf_typeless(false),
    _is_invalid(false), _is_empty(true), _exists(true)
{
  if (name && name[0]) {
    // name must be non-empty
    path_comps.push(name);
  }
  if (val) {
    // value could be empty
    path_comps.push(val);
  }

  while (1) {
    if (path_comps.size() == 0) {
      // nothing to do for root node
      break;
    }

    tr1::shared_ptr<Ctemplate> def(cstore->parseTmpl(path_comps, false));
    if (def.get()) {
      // got the def
      _is_tag = def->isTag();
      _is_leaf = (!_is_tag && !def->isTypeless());

      // match constructor from cstore (leaf node never _is_value)
      _is_value = (def->isValue() && !_is_leaf);
      _is_multi = def->isMulti();

      /* XXX given the current definition of "default", the concept of
       * "default" doesn't really apply to config files.
       */
      _is_default = false;
      _is_deactivated = deact;

      vector<string> tcnodes;
      cstore->tmplGetChildNodes(path_comps, tcnodes);
      if (tcnodes.size() == 0) {
        // typeless leaf node
        _is_leaf_typeless = true;
      }

      if (comment) {
        _comment = comment;
      }
      // ignore return
    } else {
      // not a valid node
      _is_invalid = true;
      if (tag_if_invalid) {
        /* this is only used when the parser is creating a "tag node". force
         * the node to be tag since we don't have template for invalid node.
         */
        _is_tag = true;
      }
      if (val) {
        /* if parser got value for the invalid node, always treat it as
         * "tag value" for simplicity.
         */
        _is_tag = true;
        _is_value = true;
      }
      break;
    }

    break;
  }

  // restore path_comps. also set value/name for both valid and invalid nodes.
  if (val) {
    if (_is_multi) {
      _values.push_back(val);
    } else {
      _value = val;
    }
    path_comps.pop();
  }
  if (name && name[0]) {
    _name = name;
    path_comps.pop();
  }
}

CfgNode::CfgNode(Cstore& cstore, Cpath& path_comps, bool active,
                 bool recursive)
  : _is_tag(false), _is_leaf(false), _is_multi(false), _is_value(false),
    _is_default(false), _is_deactivated(false), _is_leaf_typeless(false),
    _is_invalid(false), _is_empty(false), _exists(true)
{
  /* first get the def (only if path is not empty). if path is empty, i.e.,
   * "root", treat it as an intermediate node.
   */
  if (path_comps.size() > 0) {
    tr1::shared_ptr<Ctemplate> def(cstore.parseTmpl(path_comps, false));
    if (def.get()) {
      // got the def
      if (!cstore.cfgPathExists(path_comps, active)) {
        // path doesn't exist
        _exists = false;
        return;
      }

      _is_value = def->isValue();
      _is_tag = def->isTag();
      _is_leaf = (!_is_tag && !def->isTypeless());
      _is_multi = def->isMulti();
      _is_default = cstore.cfgPathDefault(path_comps, active);
      _is_deactivated = cstore.cfgPathDeactivated(path_comps, active);
      cstore.cfgPathGetComment(path_comps, _comment, active);
      // ignore return

      if (_is_leaf && _is_value) {
        /* "leaf value" so recursion should never reach here. if path is
         * specified by user, nothing further to do.
         */
        return;
      }
    } else {
      // not a valid node
      _is_invalid = true;
      return;
    }
  }

  // handle leaf node (note path_comps must be non-empty if this is leaf)
  if (_is_leaf) {
    _name = path_comps[path_comps.size() - 1];
    if (_is_multi) {
      // multi-value node
      cstore.cfgPathGetValuesDA(path_comps, _values, active, true);
      // ignore return value
    } else {
      // single-value node
      cstore.cfgPathGetValueDA(path_comps, _value, active, true);
      // ignore return value
    }
    return;
  }

  // handle intermediate (typeless) or tag
  if (_is_value) {
    // tag value
    _name = path_comps[path_comps.size() - 2];
    _value = path_comps[path_comps.size() - 1];
  } else {
    // tag node or typeless node
    _name = (path_comps.size() > 0 ? path_comps[path_comps.size() - 1] : "");
  }

  // check child nodes
  vector<string> cnodes;
  cstore.cfgPathGetChildNodesDA(path_comps, cnodes, active, true);
  if (cnodes.size() == 0) {
    // empty subtree. done.
    vector<string> tcnodes;
    cstore.tmplGetChildNodes(path_comps, tcnodes);
    if (tcnodes.size() == 0) {
      // typeless leaf node
      _is_leaf_typeless = true;
    }
    _is_empty = true;
    return;
  }

  if (!recursive) {
    // nothing further to do
    return;
  }

  // recurse
  for (size_t i = 0; i < cnodes.size(); i++) {
    path_comps.push(cnodes[i]);
    CfgNode *cn = new CfgNode(cstore, path_comps, active, recursive);
    _child_nodes.push_back(cn);
    path_comps.pop();
  }
}

