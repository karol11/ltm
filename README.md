# LTM

LTM - a object lifetime management library:
- Helps to transforms C++ into managed programming language.
- Allows to create arbitrary data models.
- Allows to declaratively express UML abstractions:
    - compositions,
    - aggregation,
    - asoociation.
- Automates copy/destruction operations.
- Maintains sharing/not sharing ownership invariants and topology of cross-references.
- Lightweight and simple.
- Gives the same guarantees as GC without its resource consumption and sudden stops.
- Unlike GC allows to control resources other than RAM.

Usage example:
```C++
struct Document : Object {
    vector<own<Visual>> inner;
    Document(std::initializer_list<pin<Visual>> items);
    LTM_COPYABLE(Document)
};

struct TextBlock : Object {
    string text;
    LTM_COPYABLE(TextBlock)
};

struct SmartConnector : Object {
    weak<Object> start, end;
    LTM_COPYABLE(SmartConnector)
};

pin<Document> create() {
  SmartConnector* sc=0;
  TextBlock *t1=0, *t2=0;
  pin<Document> r = new Document({
    pin<SmartConnector>::make().mark(sc),
    pin<TextBlock>::make().mark(t1),
    pin<TextBlock>::make().mark(t2)}))},
  sc->start = t1;
  sc->end = t2;
  return r;
}

void main() {
  own<Document> doc = create();
  doc->inner[1].cast<TextBlock>()->text = "qwer";

  own<Document> copy = doc;
}
```

More info: (TBD)
