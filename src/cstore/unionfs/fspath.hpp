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

#ifndef _FSPATH_HPP_
#define _FSPATH_HPP_
#include <string>

#include <cstore/svector.hpp>

namespace cstore { // begin namespace cstore
namespace unionfs { // begin namespace unionfs

class FsPath {
public:
  FsPath() : _data() {};
  explicit FsPath(const char *full_path) : _data() { operator=(full_path); };
  explicit FsPath(const std::string& full_path) : _data() {
    operator=(full_path);
  };
  FsPath(const FsPath& p) : _data() { operator=(p); };
  ~FsPath() {};

  void push(const char *comp) { _data.push_back(comp); };
  void push(const std::string& comp) { _data.push_back(comp.c_str()); };
  void pop() { _data.pop_back(); };
  void pop(std::string& last) { _data.pop_back(last); };

  FsPath& operator=(const char *full_path) {
    _data = full_path;
    return *this;
  };
  FsPath& operator=(const std::string& full_path) {
    _data = full_path;
    return *this;
  };
  FsPath& operator=(const FsPath& p) {
    _data = p._data;
    return *this;
  };
  FsPath& operator/=(const FsPath& p) {
    _data /= p._data;
    return *this;
  }
  FsPath operator/(const FsPath& rhs) {
    FsPath lhs(*this);
    lhs /= rhs;
    return lhs;
  };

  bool operator==(const FsPath& rhs) const {
    return (_data == rhs._data);
  };

  size_t length() const { return _data.length(); };
  bool has_parent_path() const { return (_data.size() > 0); };
  const char *path_cstr() const { return _data.get_cstr(); };
  size_t hash() const { return _data.hash(); };

private:
  struct FsPathParams {
    static const char separator = '/';
    static const size_t static_num_elems = 24;
    static const size_t static_buf_len = 256;
  };

  cstore::svector<FsPathParams> _data;
};

struct FsPathHash {
  inline size_t operator()(const FsPath& p) const {
    return p.hash();
  };
};

} // end namespace unionfs
} // end namespace cstore

#endif /* _FSPATH_HPP_ */

