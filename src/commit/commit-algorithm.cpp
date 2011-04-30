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

#include <cstdio>
#include <vector>
#include <string>

#include <cli_cstore.h>
#include <commit/commit-algorithm.hpp>
#include <cnode/cnode-algorithm.hpp>

using namespace commit;
using namespace std;


////// static functions
static void
_set_node_commit_state(CfgNode& node, CommitState s, bool recursive)
{
  node.setCommitState(s);
  if (recursive) {
    for (size_t i = 0; i < node.numChildNodes(); i++) {
      _set_node_commit_state(*(node.childAt(i)), s, recursive);
    }
  }
}

static void
_set_node_commit_path(CfgNode& node, const Cpath &p, bool recursive)
{
  node.setCommitPath(p, node.isValue(), node.getValue(), node.getName());
  if (recursive) {
    for (size_t i = 0; i < node.numChildNodes(); i++) {
      _set_node_commit_path(*(node.childAt(i)), node.getCommitPath(),
                            recursive);
    }
  }
}

static void
_set_node_commit_child_delete_failed(CfgNode& node)
{
  // recursively bottom-up
  node.setCommitChildDeleteFailed();
  if (node.getParent()) {
    _set_node_commit_child_delete_failed(*(node.getParent()));
  }
}

static void
_set_node_commit_create_failed(CfgNode& node)
{
  // recursively top-down
  if (node.getCommitState() == COMMIT_STATE_ADDED) {
    // only set failure if the node is being created
    node.setCommitCreateFailed();
  }
  for (size_t i = 0; i < node.numChildNodes(); i++) {
    _set_node_commit_create_failed(*(node.childAt(i)));
  }
}

static size_t
_get_num_commit_multi_values(const CfgNode& node)
{
  return (node.getCommitState() == COMMIT_STATE_CHANGED
          ? node.numCommitMultiValues() : node.getValues().size());
}

static string
_get_commit_multi_value_at(const CfgNode& node, size_t idx)
{
  return (node.getCommitState() == COMMIT_STATE_CHANGED
          ? node.commitMultiValueAt(idx) : (node.getValues())[idx]);
}

static CommitState
_get_commit_multi_state_at(const CfgNode& node, size_t idx)
{
  CommitState s = node.getCommitState();
  return (s == COMMIT_STATE_CHANGED ? node.commitMultiStateAt(idx) : s);
}

// nodes other than "changed" leaf nodes
static CfgNode *
_create_commit_cfg_node(CfgNode& cn, const Cpath& p, CommitState s)
{
  CfgNode *node = new CfgNode(cn);
  _set_node_commit_state(*node, s, (s != COMMIT_STATE_UNCHANGED));
  _set_node_commit_path(*node, p, (s != COMMIT_STATE_UNCHANGED));
  if (s == COMMIT_STATE_UNCHANGED) {
    node->clearChildNodes();
  } else {
    for (size_t i = 0; i < node->numChildNodes(); i++) {
      node->childAt(i)->setParent(node);
    }
  }
  return node;
}

// "changed" multi-value leaf nodes
static CfgNode *
_create_commit_cfg_node(const CfgNode& cn, const Cpath& p,
                        const vector<string>& values,
                        const vector<CommitState>& states)
{
  CfgNode *node = new CfgNode(cn);
  _set_node_commit_state(*node, COMMIT_STATE_CHANGED, false);
  _set_node_commit_path(*node, p, false);
  node->setCommitMultiValues(values, states);
  return node;
}

// "changed" single-value leaf nodes
static CfgNode *
_create_commit_cfg_node(const CfgNode& cn, const Cpath& p, const string& val1,
                        const string& val2, bool def1, bool def2)
{
  CfgNode *node = new CfgNode(cn);
  _set_node_commit_state(*node, COMMIT_STATE_CHANGED, false);
  _set_node_commit_path(*node, p, false);
  node->setCommitValue(val1, val2, def1, def2);
  return node;
}

