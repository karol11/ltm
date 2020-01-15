#ifndef BCML_WRITER_H
#define BCML_WRITER_H

#include <iostream>
#include "dom.h"

namespace bcml {

void write(ltm::pin<dom::Dom> dom, ltm::pin<dom::DomItem> root, std::ostream& file);

}  // namespace bcml

#endif  // BCML_WRITER_H
