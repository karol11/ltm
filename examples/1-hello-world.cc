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
#include <assert.h>

#include <vector>
using std::vector;

#include <string>
using std::string;

#include "ltm.h"
using ltm::Object;
using ltm::own;
using ltm::pin;
using ltm::weak;

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
  Group(int x, int y, std::initializer_list<pin<Visual>> items) : Visual(x, y) {
    mc(inner, items);
  }
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
  Style* s1 = 0;
  SmartConnector* sc = 0;
  TextBlock *t1 = 0, *t2 = 0;
  pin<Document> r = new Document(0, 0,
      {pin<Group>(new Group(10, 10, {
          pin<SmartConnector>::make(10).mark(sc),
          pin<TextBlock>::make(10, 0, "qwerty").mark(t1),
          pin<TextBlock>::make(10, 100, "zxcv").mark(t2)}))},
      {pin<Style>::make("Arial", 44, 12).mark(s1)});
  t2->style = s1;
  sc->start = t1;
  sc->end = t2;
  return r;
}

int main() {
  auto doc = create();

  // group1 copy
  doc->inner.push_back(doc->inner[0]);

  // check
  auto& copy = doc->inner.back().cast<Group>();
  assert(copy->inner[0].cast<SmartConnector>()->start == copy->inner[1]);

  return 0;
}
