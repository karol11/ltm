#include "dom.h"

namespace dom {

pin<FieldInfo> TypeInfo::get_field(pin<Name>) {
  report_error("unsupported get_field"); return FieldInfo::empty;
}

pin<Name> Name::peek(const string& name) {
  auto it = sub.find(name);
  return it == sub.end() ? nullptr : it->second;
}

pin<Name> Name::get_or_create(const string& name) {
  auto it = sub.find(name);
  return it == sub.end() || !it->second ? sub[name] = new Name(this, name) : it->second;
}

template<typename T, TypeInfo::Type ID>
class PrimitiveType : public TypeInfo
{
public:
  Type get_type() override { return ID; }
  void init(char* data) override { new(data) T(); }
  void dispose(char* data) { reinterpret_cast<T*>(data)->T::~T(); }
  size_t get_size() override { return sizeof(T); }
  void move(char* src, char* dst) override { new(dst) T(std::move(*reinterpret_cast<T*>(src))); };
  void copy(char* src, char* dst) override { new(dst) T(*reinterpret_cast<T*>(src)); };
};

class StringType : public PrimitiveType<string, TypeInfo::STRING>
{
public:
  string get_string(char* data) override { return *reinterpret_cast<string*>(data); }
  void set_string(string v, char* data) override { *reinterpret_cast<string*>(data) = std::move(v); }
  void init(char* data) override { new(data) string; }
  LTM_COPYABLE(StringType)
};

template<typename T>
class IntType : public PrimitiveType<T, TypeInfo::INT>
{
public:
  int64_t get_int(char* data) override { return *reinterpret_cast<T*>(data); }
  void set_int(int64_t v, char* data) override { *reinterpret_cast<T*>(data) = T(v); }
  LTM_COPYABLE(IntType)
};

template<typename T>
class UIntType : public PrimitiveType<T, TypeInfo::UINT>
{
public:
  uint64_t get_uint(char* data) override { return *reinterpret_cast<T*>(data); }
  void set_uint(uint64_t v, char* data) override { *reinterpret_cast<T*>(data) =T(v); }
  LTM_COPYABLE(UIntType)
};

template<typename T>
class FloatType : public PrimitiveType<T, TypeInfo::FLOAT>
{
public:
  double get_float(char* data) override { return *reinterpret_cast<T*>(data); }
  void set_float(double v, char* data) override { *reinterpret_cast<T*>(data) = T(v); }
  LTM_COPYABLE(FloatType)
};

class BoolType : public PrimitiveType<bool, TypeInfo::BOOL>
{
public:
  bool get_bool(char* data) override { return *reinterpret_cast<bool*>(data); }
  void set_bool(bool v, char* data) override { *reinterpret_cast<bool*>(data) = v; }
  LTM_COPYABLE(BoolType)
};

template<typename PTR, TypeInfo::Type ID>
class PtrType : public PrimitiveType<PTR, ID>
{
public:
  pin<DomItem> get_ptr(char* data) override { return *reinterpret_cast<PTR*>(data); }
  void set_ptr(const pin<DomItem>& v, char* data) override { *reinterpret_cast<PTR*>(data) = v; }
  LTM_COPYABLE(PtrType)
};

class AtomType : public PrimitiveType<own<Name>, TypeInfo::ATOM>
{
public:
  pin<Name> get_atom(char* data) override { return *reinterpret_cast<own<Name>*>(data); }
  void set_atom(pin<Name> v, char* data) override { *reinterpret_cast<own<Name>*>(data) = v; }
  LTM_COPYABLE(AtomType)
};

class VarArrayType : public TypeInfo
{
  struct Data{
    size_t count;
    char* items;
  };
public:
  VarArrayType(pin<TypeInfo> element_type)
    : element_type(element_type)
    , element_size(element_type->get_size()) {}

  size_t get_size() override { return sizeof(Data); }

  Type get_type() override { return TypeInfo::VAR_ARRAY; }

  pin<TypeInfo> get_element_type() override { return element_type; }
  size_t get_elements_count(char* data) override { return reinterpret_cast<Data*>(data)->count; }

  void init(char* data) override {
    Data* d = reinterpret_cast<Data*>(data);
    d->count = 0;
    d->items = nullptr;
  }

