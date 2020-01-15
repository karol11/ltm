#include <unordered_map>
#include <iostream>
#include <memory>
#include "dom.h"
#include <functional>
#include "cml/utf8.h"

namespace bcml {

using std::ostream;
using std::unique_ptr;
using ltm::pin;
using ltm::own;
using ltm::weak;
using ltm::Object;
using dom::Name;
using dom::Dom;
using dom::TypeInfo;
using dom::FieldInfo;
using dom::DomItem;
using std::vector;
using std::string;
using std::function;
using std::pair;
using std::move;
using std::unordered_map;

class BinaryWriter
{
public:
  BinaryWriter (ostream& file, pin<Dom> dom)
    : file(file), dom(dom) {
    objects.insert({nullptr, 0});
  }

  void write(pin<DomItem> root) {
    write_ptr(root, Dom::get_type(root), true);
  }

private:
  void write_byte(char v) {
    file.put(v);
  }
  void write_u7(uint64_t v) {
    for (; v > 0x7f; v >>= 7)
      write_byte(char(v | 0x80));
    write_byte(char(v));
  }

  void write_s7(int64_t v) {
    write_u7((v >> 63) ^ (v << 1));
  }

  void write_16(uint64_t v) {
    write_byte(char(v));
    write_byte(char(v >> 8));
  }

  void write_32(uint64_t v) {
    write_16(v);
    write_16(v >> 16);
  }

  void write_64(uint64_t v) {
    write_32(v);
    write_32(v >> 32);
  }

  static int utf8_helper(void* data) {
    return *(*reinterpret_cast<const char**>(data))++;
  }

  size_t str_len(const char* data) {
    for (size_t r = 0;; ++r) {
      if (get_utf8(utf8_helper, &data) <= 0)
        return r;
    }
  }

  void write_chars(const char* data) {
    for (char c; (c = get_utf8(utf8_helper, &data)) != 0;)
      write_u7(c);
  }

  void write_name(const pin<Name>& n) {
    // L0 -  seen[L]
    // Lr1 - new (in root domain): domain string(L)
    auto it = names.find(n);
    if (it != names.end()) {
      write_u7(it->second << 1);
      return;
    }
    if (n->domain == dom->names()){ 
      write_u7(str_len(n->name.c_str()) << 2 | 0b01);
    } else {
      write_u7(str_len(n->name.c_str()) << 2 | 0b11);
      write_name(n->domain);
    }
    write_chars(n->name.c_str());
    names.insert({n, names.size()});
  }

  void write_data(pin<TypeInfo> type, char* data) {
    switch(type->get_type()) {
    case TypeInfo::BOOL: write_byte(type->get_bool(data) ? 1 : 0); break;
    case TypeInfo::INT: write_s7(type->get_int(data)); break;
    case TypeInfo::UINT: write_u7(type->get_uint(data)); break;
    case TypeInfo::FLOAT: break; // TODO
    case TypeInfo::FIX_ARRAY:
      for (size_t i = 0, n = type->get_elements_count(data); i < n; i++)
        write_data(type->get_element_type(), type->get_element_ptr(i, data));
      break;
    case TypeInfo::VAR_ARRAY:
      write_u7(type->get_elements_count(data));
      for (size_t i = 0, n = type->get_elements_count(data); i < n; i++)
        write_data(type->get_element_type(), type->get_element_ptr(i, data));
      break;
    case TypeInfo::STRUCT:
      type->for_fields([&](pin<FieldInfo> field){
        write_data(field->type, field->get_data(data));
      });
      break;
    case TypeInfo::STRING: {
      auto s = type->get_string(data);
      write_u7(str_len(s.c_str()));
      write_chars(s.c_str());
      break; }
    case TypeInfo::WEAK:
      write_ptr(type->get_ptr(data), Dom::get_type(type->get_ptr(data)), false);
      break;
    case TypeInfo::ATOM:
      write_name(type->get_atom(data));
      break;
    case TypeInfo::OWN:
      write_ptr(type->get_ptr(data), Dom::get_type(type->get_ptr(data)), true);
      break;
    }
  }

