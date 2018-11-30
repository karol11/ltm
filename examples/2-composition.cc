#include <vector>
using std::vector;

#include <string>
using std::string;

#include "ltm.h"
using ltm::Object;
using ltm::own;
using ltm::pin;
using ltm::weak;

//#define NEGATIVES
#define CXX14

namespace ltm_tests {
struct PageItem : public Object {
  void copy_to(Object*& dst) override { dst = new PageItem(*this); }
};

struct Point : public PageItem {
  int x, y;
  void copy_to(Object*& dst) override { dst = new Point(*this); }
};

bool type_test_fn(own<PageItem>& p) {
  return p;
}

void type_test() {
  own<PageItem> p = new Point();
  p = new Point();
  auto xx = p.cast<Point>()->x;
  own<Point> pt;
  type_test_fn(pt);
  type_test_fn(own<Point>());
#ifdef NEGATIVES
  p->x;
  own<Point> pt1 = new PageItem();
#endif
}

struct Node : Object {
  char c;
  own<Node> left, right;

  Node(char c, pin<Node> left = nullptr, pin<Node> right = nullptr)
      : c(c), left(left), right(right) {}

  void copy_to(Object*& d) override { d = new Node(*this); }
};

void construction_and_implicit_conversions() {
  own<Point> a = new Point;
  a.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);

