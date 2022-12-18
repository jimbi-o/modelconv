#include "modelconv/modelconv.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>
#include "assimp/Importer.hpp"
#include "assimp/GltfMaterial.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "spdlog/spdlog.h"
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
  uint32_t material_index{0};
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
    {
      // per mesh material
      per_mesh_data.material_index = mesh->mMaterialIndex;
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
    elem["material_index"] = mesh.material_index;
    json.emplace_back(std::move(elem));
  }
  return json;
}
template <typename T>
auto GetVectorSizeInBytes(const std::vector<T>& vector) {
  return GetUint32(vector.size() * sizeof(vector[0]));
}
auto CreateJsonBinaryEntityList(const std::vector<float>& transform_matrix_list,
                                const MeshBuffers& mesh_buffers) {
  nlohmann::json json;
  // call order to CreateJsonBinaryEntity must match that of OutputBinaryToFile
  uint32_t offset_in_bytes = 0;
  json["transform"] = CreateJsonBinaryEntity(transform_matrix_list, 16, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(transform_matrix_list);
  json["index"]     = CreateJsonBinaryEntity(mesh_buffers.index_buffer, 1, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(mesh_buffers.index_buffer);
  json["position"]  = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_position, 3, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_position);
  json["normal"]    = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_normal, 3, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_normal);
  json["tangent"]   = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_tangent, 3, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_tangent);
  json["bitangent"] = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_bitangent, 3, offset_in_bytes);
  offset_in_bytes  += GetVectorSizeInBytes(mesh_buffers.vertex_buffer_bitangent);
  json["texcoord"]  = CreateJsonBinaryEntity(mesh_buffers.vertex_buffer_texcoord, 2, offset_in_bytes);
  return json;
}
void WriteOutJson(const nlohmann::json& json, const char* const filename) {
  std::ofstream output_stream(filename);
  output_stream << std::setw(2) << json << std::endl;
}
auto GetFilenameStem(const char* const filename) {
  std::string str(filename);
  auto slash_pos = str.find_last_of('/');
  auto period_pos = str.find_last_of('.');
  if (slash_pos == std::string::npos) {
    slash_pos = 0;
  } else {
    slash_pos++;
    if (slash_pos >= str.length()) {
      logerror("invalid filename {} {}", str.c_str(), slash_pos);
      slash_pos = str.length() - 1;
    }
  }
  if (period_pos == std::string::npos) {
    period_pos = str.length();
  }
  return str.substr(slash_pos, period_pos - slash_pos);
}
auto MergeStrings(const char* const str1, const char str2, const char* const str3) {
  const uint32_t kBufferLen = 128;
  char buffer[kBufferLen];
  const auto result = snprintf(buffer, kBufferLen, "%s%c%s", str1, str2, str3);
  if (result > 0 && result < kBufferLen) {
    return std::string(buffer);
  }
  logerror("snprintf error:{} {} {} {}", result, str1, str2, str3);
  return std::string();
}
auto GetOutputFilename(const char* const basename, const char* const extension) {
  return MergeStrings(basename, '.', extension);
}
auto GetOutputFilePath(const char* const directory, const char* const filename) {
  return MergeStrings(directory, '/', filename);
}
auto GetShadingMode(const aiMaterial& material) {
  int32_t shading_mode;
  if (const auto result = material.Get(AI_MATKEY_SHADING_MODEL, shading_mode); result != AI_SUCCESS) {
    logerror("failed to retrieve AI_MATKEY_SHADING_MODEL {}", result);
    return 0;
  }
  return shading_mode;
}
auto GetMaterialVal(const aiMaterial& material, const char * const key, const uint32_t type, const uint32_t idx, std::vector<float>&& default_val) {
  aiColor4D color;
  if (const auto result = material.Get(key, type, idx, color); result != AI_SUCCESS) {
    logwarn("material: failed to get {} {} {} {}", key, type, idx, result);
    return default_val;
  }
  return std::vector{color.r, color.g, color.b, color.a};
}
auto GetMaterialVal(const aiMaterial& material, const char * const key, const uint32_t type, const uint32_t idx, const float default_val) {
  float val{default_val};
  if (const auto result = material.Get(key, type, idx, val); result != AI_SUCCESS) {
    logwarn("material: failed to get {} {} {} {}", key, type, idx, result);
  }
  return val;
}
auto GetMaterialVal(const aiMaterial& material, const char * const key, const uint32_t type, const uint32_t idx, const bool default_val) {
  bool val{default_val};
  if (const auto result = material.Get(key, type, idx, val); result != AI_SUCCESS) {
    logwarn("material: failed to get {} {} {} {}", key, type, idx, result);
  }
  return val;
}
auto GetMaterialStrVal(const aiMaterial& material, const char * const key, const uint32_t type, const uint32_t idx, std::string&& default_val) {
  aiString val;
  if (const auto result = material.Get(key, type, idx, val); result != AI_SUCCESS) {
    logwarn("material: failed to get {} {} {} {}", key, type, idx, result);
    return default_val;
  }
  return std::string(val.data);
}
struct Texture {
  aiTextureType texture_type{static_cast<aiTextureType>(-1)};
  std::string path;
};
struct Sampler {
  static const uint32_t kMapModeNum = 3;
  aiTextureMapMode mapmode[kMapModeNum];
  uint32_t mag_filter;
  uint32_t min_filter;
};
auto IsValidMapMode(const aiTextureMapMode mapmode) {
  switch (mapmode) {
    case aiTextureMapMode_Wrap:
      return true;
    case aiTextureMapMode_Clamp:
      return true;
    case aiTextureMapMode_Decal:
      return true;
    case aiTextureMapMode_Mirror:
      return true;
  }
  return false;
}
bool IsMapModeIdentical(const aiTextureMapMode* a, const aiTextureMapMode* b) {
  for (uint32_t i = 0; i < Sampler::kMapModeNum; i++) {
    if (!IsValidMapMode(a[i]) && !IsValidMapMode(b[i])) { continue; }
    if (a[i] != b[i]) { return false; }
  }
  return true;
}
const uint32_t kInvalidTexture = ~0U;
auto GetTextureIndex(const aiTextureType texture_type, const char* const path, const std::vector<Texture>& textures) {
  const auto count = GetUint32(textures.size());
  for (uint32_t i = 0; i < count; i++) {
    const auto& texture = textures[i];
    if (texture_type != texture.texture_type) { continue; }
    if (strcmp(path, texture.path.c_str()) != 0) { continue; }
    return i;
  }
  return kInvalidTexture;
}
constexpr auto CreateTexture(const aiTextureType texture_type, const char* const path) {
  return Texture{
    .texture_type = texture_type,
    .path = path,
  };
}
const uint32_t kInvalidSampler = ~0U;
auto GetSamplerIndex(const aiTextureMapMode mapmode[3], const uint32_t mag_filter, const uint32_t min_filter, const std::vector<Sampler>& samplers) {
  const auto count = GetUint32(samplers.size());
  for (uint32_t i = 0; i < count; i++) {
    const auto& sampler = samplers[i];
    if (!IsMapModeIdentical(mapmode, sampler.mapmode)) { continue; }
    if (mag_filter != sampler.mag_filter) { continue; }
    if (min_filter != sampler.min_filter) { continue; }
    return i;
  }
  return kInvalidSampler;
}
constexpr auto CreateSampler(const aiTextureMapMode mapmode[3], const uint32_t mag_filter, const uint32_t min_filter) {
  return Sampler{
    .mapmode = {mapmode[0], mapmode[1], mapmode[2]},
    .mag_filter = mag_filter,
    .min_filter = min_filter,
  };
}
auto FindOrCreateTexture(const aiTextureType texture_type, const char* const path, std::vector<Texture>* textures) {
  if (const auto texture_index = GetTextureIndex(texture_type, path, *textures); texture_index != kInvalidTexture) {
    return texture_index;
  }
  const auto texture_index = GetUint32(textures->size());
  textures->push_back(CreateTexture(texture_type, path));
  return texture_index;
}
auto FindOrCreateSampler(const aiTextureMapMode mapmode[3], const uint32_t mag_filter, const uint32_t min_filter, std::vector<Sampler>* samplers) {
  if (const auto sampler_index = GetSamplerIndex(mapmode, mag_filter, min_filter, *samplers); sampler_index != kInvalidSampler) {
    return sampler_index;
  }
  const auto sampler_index = GetUint32(samplers->size());
  samplers->push_back(CreateSampler(mapmode, mag_filter, min_filter));
  return sampler_index;
}
const uint32_t SamplerFilter_UNSET = 0;
const uint32_t SamplerMagFilter_Nearest = 9728;
const uint32_t SamplerMagFilter_Linear = 9729;
const uint32_t SamplerMinFilter_Nearest = 9728;
const uint32_t SamplerMinFilter_Linear = 9729;
const uint32_t SamplerMinFilter_Nearest_Mipmap_Nearest = 9984;
const uint32_t SamplerMinFilter_Linear_Mipmap_Nearest = 9985;
const uint32_t SamplerMinFilter_Nearest_Mipmap_Linear = 9986;
const uint32_t SamplerMinFilter_Linear_Mipmap_Linear = 9987;
auto CreateDefaultMaterial(const aiTextureType texture_type, std::vector<Texture>* textures, std::vector<Sampler>* samplers) {
  nlohmann::json texture_json;
  switch (texture_type) {
    case aiTextureType_UNKNOWN: // occulusion-metallic-roughness
      texture_json["texture"] = FindOrCreateTexture(aiTextureType_UNKNOWN, "yellow", textures);
      break;
    case aiTextureType_BASE_COLOR:
      texture_json["texture"] = FindOrCreateTexture(aiTextureType_BASE_COLOR, "white", textures);
      break;
    case aiTextureType_NORMALS:
      texture_json["texture"] = FindOrCreateTexture(aiTextureType_NORMALS, "normal", textures);
      break;
    case aiTextureType_EMISSIVE:
      texture_json["texture"] = FindOrCreateTexture(aiTextureType_EMISSIVE, "black", textures);
      break;
  }
  const aiTextureMapMode mapmode[] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, static_cast<aiTextureMapMode>(-1)};
  texture_json["sampler"] = FindOrCreateSampler(mapmode, SamplerMagFilter_Linear, SamplerMinFilter_Linear_Mipmap_Linear, samplers);
  return texture_json;
}
auto GetTexture(const aiMaterial& material, const aiTextureType texture_type, std::vector<Texture>* textures, std::vector<Sampler>* samplers) {
  if (material.GetTextureCount(texture_type) == 0) {
    return CreateDefaultMaterial(texture_type, textures, samplers);
  }
  if (material.GetTextureCount(texture_type) > 1) {
    logwarn("multiple texture not implemented {}", texture_type);
    return CreateDefaultMaterial(texture_type, textures, samplers);
  }
  aiString path;
  aiTextureMapping mapping;
  unsigned int uvindex;
  ai_real blend;
  aiTextureOp op;
  aiTextureMapMode mapmode[3];
  const uint32_t slot = 0;
  if (const auto result = material.GetTexture(texture_type, slot, &path, &mapping, &uvindex, &blend, &op, mapmode); result != AI_SUCCESS) {
    return CreateDefaultMaterial(texture_type, textures, samplers);
  }
  if (mapping != aiTextureMapping::aiTextureMapping_UV) {
    logerror("only uv mapping is supported {}", mapping);
    return CreateDefaultMaterial(texture_type, textures, samplers);
  }
  if (uvindex != 0) {
    logerror("only uv 0 supported so far. {}", uvindex);
    return CreateDefaultMaterial(texture_type, textures, samplers);
  }
  nlohmann::json texture_json;
  texture_json["texture"] = FindOrCreateTexture(texture_type, path.C_Str(), textures);
  aiUVTransform transform{};
  if (const auto result = material.Get(AI_MATKEY_UVTRANSFORM(texture_type, slot), transform); result == AI_SUCCESS) {
    // not needed so far.
  }
  uint32_t mag_filter{SamplerMagFilter_Linear};
  uint32_t min_filter{SamplerMinFilter_Linear_Mipmap_Linear};
  material.Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(texture_type, slot), mag_filter);
  material.Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(texture_type, slot), min_filter);
  texture_json["sampler"] = FindOrCreateSampler(mapmode, mag_filter, min_filter, samplers);
  return texture_json;
}
std::string GetMapMode(const aiTextureMapMode mapmode) {
  switch (mapmode) {
    case aiTextureMapMode_Wrap:
      return "wrap";
    case aiTextureMapMode_Clamp:
      return "clamp";
    case aiTextureMapMode_Decal:
      logwarn("aiTextureMapMode_Decal not implemented");
      return "wrap";
    case aiTextureMapMode_Mirror:
      return "mirror";
  }
  return std::string();
}
std::string GetMagFilter(const uint32_t mag_filter) {
  switch (mag_filter) {
    case SamplerFilter_UNSET:
      return "linear";
    case SamplerMagFilter_Nearest:
      return "point";
    case SamplerMagFilter_Linear:
      return "linear";
  }
  logerror("invalid value for mag filter {}", mag_filter);
  return "linear";
}
std::string GetMinFilter(const uint32_t min_filter) {
  switch (min_filter) {
    case SamplerFilter_UNSET:
      return "linear";
    case SamplerMinFilter_Nearest:
      return "point";
    case SamplerMinFilter_Linear:
      return "linear";
    case SamplerMinFilter_Nearest_Mipmap_Nearest:
      return "point";
    case SamplerMinFilter_Linear_Mipmap_Nearest:
      return "linear";
    case SamplerMinFilter_Nearest_Mipmap_Linear:
      return "point";
    case SamplerMinFilter_Linear_Mipmap_Linear:
      return "linear";
  }
  logerror("invalid value for min filter {}", min_filter);
  return "linear";
}
std::string GetMipFilter(const uint32_t min_filter) {
  switch (min_filter) {
    case SamplerFilter_UNSET:
      return "linear";
    case SamplerMinFilter_Nearest:
      return "linear";
    case SamplerMinFilter_Linear:
      return "linear";
    case SamplerMinFilter_Nearest_Mipmap_Nearest:
      return "point";
    case SamplerMinFilter_Linear_Mipmap_Nearest:
      return "point";
    case SamplerMinFilter_Nearest_Mipmap_Linear:
      return "linear";
    case SamplerMinFilter_Linear_Mipmap_Linear:
      return "linear";
  }
  logerror("invalid value for mip filter {}", min_filter);
  return "linear";
}
auto CreateTextureJson(const std::vector<Texture>& textures) {
  auto json = nlohmann::json::array();
  for (const auto& t : textures) {
    nlohmann::json j;
    switch (t.texture_type) {
      case aiTextureType_UNKNOWN: // occulusion-metallic-roughness
        j["type"] = "occulusion-metallic-roughness";
        break;
      case aiTextureType_BASE_COLOR:
        j["type"] = "albedo";
        break;
      case aiTextureType_NORMALS:
        j["type"] = "normal";
        break;
      case aiTextureType_EMISSIVE:
        j["type"] = "emissive";
        break;
    }
    j["path"] = t.path;
    json.emplace_back(std::move(j));
  }
  return json;
}
auto CreateSamplerJson(const std::vector<Sampler>& samplers) {
  auto json = nlohmann::json::array();
  for (const auto& s : samplers) {
    auto mapmode = nlohmann::json::array();
    for (uint32_t i = 0; i < Sampler::kMapModeNum; i++) {
      if (!IsValidMapMode(s.mapmode[i])) { continue; }
      mapmode.emplace_back(GetMapMode(s.mapmode[i]));
    }
    nlohmann::json j;
    j["mapmode"] = std::move(mapmode);
    j["mag_filter"] = GetMagFilter(s.mag_filter);
    j["min_filter"] = GetMinFilter(s.min_filter);
    j["mip_filter"] = GetMipFilter(s.min_filter);
    json.emplace_back(std::move(j));
  }
  return json;
}
auto CreateJsonMaterialList(const uint32_t material_num, const aiMaterial * const * const materials, const bool is_gltf) {
  auto json = nlohmann::json::array();
  std::vector<Texture> textures;
  std::vector<Sampler> samplers;
  for (uint32_t i = 0; i < material_num; i++) {
    const auto& material = *(materials[i]);
    nlohmann::json material_json;
    if (const auto shading_mode = GetShadingMode(material); shading_mode != aiShadingMode_PBR_BRDF) {
      logwarn("only pbr/brdf is loaded so far. {}", shading_mode);
      continue;
    }
    // assimp/code/AssetLib/glTF2/glTF2Asset.h Material
    // assimp/code/AssetLib/glTF2/glTF2Importer.cpp ImportMaterial
    {
      material_json["albedo"]["texture"] = GetTexture(material, aiTextureType_BASE_COLOR, &textures, &samplers);
      material_json["albedo"]["factor"]  = GetMaterialVal(material, AI_MATKEY_BASE_COLOR, {1.0f, 1.0f, 1.0f, 1.0f});
    }
    // https://github.com/sbtron/glTF/blob/30de0b365d1566b1bbd8b9c140f9e995d3203226/specification/2.0/README.md#pbrmetallicroughnessmetallicroughnesstexture
    if (is_gltf) {
      auto texture = GetTexture(material, aiTextureType_UNKNOWN /*=AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE*/, &textures, &samplers);
      material_json["occlusion"]["texture"] = texture;
      material_json["occlusion"]["texture"]["channel"] = 0; // for R channel
      material_json["occlusion"]["strength"] = GetMaterialVal(material, AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_LIGHTMAP, 0), 1.0f);
      material_json["metallic"]["texture"] = texture;
      material_json["metallic"]["texture"]["channel"] = 1; // for G channel
      material_json["metallic"]["factor"] = GetMaterialVal(material, AI_MATKEY_METALLIC_FACTOR, 1.0f);
      material_json["roughness"]["texture"] = texture;
      material_json["roughness"]["texture"]["channel"]  = 2; // for B channel
      material_json["roughness"]["factor"] = GetMaterialVal(material, AI_MATKEY_ROUGHNESS_FACTOR, 1.0f);
    } else {
      logerror("none-gltf model load not implemented.");
      // post-process to combine occlusion(R),metallic(G),roughness(B) textues to single texture not implemented yet.
      // consider using libvips.
      assert(false);
    }
    {
      material_json["normal"]["texture"] = GetTexture(material, aiTextureType_NORMALS, &textures, &samplers);
      material_json["normal"]["scale"]   = GetMaterialVal(material, AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), 1.0f);
    }
    {
      material_json["emissive"]["texture"] = GetTexture(material, aiTextureType_EMISSIVE, &textures, &samplers);
      material_json["emissive"]["factor"]  = GetMaterialVal(material, AI_MATKEY_COLOR_EMISSIVE, {1.0f, 1.0f, 1.0f, 1.0f});
    }
    material_json["double_sided"] = GetMaterialVal(material, AI_MATKEY_TWOSIDED, false);
    material_json["alpha_mode"] = GetMaterialStrVal(material, AI_MATKEY_GLTF_ALPHAMODE, "OPAQUE");
    material_json["alpha_cutoff"] = GetMaterialVal(material, AI_MATKEY_GLTF_ALPHACUTOFF, 0.2f);
    json.emplace_back(std::move(material_json));
  }
  nlohmann::json ret;
  ret["materials"] = std::move(json);
  ret["textures"] = CreateTextureJson(textures);
  ret["samplers"] = CreateSamplerJson(samplers);
  return ret;
}
} // namespace anonymous
void OutputToDirectory(const char* const input_filepath, const char* const output_dir_root) {
  using namespace modelconv;
  const auto basename_str = GetFilenameStem(input_filepath);
  const auto basename = basename_str.c_str();
  Assimp::Importer importer;
  const auto scene = importer.ReadFile(input_filepath,
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
                                       | aiProcess_RemoveRedundantMaterials);
  // consider using meshoptimizer (https://github.com/zeux/meshoptimizer) for mesh optimizations.
  if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || !scene->HasMeshes() || scene->mRootNode == nullptr) {
    logerror("failed to load scene. {}", input_filepath);
    return;
  }
  std::vector<PerDrawCallModelIndexSet> per_draw_call_model_index_set(scene->mNumMeshes);
  const auto transform_matrix_list = GetTransformMatrixList(scene->mRootNode, per_draw_call_model_index_set.data());
  const auto mesh_buffers = GatherMeshData(scene->mNumMeshes, scene->mMeshes, &per_draw_call_model_index_set);
  const auto binary_filename = GetOutputFilename(basename, "bin");
  const auto output_directory = MergeStrings(output_dir_root, '/', basename);
  std::filesystem::create_directory(output_directory);
  OutputBinariesToFile(transform_matrix_list, mesh_buffers, GetOutputFilePath(output_directory.c_str(), binary_filename.c_str()).c_str());
  nlohmann::json json;
  json["meshes"] = CreateMeshJson(per_draw_call_model_index_set);
  json["binary_info"] = CreateJsonBinaryEntityList(transform_matrix_list, mesh_buffers);
  json["binary_filename"] = binary_filename;
  json["material_settings"] = CreateJsonMaterialList(scene->mNumMaterials, scene->mMaterials, true);
  json["output_directory"] = output_directory;
  const auto json_filepath = GetOutputFilePath(output_directory.c_str(), GetOutputFilename(basename, "json").c_str());
  WriteOutJson(json, json_filepath.c_str());
}
} // namespace modelconv
#include "doctest/doctest.h"
TEST_CASE("load model") {
  using namespace modelconv;
#if 0
  const char* const filename = "donut2022.fbx";
#else
  const char* const filename = "glTF/BoomBoxWithAxes.gltf";
#endif
  const char* const directory = "output";
  const auto basename_str = GetFilenameStem(filename);
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
                                       | aiProcess_RemoveRedundantMaterials);
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
  nlohmann::json json;
  json["meshes"] = CreateMeshJson(per_draw_call_model_index_set);
  json["binary_info"] = CreateJsonBinaryEntityList(transform_matrix_list, mesh_buffers);
  json["binary_filename"] = binary_filename;
  json["material_settings"] = CreateJsonMaterialList(scene->mNumMaterials, scene->mMaterials, true);
  const auto json_filepath = GetOutputFilePath(output_directory.c_str(), GetOutputFilename(basename, "json").c_str());
  WriteOutJson(json, json_filepath.c_str());
}
TEST_CASE("interface test") {
  modelconv::OutputToDirectory("glTF/BoomBoxWithAxes.gltf", "output");
}
