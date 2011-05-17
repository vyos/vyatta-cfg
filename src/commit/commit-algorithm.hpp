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

#ifndef _COMMIT_ALGORITHM_HPP_
#define _COMMIT_ALGORITHM_HPP_
#include <vector>
#include <string>
#include <queue>
#include <tr1/memory>

#include <cnode/cnode-util.hpp>
#include <cstore/cpath.hpp>
#include <cstore/ctemplate.hpp>

// forward decl
namespace cnode {
class CfgNode;
}
namespace cstore {
class Cstore;
}

namespace commit {

using namespace cnode;
using namespace cstore;

enum CommitState {
  COMMIT_STATE_UNCHANGED,
  COMMIT_STATE_ADDED,
  COMMIT_STATE_DELETED,
  COMMIT_STATE_CHANGED
};

enum CommitTreeTraversalOrder {
  PRE_ORDER,
  POST_ORDER
};

enum CommitHook {
  PRE_COMMIT,
  POST_COMMIT,
  LAST
};

class CommitData {
public:
  CommitData();
  virtual ~CommitData() {}

  // setters
  void setCommitState(CommitState s);
  void setCommitPath(const Cpath& p, bool is_val, const std::string& val,
                     const std::string& name);
  void setCommitMultiValues(const std::vector<std::string>& values,
                            const std::vector<CommitState>& states);
  void setCommitValue(const std::string& val1, const std::string& val2,
                      bool def1, bool def2);
  void setCommitChildDeleteFailed();
  void setCommitCreateFailed();
  void setCommitSubtreeChanged();

  // getters
  CommitState getCommitState() const;
  Cpath getCommitPath() const;
  size_t numCommitMultiValues() const;
  std::string commitMultiValueAt(size_t idx) const;
  CommitState commitMultiStateAt(size_t idx) const;
  std::string commitValueBefore() const;
  std::string commitValueAfter() const;
  bool commitChildDeleteFailed() const;
  bool commitCreateFailed() const;
  bool commitSubtreeChanged() const;

  // for tmpl stuff
  void setTmpl(std::tr1::shared_ptr<cstore::Ctemplate> def);
  std::tr1::shared_ptr<cstore::Ctemplate> getTmpl() const;
  const vtw_def *getDef() const;
  unsigned int getPriority() const;
  void setPriority(unsigned int p);
  const vtw_node *getActions(vtw_act_type act, bool raw = false) const;
  bool isBeginEndNode() const;

private:
  std::tr1::shared_ptr<cstore::Ctemplate> _def;
  Cpath _commit_path;
  CommitState _commit_state;
  std::vector<std::string> _commit_values;
  std::vector<CommitState> _commit_values_states;
  std::pair<std::string, std::string> _commit_value;
  std::pair<bool, bool> _commit_default;
  bool _commit_create_failed;
  bool _commit_child_delete_failed;
  bool _commit_subtree_changed;
};

class PrioNode : public TreeNode<PrioNode> {
public:
  PrioNode(CfgNode *n);
  ~PrioNode() {}

  CfgNode *getCfgNode();
  unsigned int getPriority() const;
  CommitState getCommitState() const;
  Cpath getCommitPath() const;
  bool parentCreateFailed() const;
  bool succeeded() const;
  bool hasSubtreeFailure() const;
  bool hasSubtreeSuccess() const;

  void setCfgParent(CfgNode *p);
  void setSucceeded(bool succeeded);
  void setSubtreeFailure();
  void setSubtreeSuccess();

private:
  CfgNode *_node;
  CfgNode *_cfg_parent;
  bool _succeeded;
  bool _subtree_failure;
  bool _subtree_success;
};

template<bool for_delete> struct PrioNodeCmp {
  inline bool operator()(PrioNode *a, PrioNode *b) {
    return _is_after(a, b, Int2Type<for_delete>());
  }

  /* note: if comparing "for delete", use "<". if not for delete, use ">".
   *       if two nodes have the same priority, the ordering between them
   *       is not defined, i.e., can be either.
   */
  inline bool _is_after(PrioNode *a, PrioNode *b, Int2Type<false>) {
    return (a->getPriority() > b->getPriority());
  }
  inline bool _is_after(PrioNode *a, PrioNode *b, Int2Type<true>) {
    return (a->getPriority() < b->getPriority());
  }
};

typedef std::priority_queue<PrioNode *, std::vector<PrioNode *>,
                            PrioNodeCmp<false> > PrioQueueT;
typedef std::priority_queue<PrioNode *, std::vector<PrioNode *>,
                            PrioNodeCmp<true> > DelPrioQueueT;

typedef std::pair<CommitState, std::tr1::shared_ptr<Cpath> >
  CommittedPathT;
typedef std::vector<CommittedPathT> CommittedPathListT;

// exported functions
const char *getCommitHookDir(CommitHook hook);
CfgNode *getCommitTree(CfgNode *cfg1, CfgNode *cfg2, const Cpath& cur_path);
bool isCommitPathEffective(Cstore& cs, const Cpath& pcomps,
                           std::tr1::shared_ptr<Ctemplate> def,
                           bool in_active, bool in_working);
bool doCommit(Cstore& cs, CfgNode& cfg1, CfgNode& cfg2);

} // namespace commit

#endif /* _COMMIT_ALGORITHM_HPP_ */

