#include "bcml_reader.h"

#include <memory>
#include "cml/utf8.h"

namespace bcml {

using std::istream;
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

class BinaryReader
{
public:
  BinaryReader(istream& file, pin<Dom> dom)
    : file(file)
    , dom(dom)
  {
    objects.push_back(nullptr);
  }

  pin<DomItem> read() { return read_ptr(true); }
private:
  struct IReader : public Object {
    IReader() { make_shared(); }
    virtual void read(char* dst, TypeInfo& accessor, BinaryReader& reader) =0;
  };
  
  template<typename action_t>
  struct Reader : public IReader {
    Reader(action_t action) : action(move(action)) {}
    void read(char* dst, TypeInfo& accessor, BinaryReader& reader) override { action(dst, accessor, reader); }
    action_t action;
    LTM_COPYABLE(Reader<action_t>)
  };

  template<typename action_t>
  pin<IReader> make_reader(action_t action) {
    return pin<IReader>::make<Reader<action_t>>(move(action));
  }

  uint64_t get_byte() { return file.get() & 0xff; }

  inline uint64_t read_u7() {
    auto r = get_byte();
    return (r & 0x80) == 0 ? r : read_u7_(r & 0x7f);
  }

  uint64_t read_u7_(uint64_t r) {
    for (unsigned int i = 7;; i += 7) {
      auto c = get_byte();
      r |= (c & 0x7f) << i;
      if ((c & 0x80) == 0)
        return r;
    }
  }

  static int64_t to_7signed(uint64_t v) {
    return (v >> 1) ^ (0 - (v & 1));
  }

  uint64_t get_16() {
    auto r = get_byte();
    return r | get_byte() << 8;
  }

  uint64_t get_32() {
    auto r = get_16();
    return r | get_16() << 16;
  }

  uint64_t get_64() {
    auto r = get_32();
    return r | get_32() << 32;
  }

  template<uint32_t width>
  static int64_t to_signed(int64_t src) {
    return src << (64 - width) >> (64 - width);
  }
  
  string read_chars(uint64_t count) {
    string r;
    for (count++; --count;) {
      put_utf8(static_cast<int>(read_u7()), [](void* ctx, char byte){
        reinterpret_cast<string*>(ctx)->append(1, byte);
        return 1;
      }, &r);
    }
    return r;
  }

  pin<Name> read_name() {
    // L0 -  seen[L]
    // Lr1 - new (r-is root, has no domain): domain string(L)
    uint64_t id = read_u7();
    if ((id & 1) == 0) {
      id >>= 1;
      if (id < names.size())
        return names[id];
      error("bad name index");
      return nullptr;
    }
    auto r = ((id & 2) == 0 ? dom->names() : read_name())->get_or_create(read_chars(id >> 2));
    names.push_back(r);
    return r;
  }

  virtual void error(const char* message) { std::cerr << message << " at " << file.tellg(); }