static void
_get_commit_prio_subtrees(CfgNode *sroot, PrioNode& parent)
{
  if (!sroot) {
    return;
  }

  PrioNode *pn = &parent;
  // need a local copy since nodes can be detached
  vector<CfgNode *> cnodes = sroot->getChildNodes();
  if (sroot->getPriority() && (sroot->isValue() || !sroot->isTag())) {
    // enforce hierarchical constraint
    unsigned int prio = sroot->getPriority();
    unsigned int pprio = parent.getPriority();
    if (prio <= pprio) {
      // can't have that => make it higher than parent priority
      OUTPUT_USER("Warning: priority inversion [%s](%u) <= [%s](%u)\n"
                  "         changing [%s] to (%u)\n",
                  sroot->getCommitPath().to_string().c_str(), prio,
                  parent.getCommitPath().to_string().c_str(), pprio,
                  sroot->getCommitPath().to_string().c_str(),
                  pprio + 1);
      sroot->setPriority(pprio + 1);
    }

    // only non-"tag node" applies ("tag nodes" not used in prio tree)
    pn = new PrioNode(sroot);
    /* record the original parent in config tree. this will be used to
     * enforce "hierarchical constraint" in the config. skip the tag node
     * if this is a tag value since commit doesn't act on tag nodes.
     */
    CfgNode *pnode = sroot->getParent();
    pn->setCfgParent(sroot->isTag() ? pnode->getParent() : pnode);
    parent.addChildNode(pn);
    sroot->detachFromParent();
  }

  for (size_t i = 0; i < cnodes.size(); i++) {
    _get_commit_prio_subtrees(cnodes[i], *pn);
  }
}

static void
_get_commit_prio_queue(PrioNode *proot, PrioQueueT& pq, DelPrioQueueT& dpq)
{
  if (!proot) {
    return;
  }
  if (proot->getCommitState() == COMMIT_STATE_DELETED) {
    dpq.push(proot);
  } else {
    pq.push(proot);
  }
  for (size_t i = 0; i < proot->numChildNodes(); i++) {
    _get_commit_prio_queue(proot->childAt(i), pq, dpq);
  }
}

template<class N> static bool
_trv_tag_node(N *node)
{
  return false;
}
template<> bool
_trv_tag_node<CfgNode>(CfgNode *node)
{
  return node->isTagNode();
}

template<class N> static bool
_trv_be_node(N *node)
{
  return false;
}
template<> bool
_trv_be_node<CfgNode>(CfgNode *node)
{
  return node->isTagNode();
}

template<class N> static void
_commit_tree_traversal(N *root, bool betree_only,
                       CommitTreeTraversalOrder order,
                       vector<N *>& nodelist, bool include_root = false,
                       bool init = true)
{
  /* note: commit traversal doesn't include "tag node", only "tag values".
   *       (this only applies when traversing CfgNode tree, not when
   *        traversing PrioNode tree, hence the specializations above.)
   *
   *       also, "include_root" controls if the "original root" is included.
   */
  if (order == PRE_ORDER && !_trv_tag_node(root) && include_root) {
    nodelist.push_back(root);
  }
  if (init || !betree_only || _trv_tag_node(root) || !_trv_be_node(root)) {
    for (size_t i = 0; i < root->numChildNodes(); i++) {
      _commit_tree_traversal(root->childAt(i), betree_only, order,
                             nodelist, true, false);
    }
  }
  if (order == POST_ORDER && !_trv_tag_node(root) && include_root) {
    nodelist.push_back(root);
  }
}

static bool
_exec_tmpl_actions(Cstore& cs, CommitState s, char *at_str,
                   const Cpath& path, const Cpath& disp_path,
                   const vtw_node *actions, const vtw_def *def)
{
  if (!actions) {
    // no actions => success
    return true;
  }

  /* XXX this follows the logic in original implementation since some
   * features are using it.
   */
  const char *aenv = "ACTIVE";
  bool in_del = false;
  switch (s) {
  case COMMIT_STATE_ADDED:
  case COMMIT_STATE_CHANGED:
    aenv = "SET";
    break;
  case COMMIT_STATE_DELETED:
    aenv = "DELETE";
    in_del = true;
    break;
  default:
    break;
  }
  setenv("COMMIT_ACTION", aenv, 1);
  set_in_delete_action(in_del);
  bool ret = cs.executeTmplActions(at_str, path, disp_path, actions, def);
  set_in_delete_action(false);
  unsetenv("COMMIT_ACTION");
  return ret;
}