  void dispose(char* data) override {
    Data* d = reinterpret_cast<Data*>(data);
    char* item = d->items;
    for (size_t i = d->count + 1; --i; item += element_size) {
      element_type->dispose(item);
    }
    delete[] d->items;
  };

  void move(char* src, char* dst) override {
    Data* d = reinterpret_cast<Data*>(dst);
    Data* s = reinterpret_cast<Data*>(src);
    Data t = *s;
    init(src);
    dispose(dst);
    *d = t;
  };

  void copy(char* src, char* dst) override {
    Data* d = reinterpret_cast<Data*>(dst);
    Data* s = reinterpret_cast<Data*>(src);
    Data t{s->count, new char[element_size * s->count]};
    char* si = s->items;
    char* di = t.items;
    for (size_t i = s->count + 1; --i; si += element_size, di += element_size) {
      element_type->copy(si, di);
    }
    dispose(dst);
    *d = t;    
  };

  char* get_element_ptr(size_t index, char* data) override {
    return index < reinterpret_cast<Data*>(data)->count
      ? reinterpret_cast<Data*>(data)->items + index * element_size
      : nullptr;
  }

  void set_elements_count(size_t count, char* data) override {
    Data* v = reinterpret_cast<Data*>(data);
    if (v->count == count)
      return;
    char* dst = new char[count * element_size];
    char* si = v->items;
    char* di = dst;
    for (size_t i = min(v->count, count) + 1; --i; si += element_size, di += element_size) {
      element_type->move(si, di);
      element_type->dispose(si);
    }
    if (v->count < count) {
      for (size_t i = count - v->count + 1; --i; di += element_size) {
        element_type->init(di);
      }
    } else {
      for (size_t i = v->count - count + 1; --i; si += element_size) {
        element_type->dispose(si);
      }
    }
    delete[] v->items;
    v->items = dst;
    v->count = count;
  }

protected:
  own<TypeInfo> element_type;
  size_t element_size;
  LTM_COPYABLE(VarArrayType)
};

class FixArrayType : public TypeInfo
{
public:
  FixArrayType(pin<TypeInfo> element_type, size_t elements_count)
    : element_type(element_type)
    , elements_count(elements_count)
    , element_size(element_type->get_size()) {}

  size_t get_size() override { return element_size * elements_count; }

  Type get_type() override { return TypeInfo::FIX_ARRAY; }

  pin<TypeInfo> get_element_type() override { return element_type; }

  char* get_element_ptr(size_t index, char* data) override {
    return index < elements_count ? data + element_size * index : nullptr;
  }

  size_t get_elements_count(char*) override { return elements_count; }

  void init(char* data) override {
    for (size_t i = elements_count + 1; --i; data += element_size) {
      element_type->init(data);
    }
  }

  void dispose(char* data) override {
    for (size_t i = elements_count + 1; --i; data += element_size) {
      element_type->dispose(data);
    }    
  };

  void move(char* src, char* dst) override {
    for (size_t i = elements_count + 1; --i; src += element_size, dst += element_size) {
      element_type->move(src, dst);
    }
  };

  void copy(char* src, char* dst) override {
    for (size_t i = elements_count + 1; --i; src += element_size, dst += element_size) {
      element_type->copy(src, dst);
    }
  };

protected:
  own<TypeInfo> element_type;
  size_t elements_count;
  size_t element_size;
  LTM_COPYABLE(FixArrayType)
};

class DomItemImpl : public DomItem
{
  friend class StructType;

public:
  pin<TypeInfo> get_type() override { return type; }
  char* get_data() override { return data; }

protected:
  DomItemImpl(pin<TypeInfo> type) :type(move(type)) {}
  void copy_to(Object*& d) override {
    d = alloc(type);
    type->copy(data, static_cast<DomItemImpl*>(d)->data);
  }

  static DomItemImpl* alloc(const pin<TypeInfo>& type) {
    return new (new char[sizeof(DomItemImpl) + type->get_size()]) DomItemImpl(type);
  }

  void internal_dispose() noexcept override {
    type->dispose(data);
    this->DomItem::~DomItem();
    delete[] reinterpret_cast<char*>(this);
  }

  const own<TypeInfo> type;
  char data[1];
};

class StructType : public TypeInfo
{
public:
  Type get_type() override { return STRUCT; }
  size_t get_size() override { return instance_size; }

