#include <unordered_map>
#include <vector>
#include <unordered_set>
#include "dom.h"
#include "../../../cml/c/src/cml_cpp.h"

namespace tcml {
using std::istream;
using std::vector;
using ltm::pin;
using dom::Name;
using dom::Dom;
using dom::TypeInfo;
using dom::FieldInfo;
using dom::DomItem;
using std::string;
using std::unordered_map;

template<typename T, typename K>
T* find(unordered_map<K, T>& m, const K& k) {
  auto it = m.find(k);
  return it == m.end() ? nullptr : &it->second;
}

class TextReader : public cml::err_handler
{
public:
  pin<DomItem> read(istream& stream, const pin<Dom>& dom) {
    this->dom = dom;
    cmd.cml::dom::~dom();
    new(&cmd) cml::dom(move(stream), this);
    vector<pin<FieldInfo>> fields;
    try_type_fields(cmd, nullptr, fields);
    return read_ptr(cmd);
  }
private:
  pin<DomItem> read_ptr(cml::var n) {
    if (auto item = find(ref_objects, *n))
      return *item;
    auto cml_type = (*n).type();
    pin<TypeInfo> dom_type;
    if (auto maybe_dom_type = find(ref_types, cml_type))
      dom_type = *maybe_dom_type;
    else {
      vector<pin<FieldInfo>> fields;
      for (auto cml_f : cml_type) {
        if (auto dom_f = find(this->fields, cml_f)) {
          if ((*dom_f)->type)
            fields.push_back(*dom_f);
          else
            error("untyped field");
        } else
          error("undefined field");
      }
      auto name_text = cml_type.name();
      dom_type = dom->get_struct_type(parse_name(name_text), fields);
      ref_types.insert({cml_type, dom_type});
    }
    auto r = dom_type->create_instance();
    ref_objects[*n] = r;
    for (const auto& fv : *n) {
      const auto& field = fields[fv.first];
      read_data(field->get_data(Dom::get_data(r)), field->type, fv.second);
    }
    return r;
  }

  void read_data(char* dst, pin<TypeInfo> type, cml::var src){
    switch (type->get_type()) {
      case TypeInfo::ATOM: {
          const char* name = (*src).type().name();
          type->set_atom(parse_name(name), dst);
          break; }
      case TypeInfo::BOOL: type->set_bool(src(false), dst); break;
      case TypeInfo::FLOAT: type->set_float(src(0.0), dst); break;
      case TypeInfo::INT:  type->set_int(src(0), dst); break;
      case TypeInfo::UINT: type->set_uint(src(0), dst); break;
      case TypeInfo::STRING:  type->set_string(src(""), dst); break;
      case TypeInfo::OWN:
      case TypeInfo::WEAK: type->set_ptr(read_ptr(src), dst); break;
      case TypeInfo::STRUCT:
        for (const auto& fv : *src) {
          const auto& field = fields[fv.first];
          read_data(field->get_data(dst), field->type, fv.second);
        }
        break;
      case TypeInfo::FIX_ARRAY:
        for (size_t i = 0; i < type->get_elements_count(dst); i++)
          read_data(type->get_element_ptr(i, dst), type->get_element_type(), src[int(i)]);
        break;
      case TypeInfo::VAR_ARRAY:
        type->set_elements_count(src.size(), dst);
        for (size_t i = 0, n = type->get_elements_count(dst); i < n; i++)
          read_data(type->get_element_ptr(i, dst), type->get_element_type(), src[int(i)]);
        break;
    }
  }

  void error(const char* message) {}

  pin<Name> parse_name(const char*& text) {
    pin<Name> n = dom->names();
    for (const char* t = text;; t++) {
      if (*t == 0 || *t == '[' || *t == '-' || *t == '*' || *t == '@') {
        n = n->get_or_create(string(text, t - text));
        text = t;
        if (*t != '-' || !t[1])
          return n;
        text++;
      }
    }
  }

