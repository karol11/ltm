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

#include "ltm.h"
using ltm::Object;
using ltm::own;
using ltm::weak;

struct XrefNode : Object {
  char c;
  own<XrefNode> left, right;
  weak<XrefNode> ref;

  XrefNode(char c, XrefNode* left = nullptr, XrefNode* right = nullptr)
      : c(c), left(left), right(right) {}

  LTM_COPYABLE(XrefNode)
};

int main() {
  own<XrefNode> root = new XrefNode(5,
    new XrefNode(1),
    new XrefNode(42)
  );
  root->ref = root->left;
  root->left->ref = root->right;
  root->right->ref = root->left;

  own<XrefNode> copy = root;
  assert(copy->ref == copy->left);
  assert(copy->left->ref == copy->right);
  assert(copy->right->ref == copy->left);

  return 0;
}