  StructType(pin<Name> name, vector<pin<FieldInfo>> init_fields)
    : name(name)
  {
    instance_size = 0;
    for (auto& f : init_fields){
      fields.insert({f->name, f});
      f->offset = instance_size;
      instance_size += f->type->get_size();
    }
  }

  void init(char* data) override {
    for (auto& f : fields){
      f.second->type->init(f.second->get_data(data));
    }
  }

  void dispose(char* data) override {
    for (auto& f : fields){
      f.second->type->dispose(f.second->get_data(data));
    }
  }

  void move(char* src, char* dst) override {
    for (auto& f : fields) {
      f.second->type->move(f.second->get_data(src), f.second->get_data(dst));
    }
  };

  void copy(char* src, char* dst) override {
    for (auto& f : fields) {
      f.second->type->copy(f.second->get_data(src), f.second->get_data(dst));
    }
  };

  pin<Name> get_name() override { return name; }

  void for_fields(function<void(pin<FieldInfo>)> action) override {
    for (const auto& f : fields)
      action(f.second);
  }

  size_t get_fields_count() override { return fields.size(); }

  pin<FieldInfo> get_field(pin<Name> name) override {
    auto it = fields.find(name);
    return it == fields.end() ? FieldInfo::empty : it->second;
  }

  pin<DomItem> create_instance() override {
    DomItemImpl* r = DomItemImpl::alloc(this);
    init(r->get_data());
    return r;
  }

protected:
  own<Name> name;
  unordered_map<own<Name>, own<FieldInfo>> fields;
  size_t instance_size;
  LTM_COPYABLE(StructType)
};

Dom::Dom()
  : atom_type(new AtomType)
  , bool_type(new BoolType)
  , string_type(new StringType)
  , own_ptr_type(new PtrType<own<DomItem>, TypeInfo::OWN>)
  , weak_ptr_type(new PtrType<weak<DomItem>, TypeInfo::WEAK>)
  , int8_type(new IntType<int8_t>)
  , int16_type(new IntType<int16_t>)
  , int32_type(new IntType<int32_t>)
  , int64_type(new IntType<int64_t>)
  , uint8_type(new UIntType<uint8_t>)
  , uint16_type(new UIntType<uint16_t>)
  , uint32_type(new UIntType<uint32_t>)
  , uint64_type(new UIntType<uint64_t>)
  , float32_type(new FloatType<float>)
  , float64_type(new FloatType<double>)
  , sealed(false)
{}

pin<TypeInfo> Dom::get_type(TypeInfo::Type type, size_t size, pin<TypeInfo> item) {
  switch(type) {
  case TypeInfo::ATOM: return atom_type;
  case TypeInfo::BOOL: return bool_type;
  case TypeInfo::STRING: return string_type;
  case TypeInfo::WEAK: return weak_ptr_type;
  case TypeInfo::OWN: return own_ptr_type;
  case TypeInfo::FLOAT: return size <= 4 ? float32_type : float64_type;
  case TypeInfo::INT:
    return
      size == 1 ? int8_type :
      size == 2 ? int16_type :
      size <= 4 ? int32_type :
      int64_type;
  case TypeInfo::UINT:
    return
      size == 1 ? uint8_type :
      size == 2 ? uint16_type :
      size <= 4 ? uint32_type :
      uint64_type;
  case TypeInfo::VAR_ARRAY: {
      if (sealed) {
        auto it = var_arrays.find(item);
        return it == var_arrays.end() ? TypeInfo::empty : it->second;
      }
      auto& result = var_arrays[item];
      if (!result)
        result = new VarArrayType(item);
      return result;
    }
  case TypeInfo::FIX_ARRAY: {
      if (sealed) {
        auto it = fixed_arrays.find(item);
        if (it == fixed_arrays.end())
          return TypeInfo::empty;
        auto size_it = it->second.find(size);
        return size_it == it->second.end() ? TypeInfo::empty : size_it->second;
      }
      auto& result = fixed_arrays[item][size];
      if (!result) result = new FixArrayType(item, size);
      return result;
    }
  default:
    return nullptr;
  }
}

void Dom::set_name(pin<DomItem> item, pin<Name> name) {
  auto it = named_objects.find(name);
  if (it != named_objects.end())
    object_names.erase(it->second);
  object_names[item] = name;
  named_objects[name] = item;
}

pin<Name> Dom::get_name(pin<DomItem> p) {
  if (!p.has_weak())
    return nullptr;
  auto it = object_names.find(p);
  if (it == object_names.end())
    return nullptr;
  if (!it->first) {
    named_objects.erase(it->second);
    object_names.erase(it);
    return nullptr;
  }
  return it->second;
}

pin<DomItem> Dom::get_named(const pin<Name>& name) {
  auto it = named_objects.find(name);
  if (it == named_objects.end())
    return nullptr;
  if (!it->second) {
    object_names.erase(it->second);
    named_objects.erase(it);
    return nullptr;
  }
  return it->second;
}

pin<TypeInfo> Dom::get_struct_type(pin<Name> name, vector<pin<FieldInfo>>& fields) {
  if (sealed) {
    auto it = named_types.find(name);
    if (it == named_types.end()) {
      for (auto& f : fields)
        f = FieldInfo::empty;
      return TypeInfo::empty;
    } else {
      for (auto& field : fields)
        field = it->second->get_field(field->name);
      return it->second;
    }
  }
  auto& result = named_types[name];
  if (!result) {
    result = new StructType(name, fields);
  } else {
    for (auto& field : fields)
      field = result->get_field(field->name);
  }
  return result;
}

class EmptyTypeInfo : public TypeInfo
{
public:
  virtual Type get_type() { return EMPTY; }
  virtual size_t get_size() { return 0;}
  virtual void init(char*){}
  virtual void move(char* src, char* dst) {}
  virtual void copy(char* src, char* dst) {}
  virtual pin<TypeInfo> get_element_type(){ return this; }
  virtual pin<TypeInfo> get_ptr_type(char*){ return this; }
  virtual void report_error(string message) {}
  LTM_COPYABLE(EmptyTypeInfo);
};

const own<TypeInfo> TypeInfo::empty = new EmptyTypeInfo;
const own<FieldInfo> FieldInfo::empty = new FieldInfo(nullptr, TypeInfo::empty);

} // namespace std

