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
using ltm::pin;

struct Node : Object {
  char c;
  own<Node> left, right;

  Node(char c, pin<Node> left = nullptr, pin<Node> right = nullptr)
      : c(c), left(left), right(right) {}
  LTM_COPYABLE(Node)
};

own<Node> root;

int main() {
  root = pin<Node>::make(5,
    pin<Node>::make(1),
    pin<Node>::make(55));

  root->left->left = root;

  assert(root->left->left != root);
  assert(root->left->left->c == root->c);
  assert(root->left->left->left->c == root->left->c);
  return 0;
}
