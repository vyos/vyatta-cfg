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

#ifndef _SVECTOR_HPP_
#define _SVECTOR_HPP_
#include <cstdlib>
#include <cstring>
#include <string>
#include <tr1/functional>

namespace cstore {

template<int v>
struct Int2Type {
  enum {
    value = v
  };
};

template<class P>
class svector {
public:
  static const char ELEM_SEP = P::separator;
  static const bool RAW_CSTR_DATA = (ELEM_SEP != 0);
  static const bool RANDOM_ACCESS = (ELEM_SEP == 0);
  static const size_t STATIC_NUM_ELEMS = P::static_num_elems;
  static const size_t STATIC_BUF_LEN = P::static_buf_len;

  svector();
  svector(const svector<P>& v);
  svector(const char *raw_cstr);
  svector(const std::string& str);
  svector(const char *raw_data, size_t dlen);
  ~svector();

  void push_back(const char *e);
  void pop_back();
  void pop_back(std::string& last);

  svector<P>& operator=(const char *raw_cstr) {
    assign(raw_cstr);
    return *this;
  };
  svector<P>& operator=(const std::string& str) {
    return operator=(str.c_str());
  };
  svector<P>& operator=(const svector<P>& v);
  svector<P>& operator/=(const svector<P>& v);
  svector<P> operator/(const svector<P>& v) {
    svector<P> lhs(*this);
    lhs /= v;
    return lhs;
  };
  void assign(const char *raw_cstr) {
    assign_raw_cstr(raw_cstr, Int2Type<RAW_CSTR_DATA>());
  };
  void assign(const char *raw_data, size_t dlen);

  bool operator==(const svector<P>& rhs) const {
    return (_len == rhs._len && memcmp(_data, rhs._data, _len) == 0);
  };
  const char *operator[](size_t idx) const {
    return elem_at(idx, Int2Type<RANDOM_ACCESS>());
  };

  size_t size() const { return _num_elems; };
  size_t length() const { return _len; };
  const char *get_cstr() const {
    return get_raw_cstr(Int2Type<RAW_CSTR_DATA>());
  };
  const char *get_data(size_t& dlen) const {
    dlen = _len;
    return _data;
  };
  size_t hash() const {
    return std::tr1::_Fnv_hash<sizeof(size_t)>::hash(_data, _len);
  };

private:
  size_t _num_elems;
  size_t _len;
  size_t _ebuf_size;
  size_t _buf_size;
  char **_elems;
  char *_elems_buf[STATIC_NUM_ELEMS];
  char **_elems_dbuf;
  char *_data;
  char _data_buf[STATIC_BUF_LEN];
  char *_data_dbuf;

