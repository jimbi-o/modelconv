#include "modelconv/modelconv.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "spdlog/spdlog.h"
#include "doctest/doctest.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-macros"
#endif
#define logtrace spdlog::trace
#define logdebug spdlog::debug
#define loginfo  spdlog::info
#define logwarn  spdlog::warn
#define logerror spdlog::error
#define logfatal spdlog::critical
#ifdef __clang__
#pragma clang diagnostic pop
#endif
namespace modelconv {
namespace {
using namespace Assimp;
const uint32_t kInvalidIndex = ~0U;
struct PerDrawCallModelIndexSet {
  std::vector<uint32_t> transform_matrix_index_list;
  uint32_t index_buffer_offset;
  uint32_t index_buffer_len;
};
auto GetUint32(const std::size_t s) {
  return static_cast<uint32_t>(s);
}
void PushTransformMatrix(const aiNode* node,
                         const uint32_t parent_transform_index,
                         const aiMatrix4x4& parent_transform,
                         PerDrawCallModelIndexSet* per_draw_call_model_index_set,
                         std::vector<aiMatrix4x4>* transform_matrix_list) {
  auto transform_index = parent_transform_index;
  auto transform = parent_transform;
  if (!node->mTransformation.IsIdentity()) {
    transform_index = kInvalidIndex;
    transform *= node->mTransformation;
  }
  if (node->mNumMeshes > 0) {
    if (transform_index == kInvalidIndex) {
      transform_index = GetUint32((*transform_matrix_list).size());
      (*transform_matrix_list).push_back(transform);
    }
    for (uint32_t i = 0; i < node->mNumMeshes; i++) {
      per_draw_call_model_index_set[node->mMeshes[i]].transform_matrix_index_list.push_back(transform_index);
    }
  }
  for (uint32_t i = 0; i < node->mNumChildren; i++) {
    PushTransformMatrix(node->mChildren[i], transform_index, transform, per_draw_call_model_index_set, transform_matrix_list);
  }
}
auto GatherMeshData(const uint32_t mesh_num, const aiMesh* const * meshes,
                    std::vector<PerDrawCallModelIndexSet>* per_draw_call_model_index_set) {
  std::vector<uint32_t> index_buffer;
  for (uint32_t i = 0; i < mesh_num; i++) {
    auto mesh = meshes[i];
    if (!mesh->HasFaces()) { continue; }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
      logwarn("invalid primitive type {}", mesh->mPrimitiveTypes);
      continue;
    }
    // per mesh index data
    const uint32_t kTriangleVertexNum = 3;
    auto& per_mesh_data = (*per_draw_call_model_index_set)[i];
    per_mesh_data.index_buffer_offset = static_cast<uint32_t>(index_buffer.size());
    per_mesh_data.index_buffer_len    = mesh->mNumFaces * kTriangleVertexNum;
    // push mesh index buffer
    index_buffer.reserve(per_mesh_data.index_buffer_offset + per_mesh_data.index_buffer_len);
    for (uint32_t j = 0; j < mesh->mNumFaces; j++) {
      const auto& face = mesh->mFaces[j];
      if (face.mNumIndices != kTriangleVertexNum) {
        logerror("invalid face num", face.mNumIndices);
        continue;
      }
      for (uint32_t k = 0; k < kTriangleVertexNum; k++) {
        index_buffer.push_back(face.mIndices[k]);
      }
    }
    assert(index_buffer.size() == per_mesh_data.index_buffer_offset + per_mesh_data.index_buffer_len);
  }
  return index_buffer;
}
auto GetFlattenedMatrixList(const std::vector<aiMatrix4x4>& matrix_list) {
  if (matrix_list.empty()) { return std::vector<float>{}; }
  assert(sizeof(matrix_list[0].a1) == 4);
  const auto kMatrixNum = GetUint32(matrix_list.size());
  const uint32_t kComponentNum = 16;
  const auto kMatrixByteSize = GetUint32(sizeof(float)) * kComponentNum;
  std::vector<float> float_list(kComponentNum * kMatrixNum);
  for (uint32_t i = 0; i < kMatrixNum; i++) {
    memcpy(float_list.data() + i * kComponentNum, &matrix_list[i].a1, kMatrixByteSize);
  }
  return float_list;
}
auto OutputBinaryToFile(const size_t file_size_in_byte, const void* buffer, const char* const filename_base, const char* const filename_option) {
  const uint32_t kFilenameLen = 128;
  char filename[kFilenameLen];
  snprintf(filename, kFilenameLen, "%s%s", filename_base, filename_option);
  std::ofstream output_file(filename, std::ios::out | std::ios::binary);
  output_file.write(reinterpret_cast<const char*>(buffer), file_size_in_byte);
}
} // namespace anonymous
} // namespace modelconv
TEST_CASE("load model") {
  using namespace modelconv;
  const char* const filename = "donut2022.fbx";
  Assimp::Importer importer;
  const auto scene = importer.ReadFile(filename,
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
  // consider using meshoptimizer (https://github.com/zeux/meshoptimizer) for mesh optimizations.
  CHECK_NE(scene, nullptr);
  CHECK_EQ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE), 0);
  CHECK_UNARY(scene->HasMeshes());
  CHECK_NE(scene->mRootNode, nullptr);
  std::vector<PerDrawCallModelIndexSet> per_draw_call_model_index_set(scene->mNumMeshes);
  {
    // output transform matrix binaries
    std::vector<aiMatrix4x4> transform_matrix_list;
    aiMatrix4x4 transform_matrix;
    PushTransformMatrix(scene->mRootNode, kInvalidIndex, transform_matrix, per_draw_call_model_index_set.data(), &transform_matrix_list);
    const auto transform_matrix_list_binary = GetFlattenedMatrixList(transform_matrix_list);
    OutputBinaryToFile(transform_matrix_list_binary.size() * sizeof(float), transform_matrix_list_binary.data(), filename, ".transform_matrix.bin");
  }
  {
    auto index_buffer = GatherMeshData(scene->mNumMeshes, scene->mMeshes, &per_draw_call_model_index_set);
  }
}
