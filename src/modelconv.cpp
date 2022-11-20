#include "modelconv/modelconv.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <filesystem>
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
  uint32_t vertex_num{0};
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
      per_mesh_data.vertex_num = mesh->mNumVertices;
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
auto OutputBinaryToFile(const size_t file_size_in_byte, const void* buffer, std::ofstream* ofstream) {
  ofstream->write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(file_size_in_byte));
}
template <typename T>
auto OutputBinaryToFile(const std::vector<T>& buffer, std::ofstream* ofstream) {
  if (buffer.empty()) { return; }
  OutputBinaryToFile(buffer.size() * sizeof(buffer[0]), buffer.data(), ofstream);
}
void OutputBinariesToFile(const std::vector<float>& transform_matrix_list,
                          const MeshBuffers& mesh_buffers,
                          const char* const filename) {
  std::ofstream output_file(filename, std::ios::out | std::ios::binary);
  // call order to OutputBinaryToFile must match that of CreateJsonBinaryEntity
  OutputBinaryToFile(transform_matrix_list, &output_file);
  OutputBinaryToFile(mesh_buffers.index_buffer, &output_file);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_position, &output_file);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_normal, &output_file);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_tangent, &output_file);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_bitangent, &output_file);
  OutputBinaryToFile(mesh_buffers.vertex_buffer_texcoord, &output_file);
}
auto CreateJsonBinaryEntity(const std::size_t& size_in_bytes, const std::size_t& stride_in_bytes, const uint32_t offset_in_bytes) {
  nlohmann::json json;
  json["size_in_bytes"] = size_in_bytes;
  json["stride_in_bytes"] = stride_in_bytes;
  json["offset_in_bytes"] = offset_in_bytes;
  return json;
}
template <typename T>
auto CreateJsonBinaryEntity(const std::vector<T>& vector, const uint32_t component_num, const uint32_t offset_in_bytes) {
  if (vector.empty()) {
    return CreateJsonBinaryEntity(0, 0, offset_in_bytes);
  }
  const auto element_num = vector.size();
  const auto per_node_size_in_bytes = sizeof(vector[0]);
  return CreateJsonBinaryEntity(element_num * per_node_size_in_bytes, per_node_size_in_bytes * component_num, offset_in_bytes);
}
auto CreateMeshJson(const std::vector<PerDrawCallModelIndexSet>& per_draw_call_model_index_set) {
  auto json = nlohmann::json::array();
  const auto mesh_num = per_draw_call_model_index_set.size();
  for (uint32_t i = 0; i < mesh_num; i++) {
    const auto& mesh = per_draw_call_model_index_set[i];
    nlohmann::json elem;
    elem["transform"] = nlohmann::json::array();
    for (const auto& transform : mesh.transform_matrix_index_list) {
      elem["transform"].push_back(transform);
    }
    elem["index_buffer_offset"] = mesh.index_buffer_offset;
    elem["index_buffer_len"] = mesh.index_buffer_len;
    elem["vertex_buffer_index_offset"] = mesh.vertex_buffer_index_offset;
    elem["vertex_num"] = mesh.vertex_num;
    json.emplace_back(std::move(elem));
  }
  return json;
}
template <typename T>
auto GetVectorSizeInBytes(const std::vector<T>& vector) {
  return GetUint32(vector.size() * sizeof(vector[0]));
}
auto CreatePerDrawCallJson(const std::vector<PerDrawCallModelIndexSet>& per_draw_call_model_index_set,
                           const std::vector<float>& transform_matrix_list,
                           const MeshBuffers& mesh_buffers,
                           const char* const binary_filename) {
  nlohmann::json json;
  json["meshes"] = CreateMeshJson(per_draw_call_model_index_set);
  json["binary_info"]["filename"] = binary_filename;
  // call order to CreateJsonBinaryEntity must match that of OutputBinaryToFile
  uint32_t offset_in_bytes = 0;
  json["binary_info"]["transform"] = CreateJsonBinaryEntity(transform_matrix_list, 16, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(transform_matrix_list);
  json["binary_info"]["index"]     = CreateJsonBinaryEntity(mesh_buffers.index_buffer, 1, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(mesh_buffers.index_buffer);
  json["binary_info"]["position"]  = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_position, 3, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_position);
  json["binary_info"]["normal"]    = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_normal, 3, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_normal);
  json["binary_info"]["tangent"]   = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_tangent, 3, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_tangent);
  json["binary_info"]["bitangent"] = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_bitangent, 3, offset_in_bytes);
  offset_in_bytes += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_bitangent);
  json["binary_info"]["texcoord"]  = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_texcoord, 2, offset_in_bytes);
  return json;
}
void WriteOutJson(const nlohmann::json& json, const char* const filename) {
  std::ofstream output_stream(filename);
  output_stream << std::setw(2) << json << std::endl;
}
auto GetNameWithoutExtension(const char* const filename) {
  std::string str(filename);
  auto period_pos = str.find_last_of('.');
  return str.substr(0, period_pos);
}
auto MergeStrings(const char* const str1, const char str2, const char* const str3) {
  const uint32_t kBufferLen = 128;
  char buffer[kBufferLen];
  const auto result = snprintf(buffer, kBufferLen, "%s%c%s", str1, str2, str3);
  if (result > 0 && result < kBufferLen) {
    return std::string(buffer);
  }
  logerror("snprintf error:%d %s %s %s", result, str1, str2, str3);
  return std::string();
}
auto GetOutputFilename(const char* const basename, const char* const extension) {
  return MergeStrings(basename, '.', extension);
}
auto GetOutputFilePath(const char* const directory, const char* const filename) {
  return MergeStrings(directory, '/', filename);
}
} // namespace anonymous
} // namespace modelconv
TEST_CASE("load model") {
  using namespace modelconv;
  const char* const filename = "donut2022.fbx";
  const char* const directory = "output";
  const auto basename_str = GetNameWithoutExtension(filename);
  const auto basename = basename_str.c_str();
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
  const auto mesh_buffers = GatherMeshData(scene->mNumMeshes, scene->mMeshes, &per_draw_call_model_index_set);
  const auto binary_filename = GetOutputFilename(basename, "bin");
  const auto output_directory = MergeStrings(directory, '/', basename);
  std::filesystem::create_directory(output_directory);
  OutputBinariesToFile(transform_matrix_list, mesh_buffers, GetOutputFilePath(output_directory.c_str(), binary_filename.c_str()).c_str());
  auto json = CreatePerDrawCallJson(per_draw_call_model_index_set, transform_matrix_list, mesh_buffers, binary_filename.c_str());
  const auto json_filepath = GetOutputFilePath(output_directory.c_str(), GetOutputFilename(basename, "json").c_str());
  WriteOutJson(json, json_filepath.c_str());
  // TODO textures
}
