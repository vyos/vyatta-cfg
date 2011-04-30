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

#ifndef _CTEMPLATE_H_
#define _CTEMPLATE_H_

#include <tr1/memory>

#include <cli_cstore.h>

namespace cstore { // begin namespace cstore

class Ctemplate {
public:
  Ctemplate(std::tr1::shared_ptr<vtw_def> def)
    : _def(def), _is_value(false) {};
  ~Ctemplate() {};

  bool isValue() const { return _is_value; };
  bool isMulti() const { return _def->multi; };
  bool isTag() const { return _def->tag; };
  bool isTagNode() const { return (isTag() && !isValue()); };
  bool isTagValue() const { return (isTag() && isValue()); };
  bool isLeafValue() const { return (!isTag() && isValue()); };
  bool isTypeless(size_t tnum = 1) const {
    /* note: the current "multi-type" implementation only supports two types.
     *       the interface here is generalized so it can support more in the
     *       future.
     *
     *       isTypeless(i) implies isTypeless(j) for all (j > i).
     *       therefore, isTypeless() means the node has no types at all
     *       and is equivalent to isTypeless(1).
     *
     *       originally, some "users" of vtw_def checks both is_value and
     *       def_type for typeless nodes. this should not be necessary so
     *       here we only check def_type.
     */
    return ((tnum == 1) ? (_def->def_type == ERROR_TYPE)
                          : (_def->def_type2 == ERROR_TYPE));
  };
  bool isSingleLeafNode() const {
    return (!isValue() && !isMulti() && !isTag() && !isTypeless());
  };
  bool isSingleLeafValue() const {
    // assume isValue implies !isTypeless
    return (isValue() && !isMulti() && !isTag());
  };
  bool isMultiLeafNode() const {
    // assume isMulti implies !isTag && !isTypeless
    return (!isValue() && isMulti());
  };
  bool isMultiLeafValue() const {
    // assume isMulti implies !isTag && !isTypeless
    return (isValue() && isMulti());
  };

  size_t getNumTypes() const {
    return (isTypeless(1) ? 0 : (isTypeless(2) ? 1 : 2));
  };
  vtw_type_e getType(size_t tnum = 1) const {
    return ((tnum == 1) ? _def->def_type : _def->def_type2);
  };
  const char *getTypeName(size_t tnum = 1) const {
    return type_to_name(getType(tnum));
  };
  const char *getDefault() const { return _def->def_default; };
  const char *getNodeHelp() const { return _def->def_node_help; };
  const char *getEnumeration() const { return _def->def_enumeration; };
  const char *getAllowed() const { return _def->def_allowed; };
  const vtw_node *getActions(vtw_act_type act) const {
    return _def->actions[act].vtw_list_head;
  };
  const char *getCompHelp() const { return _def->def_comp_help; };
  const char *getValHelp() const { return _def->def_val_help; };
  unsigned int getTagLimit() const { return _def->def_tag; };
  unsigned int getMultiLimit() const { return _def->def_multi; };
  unsigned int getPriority() const { return _def->def_priority; };

  void setIsValue(bool is_val) { _is_value = is_val; };
  void setPriority(unsigned int p) const {
    /* this changes the parsed template and is only used during commit IF the
     * priority specified in the template violates the "hierarchical
     * constraint" and therefore needs to be changed.
     */
    _def->def_priority = p;
  }

  const vtw_def *getDef() const {
    /* XXX this is a hack for code that has not been converted and is still
     *     using vtw_def directly. this should go away once the transition
     *     is completed.
     */
    return _def.get();
  };

private:
  /* XXX ideally, template should be parsed directly into this class instead
   *     of wrapping the vtw_def struct in here. however, the legacy code
   *     (e.g., commit and code used by commit) still requires vtw_def, so
   *     need to keep it around for now until the transition is completed.
   *
   *     note that the use of shared_ptr deals with the memory of the vtw_def
   *     struct *itself*. however, various members of vtw_def are allocated
   *     dynamically by the parser and were never freed before, i.e., they
   *     have always been leaked since the beginning. such leaks are not going
   *     to be fixed by the shared_ptr use here.
   *
   *     once the transition is completed, vtw_def can be eliminated, and
   *     template data should be stored directly in this class using more
   *     suitable containers so that memory allocation/deallocation can be
   *     handled properly.
   */
  std::tr1::shared_ptr<vtw_def> _def;
  bool _is_value; /* whether the last path component is a "value". set by
                   * the cstore in get_parsed_tmpl().
                   */
};

} // end namespace cstore

#endif /* _CTEMPLATE_H_ */