/* execute the specified type of actions on the specified node.
 *
 * note that if the "committed list" clist is specified, no action will be
 * performed except adding the node to the committed list (if needed).
 */
static bool
_exec_node_actions(Cstore& cs, CfgNode& node, vtw_act_type act,
                   CommittedPathListT *clist = NULL)
{
  if (node.isMulti()) {
    // fail if this is called with a multi node
    OUTPUT_USER("_exec_node_actions() called with multi[%s]\n",
                node.getCommitPath().to_string().c_str());
    return false;
  }

  CommitState s = node.getCommitState();
  if (s == COMMIT_STATE_DELETED && node.commitChildDeleteFailed()) {
    return false;
  }

  bool nop = false;
  if (!node.getActions(act)) {
    // no actions => success
    nop = true;
  }

  auto_ptr<char> at_str;
  Cpath pcomps(node.getCommitPath());
  tr1::shared_ptr<Cpath> pdisp(new Cpath(pcomps));
  bool add_parent_to_committed = false;
  if (node.isLeaf()) {
    // single-value node
    if (s == COMMIT_STATE_CHANGED) {
      if (node.commitValueBefore() == node.commitValueAfter()) {
        // value didn't change (only "default" status), so nop
        nop = true;
      }
      at_str.reset(strdup(node.commitValueAfter().c_str()));
    } else {
      if (s == COMMIT_STATE_ADDED || s == COMMIT_STATE_DELETED) {
        // add parent to "committed list" if it's added/deleted
        add_parent_to_committed = true;
      }
      at_str.reset(strdup(node.getValue().c_str()));
    }
    pdisp->push(at_str.get());
  } else if (node.isValue()) {
    // tag value
    at_str.reset(strdup(node.getValue().c_str()));
    pcomps.pop(); // paths need to be at the "node" level
  } else {
    // typeless node
    at_str.reset(strdup(node.getName().c_str()));
  }

  if (clist) {
    /* note that even though every "tag value" will be added to the
     * "committed list" here, simply adding the corresponding "tag node"
     * here does not work.
     *
     * basically there are three scenarios for a tag node:
     *   (1) in both active and working
     *       i.e., tag node itself is unchanged => doesn't need to be added
     *       to committed list since "committed query" will check for this
     *       condition first.
     *   (2) only in working
     *       i.e., all tag values are being added so tag node itself is also
     *       being added. in this case, tag node must be considered
     *       "committed" after the first tag value has been processed
     *       successfully.
     *   (3) only in active
     *       i.e., all tag values are being deleted so is tag node itself.
     *       in this case, tag node must be considered "committed" only
     *       after all tag values have been processed successfully.
     *
     * cases (2) and (3) cannot be handled here since they depend on the
     * processing order and outcome of siblings of tag values. therefore,
     * tag node will never be added to the committed list, and the
     * "committed query" function will need to handle tag nodes as special
     * case.
     */
    if (add_parent_to_committed) {
      tr1::shared_ptr<Cpath> ppdisp(new Cpath(pcomps));
      clist->push_back(CommittedPathT(s, ppdisp));
    }
    clist->push_back(CommittedPathT(s, pdisp));
    return true;
  }

  if (nop) {
    // nothing to do
    return true;
  }

  if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                          node.getActions(act), node.getDef())) {
    if (act == create_act) {
      _set_node_commit_create_failed(node);
    }
    return false;
  }
  return true;
}

/* execute the specified type of actions on the specified "multi" node (i.e.,
 * multi-value leaf node) during commit. act is one of "delete_act",
 * "update_act", and "syntax_act", representing the different processing
 * passes.
 *
 * see comment for _exec_node_actions() above about clist.
 */