  pin<Point> a_temp = a;
  a.check(2 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(a_temp == a);

  own<PageItem> b = a_temp;
  a.check(2 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(a_temp == a);
  assert(b != a);
  b.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);

  weak<PageItem> w = a;
  a.check(2 * lc::COUNTER_STEP + lc::OWNED,
          2 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(w.pinned() == a);
}

void auto_construction() {
  auto a = own<PageItem>::make<Point>();
  auto weak_to_a = a.weaked();
  auto pinned_a = a.pinned();
  auto x = a;
}

struct XrefNode : Object {
  char c;
  own<XrefNode> left, right;
  weak<XrefNode> xref;

  XrefNode(char c, XrefNode* left = nullptr, XrefNode* right = nullptr)
      : c(c), left(left), right(right) {
    if (left)
      left->xref = this;
    if (right)
      right->xref = this;
  }

  void copy_to(Object*& d) override { d = new XrefNode(*this); }
};

void check_xref_tree(const own<XrefNode>& root) {
  root.check(lc::COUNTER_STEP + lc::OWNED,
             3 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  root->left.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  root->right.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(root->left->xref.pinned() == root);
  assert(root->right->xref.pinned() == root);
}

void copy_ops() {
  own<XrefNode> root = new XrefNode('a', new XrefNode('b'), new XrefNode('c'));
  check_xref_tree(root);

  own<XrefNode> r2 = root;
  check_xref_tree(root);
  check_xref_tree(r2);

  r2->left->xref = r2->left;
  r2->right->xref = r2->left;

  r2.check(lc::COUNTER_STEP + lc::OWNED,
           1 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r2->left.check(lc::COUNTER_STEP + lc::OWNED,
                 3 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r2->right.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(r2->left->xref.pinned() == r2->left);
  assert(r2->right->xref.pinned() == r2->left);

  auto r3 = r2;
  r2.check(lc::COUNTER_STEP + lc::OWNED,
           1 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r2->left.check(lc::COUNTER_STEP + lc::OWNED,
                 3 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r2->right.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(r2->left->xref.pinned() == r2->left);
  assert(r2->right->xref.pinned() == r2->left);
  r3.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r3->left.check(lc::COUNTER_STEP + lc::OWNED,
                 3 * lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  r3->right.check(lc::COUNTER_STEP + lc::OWNED + lc::WEAKLESS);
  assert(r3->left->xref.pinned() == r3->left);
  assert(r3->right->xref.pinned() == r3->left);

  auto r4 = own<XrefNode>::make('a', new XrefNode('b'));
  root->xref = root->left;
  if (auto p = root->xref.pinned())
    p->c = 'x';
  auto r5 = r4;
  if (auto p = r5->xref.pinned())
    p->c = 'y';
}

struct NodeWithHandler : Object {
  own<NodeWithHandler> child;
  char c;
  function<void(int param)> handler = [](char) {};

  NodeWithHandler(char c, const pin<NodeWithHandler>& child = nullptr)
      : c(c), child(child) {}

  void copy_to(Object*& d) override { d = new NodeWithHandler(*this); }
};

void weak_handlers() {
  auto root = own<NodeWithHandler>::make('a', pin<NodeWithHandler>::make('b'));
#ifdef CXX14
  root->handler = [ctx = root->child.weaked()](char param) {
    if (auto me = ctx.pinned())
      me->c = param;
  };
#elif defined(USING_BIND)
  root->handler = std::bind(
      [](const weak<NodeWithHandler>& ctx, int param) {
        if (auto me = ctx.pinned())
          me->c = param;
      },
      root->child.weaked(), _1);
#else
  {
    auto ctx = root->child.weaked();
    root->handler = [ctx](int param) {
      if (auto me = ctx.pinned())
        me->c = param;
    };
  }
#endif
  root->handler('x');

  auto r2 = root;
  r2->handler('y');
}

struct IPaintable : Object {
  virtual void paint(Point* p) = 0;
  virtual int get_width() = 0;
};

void proxy_test() {
  struct PaintablePoint : Proxy<IPaintable> {
    weak<Point> me;
    PaintablePoint(pin<Point> me) : me(me) { make_shared(); }
    void paint(Point* p) override {
      // implement the IPaintable interface using `me`
    }
    int get_width() override {
      if (auto p = me.pinned())
        return p->x;
      return 0;
    }
  };

  auto p = own<Point>::make();
  weak<IPaintable> pp = new PaintablePoint(p);

  if (auto x = pp.pinned())
    x->get_width();
}

struct IInterface {
  virtual void method1() = 0;
  virtual int method2(int param) = 0;
};

class Impl : public Object, public IInterface {
  int field1;

 public:
  Impl(int i) : field1(i) {}
  void method1() override {}
  int method2(int param) override { return field1; }

  void copy_to(Object*& dst) override { dst = new Impl(*this); }
};

void interaface_test() {
  auto c = own<Impl>::make(42);
  weak<IInterface> w = c.weaked();
  if (auto i = w.pinned())
    i->method2(11);
}

void main() {
  copy_ops();
  weak_handlers();
  construction_and_implicit_conversions();
  auto_construction();
  interaface_test();
  proxy_test();
}
}  // namespace ltm_tests

namespace composition_sample {
using ltm::Object;
using ltm::own;

struct PageElement : Object {
  int x, y, width, height;
};
struct Page : Object {
  std::vector<own<PageElement>> elements;
  void copy_to(Object*& d) { d = new Page(*this); }
};
struct Document : Object {
  std::vector<own<Page>> pages;
  void copy_to(Object*& d) { d = new Document(*this); }
};
struct TextBlock : PageElement {
  std::string text;
  void copy_to(Object*& d) { d = new TextBlock(*this); }
};
struct Image : PageElement {
  std::string url;
  void copy_to(Object*& d) { d = new Image(*this); }
};

own<Document> doc;

void main() {
  doc = new Document;
  doc->pages.push_back(new Page);
  doc->pages[0]->elements.push_back(new TextBlock);
  doc->pages[0]->elements.push_back(new Image);

  // {A} make a copy of page 0
  doc->pages.push_back(doc->pages[0]);

  // {B} copy elements from page to page
  doc->pages[1]->elements.push_back(doc->pages[0]->elements[0]);

  Object::copy(doc->pages, 0, doc->pages.size(), doc->pages.size());

  // {C} delete page and all its content
  doc->pages.erase(doc->pages.begin());

  // {D} dispose the old document and create a new one
  doc = new Document;
}
}  // namespace composition_sample

namespace aggregation_sample {
struct Style : Object {
  string font;
  int color, size;
  Style(string font, int color, int size)
      : font(std::move(font)), color(color), size(size) {}
  LTM_COPYABLE(Style)
};

struct Visual : Object {
  int x, y;
  weak<Style> style;
  Visual(int x, int y) : x(x), y(y) {}
};

struct Group : Visual {
  vector<own<Visual>> inner;
  Group(int x, int y, std::initializer_list<pin<Visual>> items)
      : Visual(x, y) { mc(inner, items); }
  LTM_COPYABLE(Group)
};

struct TextBlock : Visual {
  string text;
  TextBlock(int x, int y, string text) : Visual(x, y), text(std::move(text)) {}
  LTM_COPYABLE(TextBlock)
};

struct SmartConnector : Visual {
  int color;
  weak<Visual> start, end;
  SmartConnector(int color) : Visual(0, 0), color(color) {}
  LTM_COPYABLE(SmartConnector)
};

struct Document : Group {
  vector<own<Style>> styles;
  Document(int x,
           int y,
           std::initializer_list<pin<Visual>> items,
           std::initializer_list<pin<Style>> sts)
      : Group(x, y, items) { mc(styles, sts); }
  LTM_COPYABLE(Document)
};

own<Document> create() {
  Style* s1=0;
  SmartConnector* sc=0;
  TextBlock *t1=0, *t2=0;
  pin<Document> r = new Document(0, 0, {
    pin<Group>(new Group(10, 10, {
        pin<SmartConnector>::make(10).mark(sc),
        pin<TextBlock>::make(10, 0, "qwerty").mark(t1),
        pin<TextBlock>::make(10, 100, "zxcv").mark(t2)}))},
    {pin<Style>::make("Arial", 44, 12).mark(s1)});
  t2->style = s1;
  sc->start = t1;
  sc->end = t2;
  return r;
}

void main() {
  auto doc = create();

  // group1 copy
  doc->inner.push_back(doc->inner[0]);

  // check
  auto& copy = doc->inner.back().cast<Group>();
  assert(copy->inner[0].cast<SmartConnector>()->start == copy->inner[1]);
}
}  // namespace aggregation_sample

int main() {
  ltm_tests::main();
  composition_sample::main();
  aggregation_sample::main();
  return 0;
}