#ifdef WITH_TESTS

#include <limits>
#include "testing/base/public/gunit.h"

namespace {

using ltm::pin;
using ltm::own;
using ltm::weak;
using ltm::Object;
using dom::PtrType;
using dom::DomItem;
using dom::Dom;
using dom::TypeInfo;
using dom::FieldInfo;
using dom::StructType;
using std::vector;

template<typename T>
void test_int() {
  auto dom = pin<Dom>::make();
  auto int_type = dom->get_type(TypeInfo::INT, sizeof(T));
  EXPECT_EQ(int_type->get_size(), sizeof(T));
  char data[sizeof(T)];
  int_type->init(data);
  EXPECT_EQ(int_type->get_int(data), 0);
  int_type->set_int(std::numeric_limits<T>::min(), data);
  EXPECT_EQ(int_type->get_int(data), std::numeric_limits<T>::min());
  int_type->set_int(std::numeric_limits<T>::max(), data);
  EXPECT_EQ(int_type->get_int(data), std::numeric_limits<T>::max());
  int_type->dispose(data);
}

TEST(Dom, Primitives) {
  test_int<int8_t>();
  test_int<int16_t>();
  test_int<int32_t>();
  test_int<int64_t>();
}

TEST(Dom, String) {
  auto dom = pin<Dom>::make();
  auto str_type = dom->get_type(TypeInfo::STRING);
  char* str_data = new char[str_type->get_size()];
  str_type->init(str_data);
  EXPECT_EQ(str_type->get_string(str_data), "");
  str_type->set_string("Hello", str_data);
  EXPECT_EQ(str_type->get_string(str_data), "Hello");
  str_type->dispose(str_data);
  delete[] str_data;
}

TEST(Dom, FixedArrays) {
  auto dom = pin<Dom>::make();
  auto str_type = dom->get_type(TypeInfo::STRING);
  auto array_type = dom->get_type(TypeInfo::FIX_ARRAY, 3, str_type);
  char* data = new char[array_type->get_size()];
  EXPECT_EQ(array_type->get_elements_count(data), 3);
  array_type->init(data);
  str_type->set_string("qwerty", array_type->get_element_ptr(0, data));
  str_type->copy(array_type->get_element_ptr(0, data), array_type->get_element_ptr(1, data));
  str_type->set_string("asdfg", array_type->get_element_ptr(2, data));
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "qwerty");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(1, data)), "qwerty");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(2, data)), "asdfg");
  array_type->dispose(data);
  delete[] data;
}