static bool
_exec_multi_node_actions(Cstore& cs, const CfgNode& node, vtw_act_type act,
                         CommittedPathListT *clist = NULL)
{
  if (!node.isMulti()) {
    // fail if this is called with a non-multi node
    OUTPUT_USER("_exec_multi_node_actions() called with non-multi[%s]\n",
                node.getCommitPath().to_string().c_str());
    return false;
  }

  const vtw_def *def = node.getDef();
  Cpath pcomps(node.getCommitPath());
  if (clist) {
    CommitState s = node.getCommitState();
    if (s == COMMIT_STATE_ADDED || s == COMMIT_STATE_DELETED) {
      /* for multi-value leaf node, add the node itself to the
       * "committed list" if it is added/deleted.
       */
      tr1::shared_ptr<Cpath> ppdisp(new Cpath(pcomps));
      clist->push_back(CommittedPathT(s, ppdisp));
    }
  }
  for (size_t i = 0; i < _get_num_commit_multi_values(node); i++) {
    CommitState s = _get_commit_multi_state_at(node, i);
    if (s == COMMIT_STATE_UNCHANGED) {
      // nop for unchanged value
      continue;
    }
    string v = _get_commit_multi_value_at(node, i);
    auto_ptr<char> at_str(strdup(v.c_str()));
    tr1::shared_ptr<Cpath> pdisp(new Cpath(pcomps));
    pdisp->push(v);

    if (clist) {
      // add the value to the committed list
      clist->push_back(CommittedPathT(s, pdisp));
      continue;
    }

    if (act == syntax_act) {
      // syntax pass
      if (s != COMMIT_STATE_ADDED) {
        continue;
      }
      if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                              node.getActions(syntax_act), def)) {
        return false;
      }
    } else {
      //// delete or update pass
      // begin
      if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                              node.getActions(begin_act), def)) {
        return false;
      }

      if (act == delete_act) {
        // delete pass
        if (s == COMMIT_STATE_DELETED) {
          if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                                  node.getActions(delete_act), def)) {
            return false;
          }
        }
      } else {
        // update pass
        if (s == COMMIT_STATE_ADDED) {
          if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                                  node.getActions(create_act), def)) {
            return false;
          }
        }
        // no "CHANGED" for value of multi-value leaf node
      }

      // end
      if (!_exec_tmpl_actions(cs, s, at_str.get(), pcomps, *(pdisp.get()),
                              node.getActions(end_act), def)) {
        return false;
      }
    }
  }
  return true;
}

static void
_set_commit_subtree_changed(CfgNode& node)
{
  // recursively bottom-up
  if (node.commitSubtreeChanged()) {
    // already set => terminate recursion
    return;
  }
  node.setCommitSubtreeChanged();
  if (node.getParent()) {
    _set_commit_subtree_changed(*(node.getParent()));
  }
}

static bool
_commit_check_cfg_node(Cstore& cs, CfgNode *node, CommittedPathListT& clist)
{
  vector<CfgNode *> nodelist;
  _commit_tree_traversal(node, false, PRE_ORDER, nodelist, true);
  for (size_t i = 0; i < nodelist.size(); i++) {
    CommitState s = nodelist[i]->getCommitState();
    if (s == COMMIT_STATE_UNCHANGED) {
      // nop
      continue;
    }
    _set_commit_subtree_changed(*(nodelist[i]));

    if (nodelist[i]->isMulti()) {
      // for committed list processing, use top_act as dummy value
      _exec_multi_node_actions(cs, *(nodelist[i]), top_act, &clist);
      if (!_exec_multi_node_actions(cs, *(nodelist[i]), syntax_act)) {
        return false;
      }
      continue;
    }
    if (s != COMMIT_STATE_UNCHANGED) {
      // for committed list processing, use top_act as dummy value
      _exec_node_actions(cs, *(nodelist[i]), top_act, &clist);
    }
    if (s == COMMIT_STATE_CHANGED || s == COMMIT_STATE_ADDED) {
      if (!_exec_node_actions(cs, *(nodelist[i]), syntax_act)) {
        return false;
      }
    }
  }
  return true;
}

