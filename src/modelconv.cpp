#include "modelconv/modelconv.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "doctest/doctest.h"
TEST_CASE("load model") {
  Assimp::Importer importer;
  const auto scene = importer.ReadFile("filename", 0);
  CHECK_EQ(scene, nullptr);
}
