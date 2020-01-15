#include <unordered_map>
#include <unordered_set>
#include "dom.h"
#include "../../../cml/c/src/cml_stax_writer.h"

namespace tcml {
using std::ostream;
using std::endl;
using ltm::pin;
using dom::Name;
using dom::Dom;
using dom::TypeInfo;
using dom::DomItem;
using std::string;
using std::unordered_map;
using std::unordered_set;

class TextWriter
{
public:  
  TextWriter(ostream& stream, const pin<Dom>& dom) :stream(stream), dom(dom) {
    writer = cmlw_create(
        [](void* ctx, char c) { reinterpret_cast<ostream*>(ctx)->put(c); return 1; },
        &stream);
  }

  void write(const pin<DomItem>& root) {
    write_ptr(nullptr, root);
  }
private:
  const char* cml_name(const pin<Name>& name) {
    // todo escape
    auto it = cml_name_cache.find(name);
    if (it == cml_name_cache.end()) {
      cml_name_cache.insert(
          {name, name->domain == dom->names()
                     ? name->name
                     : string(cml_name(name->domain)) + "-" + name->name});
      it = cml_name_cache.find(name);
    }
    return it->second.c_str();
  }

  void write_value(const char* field, char* data, const pin<TypeInfo>& type) {
    switch(type->get_type()) {
    case TypeInfo::BOOL: cmlw_bool(writer, field, type->get_bool(data)); break;
    case TypeInfo::INT: cmlw_int(writer, field, type->get_int(data)); break;
    case TypeInfo::UINT: cmlw_int(writer, field, type->get_uint(data)); break;
    case TypeInfo::FLOAT: cmlw_double(writer, field, type->get_float(data)); break;
    case TypeInfo::STRING: cmlw_str(writer, field, type->get_string(data).c_str()); break;
    case TypeInfo::VAR_ARRAY:
    case TypeInfo::FIX_ARRAY: {
      auto state = cmlw_array(writer, field, int(type->get_elements_count(data)));
      for (size_t i = 0, n = type->get_elements_count(data); i < n; ++i)
        write_value(nullptr, type->get_element_ptr(i, data),
                    type->get_element_type());
      cmlw_end_array(writer, state);
      break; }
    case TypeInfo::WEAK:
    case TypeInfo::OWN:
      write_ptr(field, type->get_ptr(data));
      break;
    case TypeInfo::ATOM:
      cmlw_end_struct(writer, cmlw_struct(writer, field, cml_name(type->get_atom(data)), nullptr));
      break;
    case TypeInfo::STRUCT: write_struct(field, data, type, nullptr); break;
    }
  }

  void write_struct(const char* field, char* data, const pin<TypeInfo>& type, const char* id) {
    auto state = cmlw_struct(writer, field, cml_name(type->get_name()), nullptr);
    type->for_fields([&](pin<dom::FieldInfo> field){
      string suffix;
      for (auto type = field->type;; type = type->get_element_type()) {
        switch (type->get_type()) {
          case TypeInfo::VAR_ARRAY:
            suffix += "[]";
            continue;
          case TypeInfo::FIX_ARRAY:
            suffix += "[" + std::to_string(type->get_elements_count(nullptr)) + "]";
            continue;
          case TypeInfo::WEAK:
            suffix += "*";
            break;
          case TypeInfo::STRUCT:
            suffix += "-";
            break;
          case TypeInfo::ATOM:
            suffix += "@";
            break;
          case TypeInfo::UINT:
            suffix += "#";
            break;
        }
        break;
      }
      write_value(suffix.empty()
                      ? cml_name(field->name)
                      : (string(cml_name(field->name)) + suffix).c_str(),
                  field->get_data(data), field->type);
    });
    cmlw_end_struct(writer, state);
  }

  template<typename Consumer>
  void with_object_name(const pin<DomItem>& n, Consumer consumer) {
    auto name = dom->get_name(n);
    if (name) {
      consumer(cml_name(name));
      return;
    }
    char buffer[16 + 2];
    sprintf(buffer, "_%.8llx", reinterpret_cast<uint64_t>(n.get()));
    consumer(buffer);
  }

  void write_ptr(const char* field, const pin<DomItem>& n) {
    if (!n) {
      cmlw_ref(writer, field, "_");
      return;
    }
    with_object_name(n, [&](const char* name){
      if (items.find(n) == items.end()) {
        items.insert(n);
        write_struct(field, Dom::get_data(n), Dom::get_type(n), name);
      } else {
        cmlw_ref(writer, field, name);      
      }
    });
  }

  cml_stax_writer* writer = nullptr;
  ostream& stream;
  unordered_map<pin<Name>, string> cml_name_cache;
  pin<Dom> dom;
  unordered_set<pin<DomItem>> items;
};

} // namespace tcml

#ifdef WITH_TESTS

#include <sstream>
#include "testing/base/public/gunit.h"

TEST(CmlWriter, Basic) {
  using ltm::pin;
  using dom::Dom;
  using dom::FieldInfo;
  using dom::DomItem;
  using std::vector;
  auto dom = ltm::own<dom::Dom>::make();
  auto package = dom->names()->get_or_create("andreyka")->get_or_create("test");
  auto int_type = dom->get_type(dom::TypeInfo::INT, 8);
  vector<pin<FieldInfo>> point_fields{
      pin<FieldInfo>::make(dom->names()->get_or_create("x"), int_type),
      pin<FieldInfo>::make(dom->names()->get_or_create("y"), int_type)};
  auto point_type = dom->get_struct_type(package->get_or_create("Point"), point_fields);
  auto x_field = point_fields[0];
  auto y_field = point_fields[1];
  vector<pin<FieldInfo>> polygon_fields{
    pin<FieldInfo>::make(dom->names()->get_or_create("name"), dom->get_type(dom::TypeInfo::STRING)),
    pin<FieldInfo>::make(dom->names()->get_or_create("points"), dom->get_type(dom::TypeInfo::VAR_ARRAY, 0, point_type))};
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
  tcml::TextWriter(stream, dom).write(root);
  EXPECT_EQ(stream.str(),
    "andreyka-test-Polygon\n"
    "name \"Test\"\n"
    "points[]- :2\n"
    "\tandreyka-test-Point\n"
    "\tx 0\n"
    "\ty 1\n"
    "\n"
    "\tandreyka-test-Point\n"
    "\tx 2\n"
    "\ty 3\n");
}

#endif  // WITH_TESTS