static bool
_commit_exec_cfg_node(Cstore& cs, CfgNode *node)
{
  if (!node->commitSubtreeChanged()) {
    // nothing changed => nop
    return true;
  }

  if (node->isMulti()) {
    /* if reach here, this "multi" is being commited as a "top-level" node,
     * so need to do both "delete pass" and "update pass".
     */
    return (_exec_multi_node_actions(cs, *node, delete_act)
            ? (_exec_multi_node_actions(cs, *node, update_act)
               ? true : false) : false);
  }

  // begin
  if (!_exec_node_actions(cs, *node, begin_act)) {
    return false;
  }

  // delete pass (bottom-up)
  vector<CfgNode *> nodelist;
  _commit_tree_traversal(node, true, POST_ORDER, nodelist);
  for (size_t i = 0; i < nodelist.size(); i++) {
    if (nodelist[i]->isMulti()) {
      // do "delete pass" for "multi"
      if (!_exec_multi_node_actions(cs, *(nodelist[i]), delete_act)) {
        return false;
      }
      continue;
    }

    if (nodelist[i]->getCommitState() != COMMIT_STATE_DELETED) {
      continue;
    }
    if (nodelist[i]->isBeginEndNode()) {
      if (!_commit_exec_cfg_node(cs, nodelist[i])) {
        return false;
      }
    } else {
      if (!_exec_node_actions(cs, *(nodelist[i]), delete_act)) {
        return false;
      }
    }
  }

  CommitState s = node->getCommitState();
  if (s != COMMIT_STATE_UNCHANGED) {
    if (s == COMMIT_STATE_DELETED) {
      // delete self
      if (!_exec_node_actions(cs, *node, delete_act)) {
        return false;
      }
    } else {
      // create/update self
      vtw_act_type act = (s == COMMIT_STATE_ADDED ? create_act : update_act);
      if (!_exec_node_actions(cs, *node, act)) {
        return false;
      }
    }
  }

  // create/update pass (top-down)
  nodelist.clear();
  _commit_tree_traversal(node, true, PRE_ORDER, nodelist);
  for (size_t i = 0; i < nodelist.size(); i++) {
    if (nodelist[i]->isMulti()) {
      // do "update pass" for "multi"
      if (!_exec_multi_node_actions(cs, *(nodelist[i]), update_act)) {
        return false;
      }
      continue;
    }

    CommitState sc = nodelist[i]->getCommitState();
    if (sc == COMMIT_STATE_DELETED) {
      // deleted nodes already handled in previous loop
      continue;
    }
    if (nodelist[i]->isBeginEndNode()) {
      if (!_commit_exec_cfg_node(cs, nodelist[i])) {
        return false;
      }
    } else if (sc == COMMIT_STATE_UNCHANGED) {
      continue;
    } else {
      // added or changed
      vtw_act_type act = (sc == COMMIT_STATE_ADDED ? create_act : update_act);
      if (!_exec_node_actions(cs, *(nodelist[i]), act)) {
        return false;
      }
    }
  }

  // end
  if (!_exec_node_actions(cs, *node, end_act)) {
    return false;
  }
  return true;
}

static bool
_commit_exec_prio_subtree(Cstore& cs, PrioNode *proot)
{
  CfgNode *cfg = proot->getCfgNode();
  if (cfg) {
    if (proot->getCommitState() == COMMIT_STATE_ADDED
        && proot->parentCreateFailed()) {
      // can't create if parent create failed
      proot->setSucceeded(false);
      return false;
    }
    CommittedPathListT clist;
    if (!_commit_check_cfg_node(cs, cfg, clist)
        || !_commit_exec_cfg_node(cs, cfg)) {
      // subtree commit failed
      proot->setSucceeded(false);
      return false;
    }
    // subtree succeeded, mark nodes committed
    for (size_t i = 0; i < clist.size(); i++) {
      if (!cs.markCfgPathCommitted(*(clist[i].second.get()),
                                   (clist[i].first
                                    == COMMIT_STATE_DELETED))) {
        fprintf(stderr, "Failed to mark path committed\n");
        proot->setSucceeded(false);
        return false;
      }
    }
  }
  proot->setSucceeded(true);
  return true;
}