  void grow_data();
  void grow_elems();
  void inc_num_elems() {
    ++_num_elems;
    if (_num_elems >= _ebuf_size) {
      grow_elems();
    }
  };
  void assign_raw_cstr(const char *raw_cstr, Int2Type<true>) {
    assign(raw_cstr, strlen(raw_cstr));
  };
  const char *get_raw_cstr(Int2Type<true>) const {
    return _data;
  };
  const char *elem_at(size_t idx, Int2Type<true>) const {
    return (idx < _num_elems ? (_elems[idx] + 1) : NULL);
  }
};

template<class P>
svector<P>::svector()
  : _num_elems(0), _len(0), _ebuf_size(STATIC_NUM_ELEMS),
    _buf_size(STATIC_BUF_LEN), _elems(_elems_buf), _elems_dbuf(0),
    _data(_data_buf), _data_dbuf(0)
{
  _elems[0] = _data;
  _data[0] = 0;
}

template<class P>
svector<P>::svector(const svector<P>& v)
  : _num_elems(0), _len(0), _ebuf_size(STATIC_NUM_ELEMS),
    _buf_size(STATIC_BUF_LEN), _elems(_elems_buf), _elems_dbuf(0),
    _data(_data_buf), _data_dbuf(0)
{
  _elems[0] = _data;
  _data[0] = 0;
  operator=(v);
}

template<class P>
svector<P>::svector(const char *raw_cstr)
  : _num_elems(0), _len(0), _ebuf_size(STATIC_NUM_ELEMS),
    _buf_size(STATIC_BUF_LEN), _elems(_elems_buf), _elems_dbuf(0),
    _data(_data_buf), _data_dbuf(0)
{
  assign(raw_cstr);
}

template<class P>
svector<P>::svector(const std::string& str)
  : _num_elems(0), _len(0), _ebuf_size(STATIC_NUM_ELEMS),
    _buf_size(STATIC_BUF_LEN), _elems(_elems_buf), _elems_dbuf(0),
    _data(_data_buf), _data_dbuf(0)
{
  assign(str.c_str());
}

template<class P>
svector<P>::svector(const char *raw_data, size_t dlen)
  : _num_elems(0), _len(0), _ebuf_size(STATIC_NUM_ELEMS),
    _buf_size(STATIC_BUF_LEN), _elems(_elems_buf), _elems_dbuf(0),
    _data(_data_buf), _data_dbuf(0)
{
  assign(raw_data, dlen);
}

template<class P>
svector<P>::~svector()
{
  if (_elems_dbuf) {
    delete [] _elems_dbuf;
  }
  if (_data_dbuf) {
    delete [] _data_dbuf;
  }
}

template<class P> void
svector<P>::push_back(const char *e)
{
  size_t elen = strlen(e);
  while ((_len + elen + 2) >= _buf_size) {
    // make sure there's space for (data + SEP + e + 0)
    grow_data();
  }

  char *start = _elems[_num_elems];
  *start = ELEM_SEP;
  memcpy(start + 1, e, elen + 1);
  inc_num_elems();
  _len += (elen + 1);
  _elems[_num_elems] = start + 1 + elen;
}

template<class P> void
svector<P>::pop_back()
{
  if (_num_elems == 0) {
    return;
  }
  --_num_elems;
  _len = _elems[_num_elems] - _data;
  _data[_len] = 0;
}

template<class P> void
svector<P>::pop_back(std::string& last)
{
  if (_num_elems == 0) {
    return;
  }
  last = _elems[_num_elems - 1] + 1;
  pop_back();
}

template<class P> svector<P>&
svector<P>::operator=(const svector<P>& v)
{
  if (this == &v) {
    return *this;
  }

  while (_buf_size < v._buf_size) {
    grow_data();
  }
  if (_ebuf_size >= v._ebuf_size) {
    _num_elems = v._num_elems;
  } else {
    _num_elems = 0;
    while (_num_elems < v._num_elems) {
      inc_num_elems();
    }
  }
  _len = v._len;
  memcpy(_data, v._data, _len + 1);
  memcpy(_elems, v._elems, sizeof(char *) * (_num_elems + 1));
  const char *o0 = _elems[0];
  for (size_t i = 0; i <= _num_elems; i++) {
    _elems[i] = _data + (_elems[i] - o0);
  }
  return *this;
}

template<class P> svector<P>&
svector<P>::operator/=(const svector<P>& v)
{
  while ((_len + v._len + 1) >= _buf_size) {
    // make sure there's space for (data + data + 0)
    grow_data();
  }

  size_t v_num_elems = v._num_elems;
  size_t olen = _len;
  memcpy(_data + _len, v._data, v._len + 1);
  _len += v._len;
  for (size_t i = 1; i <= v_num_elems; i++) {
    inc_num_elems();
    _elems[_num_elems] = _data + olen + (v._elems[i] - v._data);
  }
  return *this;
}

template<class P> void
svector<P>::assign(const char *raw_data, size_t dlen)
{
  _num_elems = 0;
  _len = 0;
  _elems[0] = _data;
  if (dlen == 0 || (dlen == 1 && raw_data[0] == ELEM_SEP)) {
    _data[0] = 0;
    return;
  }

  while ((dlen + 2) >= _buf_size) {
    // make sure there's space for (SEP + raw_data + 0)
    grow_data();
  }
  _len = dlen;

  if (raw_data[0] != ELEM_SEP) {
    _data[0] = ELEM_SEP;
    memcpy(_data + 1, raw_data, _len + 1);
    ++_len;
  } else {
    memcpy(_data, raw_data, _len + 1);
  }
  for (size_t i = 1; i < _len; i++) {
    if (_data[i] == ELEM_SEP) {
      inc_num_elems();
      _elems[_num_elems] = &(_data[i]);
    }
  }
  inc_num_elems();
  _elems[_num_elems] = _data + _len;
}

template<class P> void
svector<P>::grow_data()
{
  char *tmp = new char[_buf_size + STATIC_BUF_LEN];
  if (!tmp) {
    // doomed
    exit(1);
  }
  memcpy(tmp, _data, _len + 1);

  if (tmp != _data_dbuf) {
    for (size_t i = 0; i <= _num_elems; i++) {
      _elems[i] = tmp + (_elems[i] - _data);
    }
  }
  
  if (_data_dbuf) {
    delete [] _data_dbuf;
  }
  _data_dbuf = tmp;
  _data = _data_dbuf;
  _buf_size += STATIC_BUF_LEN;
}

template<class P> void
svector<P>::grow_elems()
{
  char **tmp = new char *[_ebuf_size + STATIC_NUM_ELEMS];
  if (!tmp) {
    // doomed
    exit(1);
  }
  memcpy(tmp, _elems, _ebuf_size * sizeof(char *));
  if (_elems_dbuf) {
    delete [] _elems_dbuf;
  }
  _elems_dbuf = tmp;
  _elems = _elems_dbuf;
  _ebuf_size += STATIC_NUM_ELEMS;
}

} // end namespace cstore

#endif /* _SVECTOR_HPP_ */