TEST(Dom, VarArrays) {
  auto dom = pin<Dom>::make();
  auto str_type = dom->get_type(TypeInfo::STRING);
  auto array_type = dom->get_type(TypeInfo::VAR_ARRAY, 0, str_type);
  char* data = new char[array_type->get_size()];
  array_type->init(data);
  EXPECT_EQ(array_type->get_elements_count(data), 0);
  array_type->set_elements_count(2, data);
  EXPECT_EQ(array_type->get_elements_count(data), 2);
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(1, data)), "");
  str_type->set_string("qwerty", array_type->get_element_ptr(0, data));
  str_type->copy(array_type->get_element_ptr(0, data), array_type->get_element_ptr(1, data));
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "qwerty");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(1, data)), "qwerty");
  array_type->set_elements_count(1, data);
  EXPECT_EQ(array_type->get_elements_count(data), 1);
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "qwerty");
  array_type->set_elements_count(3, data);
  EXPECT_EQ(array_type->get_elements_count(data), 3);
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "qwerty");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(1, data)), "");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(2, data)), "");
  str_type->set_string("asdfg", array_type->get_element_ptr(2, data));
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(0, data)), "qwerty");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(1, data)), "");
  EXPECT_EQ(str_type->get_string(array_type->get_element_ptr(2, data)), "asdfg");
  array_type->dispose(data);
  delete[] data;
}

TEST(Dom, ValueStructs) {
  auto dom = pin<dom::Dom>::make();
  std::vector<pin<dom::FieldInfo>> fields{
      pin<dom::FieldInfo>::make(dom->names()->get_or_create("name"), dom->get_type(TypeInfo::STRING)),
      pin<dom::FieldInfo>::make(dom->names()->get_or_create("age"), dom->get_type(TypeInfo::FLOAT, 8))};
  auto struct_type = dom->get_struct_type(
      dom->names()->get_or_create("andreyka")->get_or_create("test")->get_or_create("Person"),
      fields);
  auto name_field = fields[0];
  auto age_field = fields[1];
  auto data = new char[struct_type->get_size()];
  struct_type->init(data);
  EXPECT_EQ(name_field->type->get_string(name_field->get_data(data)), "");
  EXPECT_EQ(age_field->type->get_float(age_field->get_data(data)), 0);
  name_field->type->set_string("Andreyka", name_field->get_data(data));
  age_field->type->set_float(47.5, age_field->get_data(data));
  EXPECT_EQ(name_field->type->get_string(name_field->get_data(data)), "Andreyka");
  EXPECT_EQ(age_field->type->get_float(age_field->get_data(data)), 47.5);
  struct_type->dispose(data);
  delete[] data;
}