static CfgNode *
_get_commit_leaf_node(CfgNode *cfg1, CfgNode *cfg2, const Cpath& cur_path,
                      bool& is_leaf)
{
  if ((cfg1 && !cfg1->isLeaf()) || (cfg2 && !cfg2->isLeaf())) {
    // not a leaf node
    is_leaf = false;
    return NULL;
  }

  is_leaf = true;

  if (!cfg1) {
    return _create_commit_cfg_node(*cfg2, cur_path, COMMIT_STATE_ADDED);
  } else if (!cfg2) {
    return _create_commit_cfg_node(*cfg1, cur_path, COMMIT_STATE_DELETED);
  }

  if (cfg1->isMulti()) {
    // multi-value node
    vector<string> values;
    vector<DiffState> pfxs;
    if (!cnode::cmp_multi_values(cfg1, cfg2, values, pfxs)) {
      // no change
      return NULL;
    }
    vector<CommitState> states;
    for (size_t i = 0; i < pfxs.size(); i++) {
      if (pfxs[i] == DIFF_ADD) {
        states.push_back(COMMIT_STATE_ADDED);
      } else if (pfxs[i] == DIFF_DEL) {
        states.push_back(COMMIT_STATE_DELETED);
      } else {
        // "changed" for "show" is really "unchanged" for "commit"
        states.push_back(COMMIT_STATE_UNCHANGED);
      }
    }
    return _create_commit_cfg_node(*cfg1, cur_path, values, states);
  } else {
    // single-value node
    string val1 = cfg1->getValue();
    string val2 = cfg2->getValue();
    bool def1 = cfg1->isDefault();
    bool def2 = cfg2->isDefault();
    if (val1 == val2 && def1 == def2) {
      // no change
      return NULL;
    }
    return _create_commit_cfg_node(*cfg1, cur_path, val1, val2, def1, def2);
  }
}

static CfgNode *
_get_commit_other_node(CfgNode *cfg1, CfgNode *cfg2, const Cpath& cur_path)
{
  string name, value;
  bool not_tag_node, is_value, is_leaf_typeless;
  vector<CfgNode *> rcnodes1, rcnodes2;
  cnode::cmp_non_leaf_nodes(cfg1, cfg2, rcnodes1, rcnodes2, not_tag_node,
                            is_value, is_leaf_typeless, name, value);

  if (!cfg1) {
    return _create_commit_cfg_node(*cfg2, cur_path, COMMIT_STATE_ADDED);
  } else if (!cfg2) {
    return _create_commit_cfg_node(*cfg1, cur_path, COMMIT_STATE_DELETED);
  }

  CfgNode *cn = _create_commit_cfg_node(*cfg1, cur_path,
                                        COMMIT_STATE_UNCHANGED);
  for (size_t i = 0; i < rcnodes1.size(); i++) {
    CfgNode *cnode = getCommitTree(rcnodes1[i], rcnodes2[i],
                                      cn->getCommitPath());
    if (cnode) {
      cn->addChildNode(cnode);
    }
  }
  if (cn->numChildNodes() < 1) {
    delete cn;
    return NULL;
  }
  return cn;
}


////// class CommitData
CommitData::CommitData()
  : _commit_state(COMMIT_STATE_UNCHANGED), _commit_create_failed(false),
    _commit_child_delete_failed(false), _commit_subtree_changed(false)
{
}

// member setters
void
CommitData::setCommitState(CommitState s)
{
  _commit_state = s;
}

void
CommitData::setCommitPath(const Cpath& p, bool is_val, const string& val,
                          const string& name)
{
  Cpath cp(p);
  if (is_val) {
    cp.push(val);
  } else if (name.size() > 0) {
    cp.push(name);
  }
  _commit_path = cp;
}

void
CommitData::setCommitMultiValues(const vector<string>& values,                                                   const vector<CommitState>& states)
{
  _commit_values = values;
  _commit_values_states = states;
}

void
CommitData::setCommitValue(const string& val1, const string& val2,                                         bool def1, bool def2)
{
  _commit_value.first = val1;
  _commit_value.second = val2;
  _commit_default.first = def1;
  _commit_default.second = def2;
}

void
CommitData::setCommitChildDeleteFailed()
{
  _commit_child_delete_failed = true;
}

void
CommitData::setCommitCreateFailed()
{
  _commit_create_failed = true;
}

void
CommitData::setCommitSubtreeChanged()
{
  _commit_subtree_changed = true;
}

// member getters
CommitState
CommitData::getCommitState() const
{
  return _commit_state;
}

Cpath
CommitData::getCommitPath() const
{
  return _commit_path;
}

size_t
CommitData::numCommitMultiValues() const
{
  return _commit_values.size();
}

string
CommitData::commitMultiValueAt(size_t idx) const
{
  return _commit_values[idx];
}

CommitState
CommitData::commitMultiStateAt(size_t idx) const
{
  return _commit_values_states[idx];
}

string
CommitData::commitValueBefore() const
{
  return _commit_value.first;
}

