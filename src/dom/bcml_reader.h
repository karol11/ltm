#ifndef BCML_READER_H
#define BCML_READER_H

#include <iostream>

#include "dom.h"
#include "../ltm.h"

namespace bcml {

ltm::pin<dom::DomItem> read(ltm::pin<dom::Dom> dom, std::istream& file);

} // namespace bcml

#endif  // BCML_READER_H