TEST(Dom, References) {
  auto dom = pin<Dom>::make();
  vector<pin<FieldInfo>> fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("data"), dom->get_type(TypeInfo::INT, 8)),
    pin<FieldInfo>::make(dom->names()->get_or_create("next"), dom->get_type(TypeInfo::OWN)),
    pin<FieldInfo>::make(dom->names()->get_or_create("prev"), dom->get_type(TypeInfo::WEAK))};
  auto struct_type = dom->get_struct_type(
      dom->names()->get_or_create("andreyka")->get_or_create("test")->get_or_create("Item"),
      fields);
  auto data_field = fields[0];
  auto next_field = fields[1];
  auto prev_field = fields[2];
  own<DomItem> item = struct_type->create_instance();
  auto data = Dom::get_data(item);
  data_field->type->set_int(5, data_field->get_data(data));
  EXPECT_EQ(data_field->type->get_int(data_field->get_data(data)), 5);
  EXPECT_TRUE(next_field->type->get_ptr(next_field->get_data(data)) == nullptr);
  EXPECT_TRUE(prev_field->type->get_ptr(prev_field->get_data(data)) == nullptr);
  auto n1 = struct_type->create_instance();
  auto d1 = Dom::get_data(n1);
  prev_field->type->set_ptr(n1, prev_field->get_data(data));
  next_field->type->set_ptr(n1, next_field->get_data(data));
  prev_field->type->set_ptr(item, prev_field->get_data(d1));
  EXPECT_TRUE(prev_field->type->get_ptr(prev_field->get_data(data)) == n1);
  EXPECT_TRUE(next_field->type->get_ptr(next_field->get_data(data)) == n1);
  EXPECT_TRUE(prev_field->type->get_ptr(prev_field->get_data(d1)) == item);
  own<DomItem> copy = item;
  EXPECT_TRUE(copy != data);
  auto n2 = next_field->type->get_ptr(next_field->get_data(Dom::get_data(copy)));
  EXPECT_TRUE(n1 != n2);
  EXPECT_TRUE(prev_field->type->get_ptr(prev_field->get_data(Dom::get_data(copy))) == n2);
  EXPECT_TRUE(prev_field->type->get_ptr(prev_field->get_data(Dom::get_data(n2))) == copy);
}

TEST(Dom, DoubledStruct) {
  auto dom = pin<Dom>::make();
  vector<pin<FieldInfo>> fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("data"), dom->get_type(TypeInfo::INT, 8)),
    pin<FieldInfo>::make(dom->names()->get_or_create("name"), dom->get_type(TypeInfo::STRING)),
    pin<FieldInfo>::make(dom->names()->get_or_create("aaa"), dom->get_type(TypeInfo::UINT, 1))};
  auto struct_type = dom->get_struct_type(dom->names()->get_or_create("Item"), fields);
  vector<pin<FieldInfo>> fields2{
    pin<FieldInfo>::make(dom->names()->get_or_create("name"), dom->get_type(TypeInfo::STRING)),
    pin<FieldInfo>::make(dom->names()->get_or_create("bbb"), dom->get_type(TypeInfo::FLOAT, 4)),
    pin<FieldInfo>::make(dom->names()->get_or_create("data"), dom->get_type(TypeInfo::INT, 8))};
  auto struct_type2 = dom->get_struct_type(dom->names()->get_or_create("Item"), fields2);
  EXPECT_TRUE(struct_type == struct_type2);
  EXPECT_TRUE(fields2[0] == fields[1]);
  EXPECT_TRUE(fields2[1] == FieldInfo::empty);
  EXPECT_TRUE(fields2[2] == fields[0]);
}

TEST(Dom, Sealed) {
  auto dom = pin<Dom>::make();
  auto int_type = dom->get_type(TypeInfo::INT, 4);
  auto ubyte_type = dom->get_type(TypeInfo::UINT, 1);
  auto array_type = dom->get_type(TypeInfo::VAR_ARRAY, 0, int_type);
  vector<pin<FieldInfo>> fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("x"), int_type),
    pin<FieldInfo>::make(dom->names()->get_or_create("y"), int_type)};
  auto struct_type = dom->get_struct_type(dom->names()->get_or_create("Item"), fields);
  dom->sealed = true;
  auto array_type2 = dom->get_type(TypeInfo::VAR_ARRAY, 0, int_type);
  EXPECT_TRUE(array_type == array_type2);
  auto array_type3 = dom->get_type(TypeInfo::VAR_ARRAY, 0, ubyte_type);
  EXPECT_TRUE(array_type3 == TypeInfo::empty);
  auto array_type4 = dom->get_type(TypeInfo::FIX_ARRAY, 3, int_type);
  EXPECT_TRUE(array_type4 == TypeInfo::empty);
  vector<pin<FieldInfo>> fields2{
    pin<FieldInfo>::make(dom->names()->get_or_create("x"), int_type),
    pin<FieldInfo>::make(dom->names()->get_or_create("y"), int_type)};
  auto struct_type2 = dom->get_struct_type(dom->names()->get_or_create("Another"), fields2);
  EXPECT_TRUE(struct_type2 == TypeInfo::empty);
  EXPECT_TRUE(fields2[0] == FieldInfo::empty);
  EXPECT_TRUE(fields2[1] == FieldInfo::empty);
}

}  // namespace


#endif //WITH_TESTS