string
CommitData::commitValueAfter() const
{
  return _commit_value.second;
}

bool
CommitData::commitChildDeleteFailed() const
{
  return _commit_child_delete_failed;
}

bool
CommitData::commitCreateFailed() const
{
  return _commit_create_failed;
}

bool
CommitData::commitSubtreeChanged() const
{
  return _commit_subtree_changed;
}

// member functions for tmpl stuff
void
CommitData::setTmpl(tr1::shared_ptr<Ctemplate> def)
{
  _def = def;
}

tr1::shared_ptr<Ctemplate>
CommitData::getTmpl() const
{
  return _def;
}

const vtw_def *
CommitData::getDef() const
{
  return (_def.get() ? _def.get()->getDef() : NULL);
}

unsigned int
CommitData::getPriority() const
{
  return (_def.get() ? _def->getPriority() : 0);
}

void
CommitData::setPriority(unsigned int p)
{
  if (!_def.get()) {
    return;
  }
  _def->setPriority(p);
}

const vtw_node *
CommitData::getActions(vtw_act_type act, bool raw) const
{
  if (!_def.get()) {
    return NULL;
  }
  if (!raw && act == create_act && !_def->getActions(act)) {
    act = update_act;
  }
  return _def->getActions(act);
}

bool
CommitData::isBeginEndNode() const
{
  return (getActions(begin_act) || getActions(end_act));
}


////// class PrioNode
PrioNode::PrioNode(CfgNode *n)
  : TreeNode<PrioNode>(), _node(n), _cfg_parent(0),
    _succeeded(true), _subtree_failure(false), _subtree_success(false)
{
}

CfgNode *
PrioNode::getCfgNode()
{
  return _node;
}

unsigned int
PrioNode::getPriority() const
{
  return (_node ? _node->getPriority() : 0);
}

CommitState
PrioNode::getCommitState() const
{
  return (_node ? _node->getCommitState() : COMMIT_STATE_UNCHANGED);
}

Cpath
PrioNode::getCommitPath() const
{
  return (_node ? _node->getCommitPath() : Cpath());
}

bool
PrioNode::parentCreateFailed() const
{
  return (_cfg_parent ? _cfg_parent->commitCreateFailed() : false);
}

bool
PrioNode::succeeded() const
{
  return _succeeded;
}

bool
PrioNode::hasSubtreeFailure() const
{
  return _subtree_failure;
}

bool
PrioNode::hasSubtreeSuccess() const
{
  return _subtree_success;
}

void
PrioNode::setCfgParent(CfgNode *p)
{
  _cfg_parent = p;
}

void
PrioNode::setSucceeded(bool succeeded)
{
  if (succeeded) {
    if (getParent()) {
      getParent()->setSubtreeSuccess();
    }
    return;
  }

  // failed
  _succeeded = false;
  if (getParent()) {
    getParent()->setSubtreeFailure();
  }
  if (getCommitState() == COMMIT_STATE_DELETED && _cfg_parent) {
    /* this will recursively set child_delete_failed on the "config parent"
     * (which should be in the parent prio subtree) and all its ancestors.
     * it will be used to prevent anything above this from being deleted
     * so that the hierarchical structure can be preserved.
     */
    _set_node_commit_child_delete_failed(*_cfg_parent);
  }
  if (_node) {
    /* this will recursively set create_failed for all nodes that are "being
     * created", i.e., in COMMIT_STATE_ADDED state.
     * in other words, if a prio subtree fails, any "create" in the subtree
     * is considered failed and therefore any prio subtree under those
     * cannot be created.
     */
    _set_node_commit_create_failed(*_node);
  }
}

void
PrioNode::setSubtreeFailure()
{
  if (_subtree_failure) {
    // already set => terminate recursion
    return;
  }

  _subtree_failure = true;
  if (getParent()) {
    getParent()->setSubtreeFailure();
  }
}

void
PrioNode::setSubtreeSuccess()
{
  if (_subtree_success) {
    // already set => terminate recursion
    return;
  }

  _subtree_success = true;
  if (getParent()) {
    getParent()->setSubtreeSuccess();
  }
}


