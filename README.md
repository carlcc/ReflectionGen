# ReflectionGen

A simple tool to parse annotated C++ codes.

After that, you can generate code for it.

# How to use it?

1. Write a lua script, in which we specify compiler options to our main program and provide a callback function to generate code.
2. Provide a C++ code file to parse.

An example can be found under `TestData` directory.

```bash
./ReflectionGen Script.lua header.hpp
```
