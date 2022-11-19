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
  uint32_t index_buffer_offset{0};
  uint32_t index_buffer_len{0};
  uint32_t vertex_buffer_index_offset{0};
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
auto Push2Components(const aiVector3D& vertex, std::vector<float>* list) {
  list->insert(list->end(), {vertex.x, vertex.y});
}
auto Push3Components(const aiVector3D& vertex, std::vector<float>* list) {
  list->insert(list->end(), {vertex.x, vertex.y, vertex.z});
}
struct MeshBuffers {
  std::vector<uint32_t> index_buffer;
  std::vector<float> vertex_buffer_position;
  std::vector<float> vertex_buffer_normal;
  std::vector<float> vertex_buffer_tangent;
  std::vector<float> vertex_buffer_bitangent;
  std::vector<float> vertex_buffer_texcoord;
};
auto GatherMeshData(const uint32_t mesh_num, const aiMesh* const * meshes,
                    std::vector<PerDrawCallModelIndexSet>* per_draw_call_model_index_set) {
  std::vector<uint32_t> index_buffer;
  std::vector<float> vertex_buffer_position;
  std::vector<float> vertex_buffer_normal;
  std::vector<float> vertex_buffer_tangent;
  std::vector<float> vertex_buffer_bitangent;
  std::vector<float> vertex_buffer_texcoord;
  uint32_t vertex_buffer_index_offset = 0;
  for (uint32_t i = 0; i < mesh_num; i++) {
    auto mesh = meshes[i];
    if (!mesh->HasFaces()) { continue; }
    if ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0) {
      logwarn("invalid primitive type {}", mesh->mPrimitiveTypes);
      continue;
    }
    auto& per_mesh_data = (*per_draw_call_model_index_set)[i];
    {
      // per mesh index data
      const uint32_t kTriangleVertexNum = 3;
      per_mesh_data.index_buffer_offset = static_cast<uint32_t>(index_buffer.size());
      per_mesh_data.index_buffer_len    = mesh->mNumFaces * kTriangleVertexNum;
      // index buffer
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
    {
      // per mesh vertex buffer data
      per_mesh_data.vertex_buffer_index_offset = vertex_buffer_index_offset;
      vertex_buffer_index_offset += mesh->mNumVertices;
      vertex_buffer_position.reserve((per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      vertex_buffer_normal.reserve((per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      vertex_buffer_tangent.reserve((per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      vertex_buffer_bitangent.reserve((per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      vertex_buffer_texcoord.reserve((per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 2);
      const auto valid_texcoord = (mesh->HasTextureCoords(0) && mesh->mNumUVComponents[0] == 2);
      if (!valid_texcoord) {
        logerror("invalid texcoord existance:{} component num:{}", mesh->HasTextureCoords(0), mesh->mNumUVComponents[0]);
      }
      for (uint32_t j = 0; j < mesh->mNumVertices; j++) {
        Push3Components(mesh->mVertices[j],   &vertex_buffer_position);
        Push3Components(mesh->mNormals[j],    &vertex_buffer_normal);
        Push3Components(mesh->mTangents[j],   &vertex_buffer_tangent);
        Push3Components(mesh->mBitangents[j], &vertex_buffer_bitangent);
        if (valid_texcoord) {
          Push2Components(mesh->mTextureCoords[0][j], &vertex_buffer_texcoord);
        }
      }
      assert(vertex_buffer_position.size() == (per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      assert(vertex_buffer_normal.size() == (per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      assert(vertex_buffer_tangent.size() == (per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      assert(vertex_buffer_bitangent.size() == (per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 3);
      assert(vertex_buffer_texcoord.size() == (per_mesh_data.vertex_buffer_index_offset + mesh->mNumVertices) * 2);
    }
  }
  return MeshBuffers{
    index_buffer,
    vertex_buffer_position,
    vertex_buffer_normal,
    vertex_buffer_tangent,
    vertex_buffer_bitangent,
    vertex_buffer_texcoord,
  };
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
auto GetTransformMatrixList(aiNode* root_node, PerDrawCallModelIndexSet* per_draw_call_model_index_set) {
  std::vector<aiMatrix4x4> transform_matrix_list;
  aiMatrix4x4 transform_matrix;
  PushTransformMatrix(root_node, kInvalidIndex, transform_matrix, per_draw_call_model_index_set, &transform_matrix_list);
  return GetFlattenedMatrixList(std::move(transform_matrix_list));
}
auto OutputBinaryToFile(const size_t file_size_in_byte, const void* buffer, const char* const filename_base, const char* const filename_option) {
  const uint32_t kFilenameLen = 128;
  char filename[kFilenameLen];
  snprintf(filename, kFilenameLen, "%s%s", filename_base, filename_option);
  std::ofstream output_file(filename, std::ios::out | std::ios::binary);
  output_file.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(file_size_in_byte));
}
template <typename T>
auto OutputBinaryToFile(const std::vector<T>& buffer, const char* const filename_base, const char* const filename_option) {
  if (buffer.empty()) { return; }
  OutputBinaryToFile(buffer.size() * sizeof(buffer[0]), buffer.data(), filename_base, filename_option);
}
const char* kIndexBufferBinFileName           = ".index_buffer.bin";
const char* kVertexBufferPositionBinFileName  = ".vertex_buffer_position.bin";
const char* kVertexBufferNormalBinFileName    = ".vertex_buffer_normal.bin";
const char* kVertexBufferTangentBinFileName   = ".vertex_buffer_tangent.bin";
const char* kVertexBufferBitangentBinFileName = ".vertex_buffer_bitangent.bin";
const char* kVertexBufferTexcoordBinFileName  = ".vertex_buffer_texcoord.bin";
auto CreatePerDrawCallJson(const std::vector<PerDrawCallModelIndexSet>& per_draw_call_model_index_set,
                           const std::vector<float>& transform_matrix_list,
                           const MeshBuffers& mesh_buffers,
                           const char* const filename_base) {
  nlohmann::json json;
  return json;
}
void WriteOutJson(const nlohmann::json& json, const char* const filename_base) {
  const uint32_t kFilenameLen = 128;
  char filename[kFilenameLen];
  snprintf(filename, kFilenameLen, "%s%s", filename_base, ".json");
  std::ofstream output_stream(filename);
  output_stream << std::setw(2) << json << std::endl;
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
                                       | aiProcess_EmbedTextures
                                       | aiProcess_RemoveRedundantMaterials);
  // consider using meshoptimizer (https://github.com/zeux/meshoptimizer) for mesh optimizations.
  CHECK_NE(scene, nullptr);
  CHECK_EQ((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE), 0);
  CHECK_UNARY(scene->HasMeshes());
  CHECK_NE(scene->mRootNode, nullptr);
  std::vector<PerDrawCallModelIndexSet> per_draw_call_model_index_set(scene->mNumMeshes);
  const auto transform_matrix_list = GetTransformMatrixList(scene->mRootNode, per_draw_call_model_index_set.data());
  OutputBinaryToFile(transform_matrix_list, filename, ".transform_matrix.bin");
  const auto mesh_buffers = GatherMeshData(scene->mNumMeshes, scene->mMeshes, &per_draw_call_model_index_set);
  OutputBinaryToFile(mesh_buffers.index_buffer, filename, kIndexBufferBinFileName);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_position, filename, kVertexBufferPositionBinFileName);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_normal, filename, kVertexBufferNormalBinFileName);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_tangent, filename, kVertexBufferTangentBinFileName);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_bitangent, filename, kVertexBufferBitangentBinFileName);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_texcoord, filename, kVertexBufferTexcoordBinFileName);
  auto json = CreatePerDrawCallJson(per_draw_call_model_index_set, transform_matrix_list, mesh_buffers, filename);
  WriteOutJson(json, filename);
}
