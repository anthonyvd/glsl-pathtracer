#ifndef _SDF_COMPILE_H_
#define _SDF_COMPILE_H_

#include <string>

// Takes a scene defined in our own weird S-expression DSL
// and generate GLSL SDF evaluation code that computes the
// shortest distance to the SDF as well as material properties.
std::string compile_scene(const std::string& scene);

#endif