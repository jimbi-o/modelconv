#include "modelconv/modelconv.h"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"
#include "gfxminimath/gfxminimath.h"
#include "doctest/doctest.h"
TEST_CASE("load model") {
  using namespace modelconv;
  using namespace gfxminimath;
  Assimp::Importer importer;
  const auto scene = importer.ReadFile("donut2022.fbx",
                                       aiProcess_MakeLeftHanded 
                                       | aiProcess_FlipWindingOrder 
                                       | aiProcess_Triangulate 
                                       | aiProcess_CalcTangentSpace
                                       | aiProcess_JoinIdenticalVertices
                                       | aiProcess_ValidateDataStructure
                                       | aiProcess_FixInfacingNormals
                                       | aiProcess_SortByPType
                                       | aiProcess_GenSmoothNormals
                                       | aiProcess_FindInvalidData
                                       | aiProcess_GenUVCoords
                                       | aiProcess_TransformUVCoords
                                       | aiProcess_FindInstances
                                       | aiProcess_Debone
                                       | aiProcess_EmbedTextures);
  // consider using meshoptimizer(https://github.com/zeux/meshoptimizer) for mesh optimizations.
  CHECK_UNARY(scene != nullptr);
  CHECK_UNARY((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) == 0);
  CHECK_UNARY(scene->HasMeshes());
  std::vector<matrix> transform_matrix;
}
