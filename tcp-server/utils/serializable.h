#include "definitions.h"

class Serializable {
public:
    // Virtual destructor is crucial for interfaces to avoid memory leaks
    virtual ~Serializable() = default;

    // 'virtual' allows overriding, '= 0' makes it pure virtual
    // Use 'const' at the end to promise this method doesn't change the object
    virtual bytestream serialize() const = 0; 
};
