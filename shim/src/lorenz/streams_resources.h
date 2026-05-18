// Forwarding header: redirects bare-name #include "streams_resources.h"
// (from vendor .cpp files in this directory) to the shim include path.
// Vendor .cpp files use bare-name includes because they were written for
// a build system that adds -Ishim/src/lorenz/. The shim's test_dep build
// rule does not add that flag, so this forwarding header bridges the gap.
#include "lorenz/streams_resources.h"