  pair<pin<TypeInfo>, pin<IReader>> read_type() {
    // L1 - existing array/struct type L
    // L0 - i7, u7, i8, u8, i16, u16, i32, u32, i64, u64, f32, f64, bool, string, own, weak, struct(fields=L-weak+1)
    auto code = read_u7();
    auto index = code >> 1;
    if (code & 1) {
      if (index >= value_types.size())
        error("bad struct/array index");
      return value_types[index];
    }
    struct Builtin {
      TypeInfo::Type type;
      size_t size;
      own<IReader> reader;
    };
    static vector<Builtin> builtin_types = {
        Builtin{TypeInfo::INT, 8, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_int(to_7signed(reader.read_u7()), dst); })},
        Builtin{TypeInfo::UINT, 8, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_uint(reader.read_u7(), dst); })},
        Builtin{TypeInfo::INT, 1, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_int(to_signed<8>(reader.get_byte()), dst); })},
        Builtin{TypeInfo::UINT, 1, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_uint(reader.get_byte(), dst); })},
        Builtin{TypeInfo::INT, 2, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_int(to_signed<16>(reader.get_16()), dst); })},
        Builtin{TypeInfo::UINT, 2, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_uint(reader.get_16(), dst); })},
        Builtin{TypeInfo::INT, 4, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_int(to_signed<32>(reader.get_32()), dst); })},
        Builtin{TypeInfo::UINT, 4, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_uint(reader.get_32(), dst); })},
        Builtin{TypeInfo::INT, 8, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_int(reader.get_64(), dst); })},
        Builtin{TypeInfo::UINT, 8, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_uint(reader.get_64(), dst); })},
        Builtin{TypeInfo::FLOAT, 4, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) {
              union {
                float f;
                uint32_t b;
              } v;
              v.b = uint32_t(reader.get_32());
              accessor.set_float(v.f, dst); })},
        Builtin{TypeInfo::FLOAT, 8, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) {
              union {
                double f;
                uint64_t b;
              } v;
              v.b = reader.get_64();
              accessor.set_float(v.f, dst); })},
        Builtin{TypeInfo::BOOL, 0, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_bool(reader.get_byte() != 0, dst); })},
        Builtin{TypeInfo::STRING, 0, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_string(reader.read_chars(reader.read_u7()), dst); })},
        Builtin{TypeInfo::OWN, 0, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_ptr(reader.read_ptr(1), dst); })},
        Builtin{TypeInfo::WEAK, 0, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_ptr(reader.read_ptr(0), dst); })},
        Builtin{TypeInfo::ATOM, 0, make_reader([](char* dst, TypeInfo& accessor, BinaryReader& reader) { accessor.set_atom(reader.read_name(), dst); })}};
    if (index < builtin_types.size()) {
      Builtin& b = builtin_types[index];
      return {dom->get_type(b.type, b.size), b.reader};
    }
    pair<pin<TypeInfo>, pin<IReader>> r;
    index -= builtin_types.size();
    if (index == 0) {
      auto array_item = read_type();
      r = {dom->get_type(TypeInfo::VAR_ARRAY, 0, array_item.first),
        make_reader([item_reader = array_item.second](char* dst, TypeInfo& accessor, BinaryReader& reader) {
          size_t size = reader.read_u7();
          accessor.set_elements_count(size, dst);
          for (size_t i = 0; i < size; i++) {
            item_reader->read(accessor.get_element_ptr(i, dst), *accessor.get_element_type(), reader);
          }
        })};
    } else if (--index & 1) {
      index >>= 1;
      auto array_item = read_type();
      r = {dom->get_type(TypeInfo::FIX_ARRAY, index, array_item.first),
        make_reader([item_reader = array_item.second, size = index](char* dst, TypeInfo& accessor, BinaryReader& reader) {
          for (size_t i = 0; i < size; i++) {
            item_reader->read(accessor.get_element_ptr(i, dst), *accessor.get_element_type(), reader);
          }
      })};
    } else {
      r = read_struct_type(index >> 1);
    }
    value_types.push_back(r);
    return r;
  }

  pair<pin<TypeInfo>, pin<IReader>> read_struct_type(size_t fields_count) {
    auto struct_name = read_name();
    vector<pin<FieldInfo>> fields;
    vector<pin<IReader>> field_readers;
    for (fields_count++; --fields_count;) {
      auto field_name = read_name();
      auto field_type = read_type();
      fields.emplace_back(new FieldInfo(field_name, field_type.first));
      field_readers.push_back(field_type.second);
    }
    pin<TypeInfo> struct_type = dom->get_struct_type(struct_name, fields);
    return {struct_type,
      make_reader([field_readers = move(field_readers), fields = move(fields)]
      (char* dst, TypeInfo& accessor, BinaryReader& reader) {
        for (size_t i = 0; i < fields.size(); ++i) {
          auto& field = fields[i];
          field_readers[i]->read(field->get_data(dst), *field->type, reader);
        }
      })};
  }
  pin<DomItem> link_to_names(pin<DomItem> target, bool reg_in_local_ids) {
    if (reg_in_local_ids)
      objects.push_back(target);
    if (!unassigned_names.empty()) {
      for (const auto& n : unassigned_names)
        dom->set_name(target, n);
      unassigned_names.clear();
    }
    return target;
  };

  pin<DomItem> read_ptr(bool with_r) {
    // Lr1 L<refTypes ? instanceOfKnownrefType: data
    //     else L==refTypes ? named import: name
    //     else error
    // Lr10 newStructType+instance: structType(L=fieldsCount), data
    // L00 L<objects ? object[L]
    //      else L==objects ? namedExport: name refDef
    //      else error
    uint64_t id = read_u7();
    auto extract_reg = [&]() {
      if (with_r) {
        bool r = (id & 1) != 0;
        id >>= 1;
        return r;
      }
      return false;
    };
    if (id & 1) {
      id >>= 1;
      bool reg_instance = extract_reg();
      if (id < ref_types.size())
        return ref_types[id](reg_instance);
      if (id == ref_types.size()) {
        return link_to_names(dom->get_named(read_name()), reg_instance);
      } else
        error("ref type id > types count");
    } else if (id & 2) {
      id >>= 2;
      bool reg_instance = extract_reg();
      auto inner_struct = read_struct_type(id);
      ref_types.push_back([this, inner_struct](bool do_register) {
        pin<DomItem> r = inner_struct.first->create_instance();
        link_to_names(r, do_register);
        inner_struct.second->read(Dom::get_data(r), *inner_struct.first, *this);
        return r;
      });
      return ref_types.back()(reg_instance);
    } else {
      id >>= 2;
      if (id < objects.size())
        return objects[id];
      if (id == objects.size()) {
        unassigned_names.push_back(read_name());
        return read_ptr(with_r);
      }
      error("object id > objects count");
    }
    return nullptr;
  }

  istream& file;
  pin<Dom> dom;
  vector<pin<DomItem>> objects;
  vector<pair<own<TypeInfo>, own<IReader>>> value_types;
  vector<function<pin<DomItem>(bool do_register)>> ref_types;
  vector<pin<Name>> names;
  vector<pin<Name>> unassigned_names;
};

