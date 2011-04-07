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

#ifndef _CPATH_HPP_
#define _CPATH_HPP_
#include <string>

#include <cstore/svector.hpp>

namespace cstore { // begin namespace cstore

class Cpath {
public:
  Cpath() : _data() {};
  Cpath(const Cpath& p) : _data() { operator=(p); };
  Cpath(const char *comps[], size_t num_comps) : _data() {
    for (size_t i = 0; i < num_comps; i++) {
      push(comps[i]);
    }
  };
  ~Cpath() {};

  void push(const char *comp) { _data.push_back(comp); };
  void push(const std::string& comp) { _data.push_back(comp.c_str()); };
  void pop() { _data.pop_back(); };
  void pop(std::string& last) { _data.pop_back(last); };
  void clear() { _data.assign("", 0); };

  Cpath& operator=(const Cpath& p) {
    _data = p._data;
    return *this;
  };
  Cpath& operator/=(const Cpath& p) {
    _data /= p._data;
    return *this;
  }
  Cpath operator/(const Cpath& rhs) {
    Cpath lhs(*this);
    lhs /= rhs;
    return lhs;
  };

  bool operator==(const Cpath& rhs) const {
    return (_data == rhs._data);
  };
  const char *operator[](size_t idx) const {
    return _data[idx];
  };

  size_t size() const { return _data.size(); };
  size_t hash() const { return _data.hash(); };
  const char *back() const {
    return (size() > 0 ? _data[size() - 1] : NULL);
  };
  std::string to_string() const { return _data.to_string(); };

private:
  struct CpathParams {
    static const char separator = 0;
    static const size_t static_num_elems = 24;
    static const size_t static_buf_len = 256;
  };

  cstore::svector<CpathParams> _data;
};

struct CpathHash {
  inline size_t operator()(const Cpath& p) const {
    return p.hash();
  };
};

} // end namespace cstore

#endif /* _CPATH_HPP_ */