  void write_ptr(pin<DomItem> data, pin<TypeInfo> type, bool has_r_bit) {
    // Lr1 L<refTypes ? instanceOfKnownrefType: data
    //     else L==refTypes ? named import: name
    //     else error
    // Lr10 newStructType+instance: structType(L=fieldsCount), data
    // L00 L<objects ? object[L]
    //      else L==objects ? namedExport: name refDef
    //      else error
    auto it = objects.find(data);
    if (it != objects.end()) {
      write_u7(it->second << 2 | 0b00);
      return;
    }
    if (auto name = dom->get_name(data)) {
      write_u7(objects.size() << 2 | 0b00);
      write_name(name);
      has_r_bit = false;
    }
    auto tt = ref_types.find(type);
    if (tt != ref_types.end())
      write_u7(has_r_bit ? tt->second << 1 | 0b11 : tt->second << 1 | 1);
    else {
      write_u7(has_r_bit ? type->get_fields_count() << 3 | 0b110 : type->get_fields_count() << 2 | 0b10);
      write_struct(type);
      ref_types.insert({type, ref_types.size()});
    }
    write_data(type, Dom::get_data(data));
  }

  void write_struct(const pin<TypeInfo>& type) {
    write_name(type->get_name());
    type->for_fields([&](pin<FieldInfo> field) {
      write_name(field->name);
      write_type(field->type);
    });
  }

  enum TypeCode{
    tcI7, tcU7, tcI8, tcU8, tcI16, tcU16, tcI32, tcU32, tcI64, tcU64, tcF32, tcF64,
    tcBoolean, tcString, tcOwn, tcWeak, tcAtom, tcVarArray, tcLast
  };

  void write_type(const pin<TypeInfo>& type) {
    // L1 - existing array/struct type L
    auto it = val_types.find(type);
    if (it != val_types.end()) {
      write_u7(it->second << 1 | 1);
      return;
    }
    switch (type->get_type()) {
    case TypeInfo::BOOL: write_byte(tcBoolean << 1); return;
    case TypeInfo::INT: write_byte(tcI7 << 1); return;
    case TypeInfo::UINT: write_byte(tcU7 << 1); return;
    case TypeInfo::FLOAT: write_byte(tcF64 << 1); return;
    case TypeInfo::FIX_ARRAY:
      write_u7((tcLast + (type->get_elements_count(nullptr) << 1 | 1)) << 1);
      write_type(type->get_element_type());
      break;
    case TypeInfo::VAR_ARRAY:
      write_byte(tcVarArray << 1);
      write_type(type->get_element_type());
      break;
    case TypeInfo::OWN: write_byte(tcOwn << 1); return;
    case TypeInfo::WEAK: write_byte(tcWeak << 1); return;
    case TypeInfo::ATOM: write_byte(tcAtom << 1); return;
    case TypeInfo::STRING: write_byte(tcString << 1); return;
    case TypeInfo::STRUCT:
      write_u7((tcLast + (type->get_fields_count() << 1)) << 1);
      write_struct(type);
      break;
    default: error("unsupported kind"); return;
    }
    val_types.insert({type, val_types.size()});
  }

  void error(const char* message) {
    throw message;
  }

  ostream& file;
  pin<Dom> dom;
  unordered_map<pin<Name>, size_t> names;
  unordered_map<pin<Object>, size_t> objects;
  unordered_map<pin<TypeInfo>, size_t> ref_types;
  unordered_map<pin<TypeInfo>, size_t> val_types;
};

void write(ltm::pin<dom::Dom> dom, ltm::pin<dom::DomItem> root, std::ostream& file) {
  bcml::BinaryWriter(file, dom).write(root);
}

} // namespace bcml

#ifdef WITH_TESTS

#include <sstream>
#include "testing/base/public/gunit.h"