////// exported functions
CfgNode *
commit::getCommitTree(CfgNode *cfg1, CfgNode *cfg2, const Cpath& cur_path)
{
  // if doesn't exist or is deactivated, treat as NULL
  if (cfg1 && (!cfg1->exists() || cfg1->isDeactivated()) ) {
    cfg1 = NULL;
  }
  if (cfg2 && (!cfg2->exists() || cfg2->isDeactivated())) {
    cfg2 = NULL;
  }

  if (!cfg1 && !cfg2) {
    fprintf(stderr, "getCommitTree error (both config NULL)\n");
    exit(1);
  }

  bool is_leaf = false;
  CfgNode *cn = _get_commit_leaf_node(cfg1, cfg2, cur_path, is_leaf);
  if (!is_leaf) {
    // intermediate node, tag node, or tag value
    cn = _get_commit_other_node(cfg1, cfg2, cur_path);
  }
  return cn;
}

bool
commit::isCommitPathEffective(Cstore& cs, const Cpath& pcomps,
                              tr1::shared_ptr<Ctemplate> def,
                              bool in_active, bool in_working)
{
  if (in_active && in_working) {
    // remain the same
    return true;
  }
  if (!in_active && !in_working) {
    // doesn't exist
    return false;
  }
  // at this point, in_active corresponds to "being deleted"

  if (def->isTagNode()) {
    // special handling for tag nodes, which are never marked
    vector<string> tvals;
    // get tag values from active or working config
    cs.cfgPathGetChildNodes(pcomps, tvals, in_active);
    Cpath vpath(pcomps);
    /* note that there should be at least 1 tag value since tag node
     * cannot exist without tag value.
     */
    for (size_t i = 0; i < tvals.size(); i++) {
      vpath.push(tvals[i]);
      if (in_active) {
        // being deleted => all tag values are being deleted
        if (!cs.cfgPathMarkedCommitted(vpath, true)) {
          /* a tag value is not marked committed
           * => a tag value has not been deleted
           * => tag node has not been deleted
           * => still effective
           */
          return true;
        }
      } else {
        // being added => all tag values are being added
        if (cs.cfgPathMarkedCommitted(vpath, false)) {
          /* a tag value is marked committed
           * => a tag value has been added
           * => tag node has been added
           * => already effective
           */
          return true;
        }
      }
      vpath.pop();
    }
    // not effective
    return false;
  }

  /* if not tag node, effectiveness corresponds to committed marking:
   *   if deleted (i.e., in_active), then !marked is effective
   *   otherwise (i.e., added), marked is effective
   */
  bool marked = cs.cfgPathMarkedCommitted(pcomps, in_active);
  return (in_active ? !marked : marked);
}

bool
commit::doCommit(Cstore& cs, CfgNode& cfg1, CfgNode& cfg2)
{
  Cpath p;
  CfgNode *root = getCommitTree(&cfg1, &cfg2, p);
  if (!root) {
    OUTPUT_USER("No configuration changes to commit\n");
    return true;
  }

  set_in_commit(true);

  PrioNode proot(root); // proot corresponds to root
  _get_commit_prio_subtrees(root, proot);
  // at this point all prio nodes have been detached from root
  PrioQueueT pq;
  DelPrioQueueT dpq;
  _get_commit_prio_queue(&proot, pq, dpq);
  size_t s = 0, f = 0;
  while (!dpq.empty()) {
    PrioNode *p = dpq.top();
    if (!_commit_exec_prio_subtree(cs, p)) {
      // prio subtree failed
      ++f;
    } else {
      // succeeded
      ++s;
    }
    dpq.pop();
  }
  while (!pq.empty()) {
    PrioNode *p = pq.top();
    if (!_commit_exec_prio_subtree(cs, p)) {
      // prio subtree failed
      ++f;
    } else {
      // succeeded
      ++s;
    }
    pq.pop();
  }
  bool ret = true;
  if (f > 0) {
    OUTPUT_USER("Commit failed\n");
    ret = false;
  }

  if (!cs.commitConfig(proot)) {
    OUTPUT_USER("Failed to generate committed config\n");
    ret = false;
  }

  set_in_commit(false);
  if (!cs.clearCommittedMarkers()) {
    OUTPUT_USER("Failed to clear committed markers\n");
    ret = false;
  }
  return ret;
}