pin<DomItem> read(pin<dom::Dom> dom, std::istream& file) {
  return bcml::BinaryReader(file, dom).read();
}

} // namespace bcml


#ifdef WITH_TESTS

#include <sstream>
#include "testing/base/public/gunit.h"

namespace {

using std::string;
using ltm::own;
using ltm::pin;
using dom::DomItem;

template<size_t N>
string from_literal_with_00(const char(&data)[N]) {
  return string(data, N - 1);
}

TEST(BcmlReader, Basic) {
  auto dom = own<dom::Dom>::make();
  auto root = bcml::read(
      dom, std::stringstream(from_literal_with_00(
               "\x16"  // new component with 2 fields, register instance
               "\x1f"  // new name, 7 characters
               "\x13"  // new name, 4 characters
               "\x21"  // new name, root, 12 characters
               "andreyka"
               "test"
               "Polygon"
               "\x11"  // new name, 4 characters
               "name"
               "\x1a"  // string-type
               "\x19"  // new name, root, 6 characters
               "points"
               "\x22"  // var array
               "\x2c"  // struct of 2 fields
               "\x17"  // new name, reg, 5 characters
               "\x02"  // old name "andreyka.test"
               "Point"
               "\x05"  // new name, root, 1 character
               "x"
               "\x00"  // int-type
               "\x05"  // new name, root, 1 character
               "y"
               "\x00"  // int-type
               "\x04"  // strlen 4 characters
               "Test"
               "\x02"  // array size
               "\x00"  // [0].x, signed 0
               "\x02"  // [0].y, signed 1
               "\x04"  // [1].x, signed 2
               "\x06"  // [1].y, signed 3
               )));
  EXPECT_TRUE(root != nullptr);
  auto root_type = dom::Dom::get_type(root);
  auto root_data = dom::Dom::get_data(root);
  EXPECT_TRUE(root_type->get_name() == dom->names()->get_or_create("andreyka")->get_or_create("test")->get_or_create("Polygon"));
  EXPECT_EQ(root_type->get_fields_count(), 2);
  auto name_field = root_type->get_field(dom->names()->get_or_create("name"));
  EXPECT_EQ(name_field->type->get_type(), dom::TypeInfo::STRING);
  EXPECT_EQ(name_field->type->get_string(name_field->get_data(root_data)), "Test");
  auto points_field = root_type->get_field(dom->names()->get_or_create("points"));
  EXPECT_EQ(points_field->type->get_type(), dom::TypeInfo::VAR_ARRAY);
  auto point_type = points_field->type->get_element_type();
  EXPECT_EQ(point_type->get_type(), dom::TypeInfo::STRUCT);
  EXPECT_EQ(points_field->type->get_elements_count(points_field->get_data(root_data)), 2);
  auto x_field = point_type->get_field(dom->names()->get_or_create("x"));
  auto y_field = point_type->get_field(dom->names()->get_or_create("y"));
  EXPECT_EQ(x_field->type->get_type(), dom::TypeInfo::INT);
  EXPECT_EQ(y_field->type->get_type(), dom::TypeInfo::INT);
  int v = 0;
  for (size_t i = 0; i < 2; i++, v += 2) {
    auto point = points_field->type->get_element_ptr(i, points_field->get_data(root_data));
    EXPECT_EQ(x_field->type->get_int(x_field->get_data(point)), v);
    EXPECT_EQ(y_field->type->get_int(y_field->get_data(point)), v + 1);
  }
}

}  // namespace

#endif