namespace {

using std::string;

void expect_bin(std::stringstream& stream, std::string& expected) {
  for (char c : expected) {
    EXPECT_EQ(stream.get(), c);
  }
}

template<size_t N>
void expect_stream(std::stringstream& stream, const char(&data)[N]) {
  expect_bin(stream, string(data, N - 1));
}

TEST(BcmlWriter, Basic) {
  using ltm::pin;
  using dom::Dom;
  using dom::FieldInfo;
  using dom::DomItem;
  using dom::TypeInfo;
  using std::vector;
  auto dom = ltm::own<dom::Dom>::make();
  auto package = dom->names()->get_or_create("andreyka")->get_or_create("test");
  auto int_type = dom->get_type(TypeInfo::INT, 8);
  vector<pin<FieldInfo>> point_fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("x"), int_type),
    pin<FieldInfo>::make(dom->names()->get_or_create("y"), int_type)};
  auto point_type = dom->get_struct_type(package->get_or_create("Point"), point_fields);
  auto x_field = point_fields[0];
  auto y_field = point_fields[1];
  vector<pin<FieldInfo>> polygon_fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("name"), dom->get_type(TypeInfo::STRING)),
    pin<FieldInfo>::make(dom->names()->get_or_create("points"), dom->get_type(TypeInfo::VAR_ARRAY, 0, point_type))};
  auto polygon_type = dom->get_struct_type(package->get_or_create("Polygon"), polygon_fields);
  auto name_field = polygon_fields[0];
  auto points_field = polygon_fields[1];
  auto root = polygon_type->create_instance();
  auto root_data = Dom::get_data(root);
  name_field->type->set_string("Test", name_field->get_data(root_data));
  points_field->type->set_elements_count(2, points_field->get_data(root_data));
  for (int i = 0; i < 2; i++) {
    auto point = points_field->type->get_element_ptr(i, points_field->get_data(root_data));
    x_field->type->set_int(i * 2, x_field->get_data(point));
    y_field->type->set_int(i * 2 + 1, y_field->get_data(point));
  }
  std::stringstream stream;
  bcml::write(dom, root, stream);
  expect_stream(stream,
                "\x16"  // new component with 2 fields, register instance
                "\x1f"  // new name, 7 characters
                "\x13"  // new name, 4 characters
                "\x21"  // new name, root, 12 characters
                "andreyka"
                "test"
                "Polygon");
  bool x_first = true;
  auto expect_points_field = [&] {
    expect_stream(stream,
                  "\x19"  // new name, root, 6 characters
                  "points"
                  "\x22"  // var array
                  "\x2c"  // struct of 2 fields
                  "\x17"  // new name, reg, 5 characters
                  "\x02"  // old name "andreyka.test"
                  "Point"
                  "\x05");  // new name, root, 1 character
    if (stream.get() == 'x') {
      expect_stream(stream,
                    "\x00"  // int
                    "\x05"  // new name, root, 1 character
                    "y"
                    "\x00");  // int
    } else {
      x_first = false;
      expect_stream(stream,
                    "y"
                    "\x00"  // int
                    "\x05"  // new name, root, 1 character
                    "x"
                    "\x00");  // int
    }
  };
  auto expect_name_field = [&] {
    expect_stream(stream,
                  "\x11"  // new name, 4 characters
                  "name"
                  "\x1a");  // string
  };
  auto expect_name_data = [&] {
    expect_stream(stream,
                "\x04"  // strlen 4 characters
                "Test");
  };
  auto expect_points_data = [&] {
    expect_stream(stream, "\x02");  // array size,
    EXPECT_EQ(stream.get(), x_first ? 0 : 2);
    EXPECT_EQ(stream.get(), x_first ? 2 : 0);
    EXPECT_EQ(stream.get(), x_first ? 4 : 6);
    EXPECT_EQ(stream.get(), x_first ? 6 : 4);
    // "\x00"  // [0].x, signed 0
    // "\x02"  // [0].y, signed 1
    // "\x04"  // [1].x, signed 2
    // "\x06"  // [1].y, signed 3
  };
  if (stream.peek() == 0x11) { // new name, 4 characters
    expect_name_field();
    expect_points_field();
    expect_name_data();
    expect_points_data();
  } else {
    expect_points_field();
    expect_name_field();
    expect_points_data();
    expect_name_data();
  }                
}

}  // namespace

#endif  // WITH_TESTS
