#ifndef DOM
#define DOM

#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include "../ltm.h"

namespace dom {

using std::initializer_list;
using std::cerr;
using std::endl;
using std::string;
using std::vector;
using std::move;
using std::min;
using ltm::Object;
using ltm::own;
using ltm::weak;
using ltm::pin;
using std::unordered_map;
using std::function;
class TypeInfo;
class FieldInfo;
class DomItem;

class Name : public Object
{
  friend class Dom;
public:
  pin<Name> peek(const string& name);
  pin<Name> get_or_create(const string& name);

  const weak<Name> domain;
  const string name;
protected:
  unordered_map<string, own<Name>> sub;

  Name(pin<Name> domain, string name)
    : domain(domain), name(name) { make_shared(); }
  LTM_COPYABLE(Name)
};

class TypeInfo : public Object
{
public:
  enum Type
  {
    EMPTY, INT, UINT, FLOAT, BOOL, STRING, OWN, WEAK, VAR_ARRAY, ATOM, FIX_ARRAY, STRUCT,
  };
  // all
  TypeInfo() { make_shared(); }
  virtual Type get_type() =0;
  virtual size_t get_size() = 0;
  virtual void init(char*) = 0;
  virtual void dispose(char*) {};
  virtual void move(char* src, char* dst) = 0;
  virtual void copy(char* src, char* dst) = 0;

  // array
  virtual pin<TypeInfo> get_element_type(){ report_error("unsupported get_element_type"); return empty; }
  virtual char* get_element_ptr(size_t index, char*){ report_error("unsupported get_element"); return nullptr; }
  virtual size_t get_elements_count(char*){ report_error("unsupported get_elements_count"); return 0; }
  virtual void set_elements_count(size_t count, char*){ report_error("unsupported set_elements_count"); }
  // struct
  virtual pin<Name> get_name() { report_error("unsupported get_name"); return nullptr; }
  virtual void for_fields(function<void(pin<FieldInfo>)>){ report_error("unsupported for_fields"); }
  virtual size_t get_fields_count(){ report_error("unsupported get_fields_count"); return 0; }
  virtual pin<FieldInfo> get_field(pin<Name>);
  virtual pin<DomItem> create_instance() { report_error("unsupported create_instance"); return nullptr; }
  // primitives
  virtual int64_t get_int(char*){ report_error("unsupported get_int"); return 0; }
  virtual void set_int(int64_t v, char*){ report_error("unsupported set_int"); }
  virtual uint64_t get_uint(char*){ report_error("unsupported get_uint"); return 0; }
  virtual void set_uint(uint64_t v, char*){ report_error("unsupported set_uint"); }
  virtual double get_float(char*){ report_error("unsupported get_float"); return 0; }
  virtual void set_float(double v, char*){ report_error("unsupported set_float"); }
  virtual bool get_bool(char*){ report_error("unsupported get_bool"); return false; }
  virtual void set_bool(bool v, char*){ report_error("unsupported set_bool"); }
  virtual pin<DomItem> get_ptr(char*){ report_error("unsupported get_ptr"); return nullptr; }
  virtual void set_ptr(const pin<DomItem>& v, char*){ report_error("unsupported set_ptr"); }
  virtual string get_string(char*){ report_error("unsupported get_str"); return ""; }
  virtual void set_string(string v, char*){ report_error("unsupported set_str"); }
  virtual pin<Name> get_atom(char*) { report_error("unsupported set_atom"); return nullptr; }
  virtual void set_atom(pin<Name>, char*) { report_error("unsupported set_atom"); }

  virtual void report_error(string message) { cerr << message << endl; }
  static const own<TypeInfo> empty;
};

class FieldInfo : public Object
{
  friend class StructType;
public:
  FieldInfo(pin<Name> name, pin<TypeInfo> type) : name(name), type(type) {}
  virtual char* get_data(char* struct_ptr){ return struct_ptr + offset; }

  const own<Name> name;
  const own<TypeInfo> type;
  static const own<FieldInfo> empty;

protected:
  ptrdiff_t offset = 0;
  LTM_COPYABLE(FieldInfo)
};

class DomItem: public Object
{
  friend class Dom;

protected:
  virtual pin<TypeInfo> get_type() =0;
  virtual char* get_data() { return reinterpret_cast<char*>(this); }
};

class Dom: public Object {
public:
  Dom();
  pin<Name> names() { return root_name; }
  pin<TypeInfo> get_type(TypeInfo::Type type, size_t size = 0, pin<TypeInfo> item = nullptr);
  pin<TypeInfo> get_struct_type(pin<Name> name, vector<pin<FieldInfo>>& fields);

  void set_name(pin<DomItem> item, pin<Name> name);
  pin<Name> get_name(pin<DomItem> p);
  pin<DomItem> get_named(const pin<Name>& name);

  static pin<TypeInfo> get_type(const pin<DomItem>& item) { return item ? item->get_type() : TypeInfo::empty; }
  static char* get_data(const pin<DomItem>& item) { return item ? item->get_data() : nullptr; }
  bool sealed;
  
protected:
  own<Name> root_name = new Name(nullptr, "");
  unordered_map<weak<DomItem>, own<Name>> object_names;
  unordered_map<own<Name>, weak<DomItem>> named_objects;
  own<TypeInfo> atom_type, bool_type, string_type, own_ptr_type, weak_ptr_type;
  own<TypeInfo> int8_type, int16_type, int32_type, int64_type;
  own<TypeInfo> uint8_type, uint16_type, uint32_type, uint64_type;
  own<TypeInfo> float32_type, float64_type;
  unordered_map<own<TypeInfo>, own<TypeInfo>> var_arrays;
  unordered_map<own<Name>, own<TypeInfo>> named_types;
  unordered_map<own<TypeInfo>, unordered_map<size_t, own<TypeInfo>>> fixed_arrays;
  LTM_COPYABLE(Dom)
};

} // namespace dom

namespace std {

inline string to_string(const dom::Name& name) {
  auto domain = name.domain.pinned();
  return domain ? to_string(*domain) + "." + name.name : name.name;
}

} // namespace std

#endif // DOM
