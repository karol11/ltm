# LTM (Object Lifetime Manager)

IMPORTANT: This is not an officially supported Google product.

Most of data structures of nowadays applications have tree-like form.
- HTML DOM is a tree of elements.
- Office documents, databases, compiler ASTs,
- GUI- and 3D-scenes - name it yourself. They are always trees of objects.

In UML terminology this structures are composites, and we need composition pointers to maintain them.
Today there is no smart pointer librariy that supports  composition:
`std::shared_ptr` and `std::weak_ptr` define aggregation and association relations,
`std::unique_ptr` lacks of basic copy operations and thus can store only termporary/singleton values.

LTM solves this problem with composite `own<T>` pointer.

It defines copy and destruction semantic in the way, that maintains tree like topology in all operations with straightforward and intuitive manner.

For example if you have a tree:
```C++
struct Node : Object {
    char c;
    own<Node> left, right;
    Node(char c) :c(c) {}
    LTM_COPYABLE(Node)
};
int main() {
    auto root = own<Node>::make('a'); // you can also write own<Node> root = new Node('a');
    root->left.set('b'); // you can also write root.left = new Node('b');
    root->right.set('c'); // you can also write root.right.set<DescendantClass>(constructor parameters);
}
```
What happens if you write `root->right = root;`?
- All (a b c) tree is copied.
- Old root->right value is disposed (c)
- Root->right starts referencing newly created tree: (a b (a' b' c')).

You don't think of this matters anymore.

In addition LTM defines weak<T> pointer that supports arbitrary cross-references between items in the tree.

The hidden magic is how this weak pointers are copied.
They maintain topology of source hierarchy.

Let's look at the expanded tree example:
```C++
struct Node : Object {
    char c;
    own<Node> left, right;
    weak<Node> next; // whatever it means
    Node(char c, weak<Node> n) :c(c), n(n) {}
    LTM_COPYABLE(node)
};
int main() {
    auto root = own<Node>::make('a', nullptr);
    root->left.set('b', root); // 'b'.next references parent object
    root->right.set('c', root->left); // 'c'.next references sibling
    root->next = root; // 'a'.next references itself
}
```
What happens if we write `own<Node> n = root;` ?

Since we assign to an `own<T>` there will be made a distinct copy of root and all its descendants.
And what a copy it will be!

In this copy:
- `n->next == n`
- `n->left->next == n`
- `n->right->next == n->left`
Because this is the way the weak pointers were setup in the original objects.

LTM has direct supports for many other operations and patterns:
- delegates and topology-aware lambdas,
- proxy objects,
- mixin-interface implementations.
- immutable shared objects,
- temporary pin-pointers,
- type-casting,
- declarative hierarchy creation.

It is fast and lightweight.

It prevents leaks better than GC without its overheads.

More info: [intro](docs/intro.md)