  pin<TypeInfo> detect_field_type(const char* t, cml::var content) {
    for (;; t++) {
      if (*t == '[') {
        if (*++t == ']')
          return dom->get_type(TypeInfo::VAR_ARRAY, 0, detect_field_type(t + 1, content[0]));
        int n = 0;
        while (*t >= '0' && *t <= '9')
          n = n * 10 +*t++ - '0';
        if (*t != ']')
          error("expected ]");
        return dom->get_type(TypeInfo::FIX_ARRAY, n, detect_field_type(t+1, content[0]));
      } else if (*t == '-') {
        if (!*++t)
          error("symbols after -");
        vector<pin<FieldInfo>> fields;
        if ((*content).get_tag())
          error("struct type accessed from multiple nodes");
          pin<Name> struct_type_name;
        return try_type_fields(content, &struct_type_name, fields) ? dom->get_struct_type(struct_type_name, fields) : nullptr;
      } else if (*t == '@') {
        if (!*++t)
          error("symbols after @");
        if (content.kind() != CMLD_STRUCT || (*content).begin() != (*content).end())
          error("expected enum tag");
        return dom->get_type(TypeInfo::ATOM);
      } else if (*t == '#') {
        if (!*++t)
          error("symbols after #");
        if (content.kind() != CMLD_INT)
          error("expected number");
        return dom->get_type(TypeInfo::UINT, 8);
      } else if (*t == '*') {
        if (!*++t)
          error("symbols after *");
        vector<pin<FieldInfo>> fields;
        if ((*content).get_tag() == 0)
          try_type_fields(content, nullptr, fields);
        return dom->get_type(TypeInfo::WEAK);
      } else if (*t) {
        error("unexpected field tail");
      } else {
        switch (content.kind()) {
          case CMLD_INT: return dom->get_type(TypeInfo::INT, 8);
          case CMLD_BOOL: return dom->get_type(TypeInfo::BOOL);
          case CMLD_STR: return dom->get_type(TypeInfo::STRING);
          case CMLD_DOUBLE: return dom->get_type(TypeInfo::FLOAT, 8);
          case CMLD_ARRAY: return dom->get_type(TypeInfo::VAR_ARRAY, 0, detect_field_type(t, content[0]));
          case CMLD_STRUCT: {
            vector<pin<FieldInfo>> fields;
            if ((*content).get_tag() == 0)
              try_type_fields(content, nullptr, fields);
            return dom->get_type(TypeInfo::OWN); }
          case CMLD_BINARY: return dom->get_type(TypeInfo::VAR_ARRAY, 0, dom->get_type(TypeInfo::UINT, 1));
          default:
            error("unexpected type");
            break;
        }
      }
    }
  }

  bool try_type_fields(cml::var content, pin<Name>* name, vector<pin<FieldInfo>>& fields) {
    if (content.kind() != CMLD_STRUCT)
      error("expected struct field");
    (*content).set_tag(1);
    if (name) {
      const char* name_ptr = (*content).type().name();
      *name = parse_name(name_ptr);
    }
    bool success = true;
    for (const auto& f : *content) {
      auto cached = find(this->fields, f.first);
      if (cached && (*cached)->type) {
        fields.push_back(*cached);
      } else {
        const char* name_str = f.first.name();
        pin<Name> name = cached ? (*cached)->name : parse_name(name_str);
        fields.push_back(
            new FieldInfo{name, detect_field_type(name_str, f.second)});
        if (!fields.back()->type) {
          success = false;
        }
        if (!cached || fields.back()->type)
          this->fields[f.first] = fields.back();
      }
    }
    return success;
  }

  void on_error(const char *error, int line, int char_pos) override {
    std::cerr << error << " at " << line << std::endl;
  }

  cml::dom cmd;
  pin<Dom> dom;
  unordered_map<cml::field, pin<FieldInfo>> fields;
  unordered_map<cml::type, pin<TypeInfo>> ref_types;
  unordered_map<cml::struc, pin<DomItem>> ref_objects;
};

} // namespace tcml

#ifdef WITH_TESTS

#include "testing/base/public/gunit.h"
#include <sstream>

namespace
{

TEST(CmlReader, Basic) {
  auto dom = ltm::own<dom::Dom>::make();
  auto root = tcml::TextReader().read(std::stringstream(R"-(
    andreyka-test-Polygon
    name "Test"
    points[]-:2
        andreyka-test-Point
        x 0
        y 1

        andreyka-test-Point
        x 2
        y 3
  )-"), dom);
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

#endif // WITH_TESTS
