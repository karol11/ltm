/*
Copyright 2018 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include <cassert>
#include <functional>
#include <iterator>
using std::bind;
using std::function;
using std::placeholders::_1;

#include <vector>
using std::vector;

#include <string>
using std::string;

#include "ltm.h"
using ltm::Object;
using ltm::own;
using ltm::pin;
using ltm::weak;
using lc = ltm::Object;
using ltm::mc;
using ltm::Proxy;

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

int main() {
  ltm_tests::main();
  return 0;
}
