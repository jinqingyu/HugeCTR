/*
 * Copyright (c) 2020, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <common.hpp>
#include <data_reader.hpp>
#include <embedding.hpp>
#include <fstream>
#include <functional>
#include <gpu_resource.hpp>
#include <learning_rate_scheduler.hpp>
#include <metrics.hpp>
#include <network.hpp>
#include <nlohmann/json.hpp>

namespace HugeCTR {

// inline to avoid build error: multiple definition
inline nlohmann::json read_json_file(const std::string& filename) {
  nlohmann::json config;
  std::ifstream file_stream(filename);
  if (!file_stream.is_open()) {
    CK_THROW_(Error_t::FileCannotOpen, "file_stream.is_open() failed: " + filename);
  }
  file_stream >> config;
  file_stream.close();
  return config;
}

/**
 * Solver Parser.
 * This class is designed to parse the solver clause of the configure file.
 */
struct SolverParser {
  //  std::string configure_file;
  unsigned long long seed;                  /**< seed of data simulator */
  LrPolicy_t lr_policy;                     /**< the only fixed lr is supported now. */
  int display;                              /**< the interval of loss display. */
  int max_iter;                             /**< the number of iterations for training */
  int num_epochs;                           /**< the number of epochs for training */
  int snapshot;                             /**< the number of iterations for a snapshot */
  std::string snapshot_prefix;              /**< naming prefix of snapshot file */
  int eval_interval;                        /**< the interval of evaluations */
  int max_eval_batches;                         /**< the number of batches for evaluations */
  int batchsize_eval;                       /**< batchsize for eval */
  int batchsize;                            /**< batchsize */
  std::string model_file;                   /**< name of model file */
  std::string dense_opt_states_file;
  std::vector<std::string> embedding_files; /**< name of embedding file */
  std::vector<std::string> sparse_opt_states_files;
  std::vector<std::vector<int>> vvgpu;      /**< device map */
  bool use_mixed_precision;
  bool enable_tf32_compute;
  float scaler;
  std::map<metrics::Type, float> metrics_spec;
  bool i64_input_key;
  bool use_algorithm_search;
  bool use_cuda_graph;
  std::string export_predictions_prefix;
  bool use_model_oversubscriber;
  std::string temp_embedding_dir;
  SolverParser(const std::string& file);
  SolverParser() {}
};

struct InferenceParser {
  //  std::string configure_file;
  float hit_rate_threshold;                    /**< hit rate threshold for updateing embedding cache */
  size_t max_batchsize;                        /**< batchsize */
  size_t num_embedding_tables;                 /**< number of embedding tables */
  size_t max_embedding_vector_size_per_sample; /**< max embedding vector size per sample */
  size_t max_feature_num_per_sample;           /**< max feature number per table */
  size_t slot_num;                             /**< total slot number */
  size_t label_dim;                            /**< dense feature dimension */
  size_t dense_dim;                            /**< dense feature dimension */
  std::string dense_model_file;                /**< name of model file */
  std::vector<std::string> sparse_model_files; /**< name of embedding file */
  std::vector<std::size_t>
      max_feature_num_for_tables; /**< max feature number of each embedding table */
  std::vector<std::size_t>
      embed_vec_size_for_tables; /**< embedding vector size for each embedding table */
  std::vector<std::size_t> slot_num_for_tables; /**< slot_num for each embedding table */
  bool use_mixed_precision;
  float scaler;
  bool i64_input_key;
  bool use_algorithm_search;
  bool use_cuda_graph;
  InferenceParser(const nlohmann::json& config);
};

/**
 * @brief The parser of configure file (in json format).
 *
 * The builder of each layer / optimizer in HugeCTR.
 * Please see User Guide to learn how to write a configure file.
 * @verbatim
 * Some Restrictions:
 *  1. Embedding should be the first element of layers.
 *  2. layers should be listed from bottom to top.
 * @endverbatim
 */
class Parser {
 private:
  nlohmann::json config_;  /**< configure file. */
  size_t batch_size_;      /**< batch size. */
  size_t batch_size_eval_; /**< batch size. */
  const bool repeat_dataset_;
  const bool i64_input_key_{false};
  const bool use_mixed_precision_{false};
  const bool enable_tf32_compute_{false};

  const float scaler_{1.f};
  const bool use_algorithm_search_;
  const bool use_cuda_graph_;

  std::map<std::string, bool> tensor_active_; /**< whether a tensor is active. */

  template <typename TypeKey>
  void create_pipeline_internal(std::shared_ptr<IDataReader>& data_reader,
                                std::shared_ptr<IDataReader>& data_reader_eval,
                                std::vector<std::shared_ptr<IEmbedding>>& embeddings,
                                std::vector<std::shared_ptr<Network>>& network,
                                const std::shared_ptr<ResourceManager>& resource_manager);

  template <typename TypeEmbeddingComp>
  void create_pipeline_inference(const InferenceParser& inference_parser,
                                 Tensor2<float>& dense_input,
                                 std::vector<std::shared_ptr<Tensor2<int>>>& rows,
                                 std::vector<std::shared_ptr<Tensor2<float>>>& embeddingvecs,
                                 std::vector<size_t>& embedding_table_slot_size,
                                 std::vector<std::shared_ptr<Layer>>* embedding, Network** network,
                                 const std::shared_ptr<ResourceManager> resource_manager);

 public:
  std::vector<TensorEntry> tensor_entries;
  /**
   * Ctor.
   * Ctor only verify the configure file, doesn't create pipeline.
   */
  Parser(const std::string& configure_file, size_t batch_size, size_t batch_size_eval,
         bool repeat_dataset, bool i64_input_key = false, bool use_mixed_precision = false,
         bool enable_tf32_compute = false, float scaler = 1.0f, bool use_algorithm_search = true,
         bool use_cuda_graph = true);

  /**
   * Ctor.
   * Ctor used in inference stage
   */
  Parser(const nlohmann::json& config);

  /**
   * Create the pipeline, which includes data reader, embedding.
   */
  void create_pipeline(std::shared_ptr<IDataReader>& train_data_reader,
                       std::shared_ptr<IDataReader>& evaluate_data_reader,
                       std::vector<std::shared_ptr<IEmbedding>>& embeddings,
                       std::vector<std::shared_ptr<Network>>& networks,
                       const std::shared_ptr<ResourceManager>& resource_manager);

  /**
   * Create inference pipeline, which only creates network and embedding
   */
  void create_pipeline(const InferenceParser& inference_parser, Tensor2<float>& dense_input,
                       std::vector<std::shared_ptr<Tensor2<int>>>& row,
                       std::vector<std::shared_ptr<Tensor2<float>>>& embeddingvec,
                       std::vector<size_t>& embedding_table_slot_size,
                       std::vector<std::shared_ptr<Layer>>* embedding, Network** network,
                       const std::shared_ptr<ResourceManager> resource_manager);
};

std::unique_ptr<LearningRateScheduler> get_learning_rate_scheduler(
    const std::string configure_file);

template <typename T>
struct SparseInput {
  Tensors2<T> train_row_offsets;
  Tensors2<T> train_values;
  std::vector<std::shared_ptr<size_t>> train_nnz;
  Tensors2<T> evaluate_row_offsets;
  Tensors2<T> evaluate_values;
  std::vector<std::shared_ptr<size_t>> evaluate_nnz;
  size_t slot_num;
  size_t max_feature_num_per_sample;
  SparseInput(int slot_num_in, int max_feature_num_per_sample_in)
      : slot_num(slot_num_in), max_feature_num_per_sample(max_feature_num_per_sample_in) {}
  SparseInput() {}
};

#define HAS_KEY_(j_in, key_in)                                          \
  do {                                                                  \
    const nlohmann::json& j__ = (j_in);                                 \
    const std::string& key__ = (key_in);                                \
    if (j__.find(key__) == j__.end())                                   \
      CK_THROW_(Error_t::WrongInput, "[Parser] No Such Key: " + key__); \
  } while (0)

#define CK_SIZE_(j_in, j_size)                                                                  \
  do {                                                                                          \
    const nlohmann::json& j__ = (j_in);                                                         \
    if (j__.size() != (j_size)) CK_THROW_(Error_t::WrongInput, "[Parser] Array size is wrong"); \
  } while (0)

#define FIND_AND_ASSIGN_INT_KEY(out, json)      \
  do {                                          \
    out = 0;                                    \
    if (json.find(#out) != json.end()) {        \
      out = json.find(#out).value().get<int>(); \
    }                                           \
  } while (0)

#define FIND_AND_ASSIGN_STRING_KEY(out, json)           \
  do {                                                  \
    out.clear();                                        \
    if (json.find(#out) != json.end()) {                \
      out = json.find(#out).value().get<std::string>(); \
    }                                                   \
  } while (0)

const std::map<std::string, Layer_t> LAYER_TYPE_MAP = {
    {"Add", Layer_t::Add},
    {"BatchNorm", Layer_t::BatchNorm},
    {"BinaryCrossEntropyLoss", Layer_t::BinaryCrossEntropyLoss},
    {"Cast", Layer_t::Cast},
    {"Concat", Layer_t::Concat},
    {"CrossEntropyLoss", Layer_t::CrossEntropyLoss},
    {"DotProduct", Layer_t::DotProduct},
    {"Dropout", Layer_t::Dropout},
    {"ELU", Layer_t::ELU},
    {"FmOrder2", Layer_t::FmOrder2},
    {"InnerProduct", Layer_t::InnerProduct},
    {"Interaction", Layer_t::Interaction},
    {"MultiCross", Layer_t::MultiCross},
    {"MultiCrossEntropyLoss", Layer_t::MultiCrossEntropyLoss},
    {"WeightMultiply", Layer_t::WeightMultiply},
    {"ReduceSum", Layer_t::ReduceSum},
    {"ReLU", Layer_t::ReLU},
    {"Reshape", Layer_t::Reshape},
    {"Sigmoid", Layer_t::Sigmoid},
    {"Slice", Layer_t::Slice}};
const std::map<std::string, Layer_t> LAYER_TYPE_MAP_MP = {
    {"Add", Layer_t::Add},
    {"BatchNorm", Layer_t::BatchNorm},
    {"BinaryCrossEntropyLoss", Layer_t::BinaryCrossEntropyLoss},
    {"Cast", Layer_t::Cast},
    {"Concat", Layer_t::Concat},
    {"DotProduct", Layer_t::DotProduct},
    {"Dropout", Layer_t::Dropout},
    {"ELU", Layer_t::ELU},
    {"FmOrder2", Layer_t::FmOrder2},
    {"FusedInnerProduct", Layer_t::FusedInnerProduct},
    {"InnerProduct", Layer_t::InnerProduct},
    {"Interaction", Layer_t::Interaction},
    {"WeightMultiply", Layer_t::WeightMultiply},
    {"ReduceSum", Layer_t::ReduceSum},
    {"ReLU", Layer_t::ReLU},
    {"Reshape", Layer_t::Reshape},
    {"Sigmoid", Layer_t::Sigmoid},
    {"Slice", Layer_t::Slice}};
const std::map<std::string, Embedding_t> EMBEDDING_TYPE_MAP = {
    {"DistributedSlotSparseEmbeddingHash", Embedding_t::DistributedSlotSparseEmbeddingHash},
    {"LocalizedSlotSparseEmbeddingHash", Embedding_t::LocalizedSlotSparseEmbeddingHash},
    {"LocalizedSlotSparseEmbeddingOneHot", Embedding_t::LocalizedSlotSparseEmbeddingOneHot}};
const std::map<std::string, Initializer_t> INITIALIZER_TYPE_MAP = {
    {"Uniform", Initializer_t::Uniform},
    {"XavierNorm", Initializer_t::XavierNorm},
    {"XavierUniform", Initializer_t::XavierUniform},
    {"Zero", Initializer_t::Zero}};

static const std::map<std::string, Optimizer_t> OPTIMIZER_TYPE_MAP = {
    {"Adam", Optimizer_t::Adam},
    {"MomentumSGD", Optimizer_t::MomentumSGD},
    {"Nesterov", Optimizer_t::Nesterov},
    {"SGD", Optimizer_t::SGD}};

static const std::map<std::string, Update_t> UPDATE_TYPE_MAP = {
    {"Local", Update_t::Local}, {"Global", Update_t::Global}, {"LazyGlobal", Update_t::LazyGlobal}};

static const std::map<std::string, Regularizer_t> REGULARIZER_TYPE_MAP = {
    {"L1", Regularizer_t::L1},
    {"L2", Regularizer_t::L2},
};

inline bool has_key_(const nlohmann::json& j_in, const std::string& key_in) {
  if (j_in.find(key_in) == j_in.end()) {
    return false;
  } else {
    return true;
  }
}

inline const nlohmann::json& get_json(const nlohmann::json& json, const std::string key) {
  HAS_KEY_(json, key);
  return json.find(key).value();
}

template <typename T>
inline T get_value_from_json(const nlohmann::json& json, const std::string key) {
  HAS_KEY_(json, key);
  auto value = json.find(key).value();
  CK_SIZE_(value, 1);
  return value.get<T>();
}

template <typename T>
inline T get_value_from_json_soft(const nlohmann::json& json, const std::string key, T B) {
  if (has_key_(json, key)) {
    auto value = json.find(key).value();
    CK_SIZE_(value, 1);
    return value.get<T>();
  } else {
    MESSAGE_(key + " is not specified using default: " + std::to_string(B));
    return B;
  }
}

template <typename Type>
struct get_optimizer_param {
  OptParams<Type> operator()(const nlohmann::json& j_optimizer);
};

inline void activate_tensor(std::map<std::string, bool>& tensor_active, std::string top_name) {
  if (tensor_active.find(top_name) != tensor_active.end()) {
    CK_THROW_(Error_t::WrongInput, top_name + ", top tensor name already exists");
  }
  tensor_active.insert(std::pair<std::string, bool>(top_name, true));
}

inline void deactivate_tensor(std::map<std::string, bool>& tensor_active, std::string bottom_name) {
  if (tensor_active.find(bottom_name) == tensor_active.end()) {
    CK_THROW_(Error_t::WrongInput, bottom_name + ", bottem tensor name does not exists");
  }
  if (tensor_active[bottom_name] == false) {
    CK_THROW_(Error_t::WrongInput, bottom_name + ", bottem tensor already consumed");
  }
  tensor_active[bottom_name] = false;
}

inline std::vector<std::string> get_layer_names(const nlohmann::json& json) {
  std::vector<std::string> layer_names;
  if (json.is_array()) {
    for (auto j : json) {
      layer_names.push_back(j.get<std::string>());
    }
  } else {
    layer_names.push_back(json.get<std::string>());
  }
  return layer_names;
}

inline void check_graph(std::map<std::string, bool>& tensor_active, const nlohmann::json& j_layers) {
  // activate label, dense and sparse input tensors
  const nlohmann::json& j_data = j_layers[0];
  if (has_key_(get_json(j_data, "label"), "top")) {
    auto label_name = get_value_from_json<std::string>(get_json(j_data, "label"), "top");
    activate_tensor(tensor_active, label_name);
  }
  auto dense_name = get_value_from_json<std::string>(get_json(j_data, "dense"), "top");
  activate_tensor(tensor_active, dense_name);
  auto j_sparse = get_json(j_data, "sparse");
  for (unsigned int k = 0; k < j_sparse.size(); k++) {
    const auto sparse_name = get_value_from_json<std::string>(j_sparse[k], "top");
    activate_tensor(tensor_active, sparse_name);
  }
  // deactivate bottom tensors and activate top tensors
  for (unsigned int i = 1; i < j_layers.size(); i++) {
    auto bottom = get_json(j_layers[i], "bottom");
    auto top = get_json(j_layers[i], "top");
    std::vector<std::string> bottom_names = get_layer_names(bottom);
    std::vector<std::string> top_names = get_layer_names(top);
    for (auto& bottom_name : bottom_names) {
      deactivate_tensor(tensor_active, bottom_name);
    }
    for (auto& top_name : top_names) {
      activate_tensor(tensor_active, top_name);
    }
  }
}

template <typename TypeKey, typename TypeFP>
struct create_embedding {
  void operator()(std::map<std::string, SparseInput<TypeKey>>& sparse_input_map,
                  std::vector<TensorEntry>* train_tensor_entries_list,
                  std::vector<TensorEntry>* evaluate_tensor_entries_list,
                  std::vector<std::shared_ptr<IEmbedding>>& embeddings, Embedding_t embedding_type,
                  const nlohmann::json& config,
                  const std::shared_ptr<ResourceManager>& resource_manager, size_t batch_size,
                  size_t batch_size_eval, bool use_mixed_precision, float scaler,
                  const nlohmann::json& j_layers);

  void operator()(const InferenceParser& inference_parser, const nlohmann::json& j_layers_array,
                  std::vector<std::shared_ptr<Tensor2<int>>>& rows,
                  std::vector<std::shared_ptr<Tensor2<float>>>& embeddingvecs,
                  std::vector<size_t>& embedding_table_slot_size,
                  std::vector<TensorEntry>* tensor_entries,
                  std::vector<std::shared_ptr<Layer>>* embeddings,
                  const std::shared_ptr<GPUResource> gpu_resource,
                  std::shared_ptr<GeneralBuffer2<CudaAllocator>>& blobs_buff);
};

template <typename TypeKey>
struct create_datareader {
  void operator()(const nlohmann::json& j,
                  std::map<std::string, SparseInput<TypeKey>>& sparse_input_map,
                  std::vector<TensorEntry>* train_tensor_entries_list,
                  std::vector<TensorEntry>* evaluate_tensor_entries_list,
                  std::shared_ptr<IDataReader>& data_reader,
                  std::shared_ptr<IDataReader>& data_reader_eval, size_t batch_size,
                  size_t batch_size_eval, bool use_mixed_precision, bool repeat_dataset,
                  const std::shared_ptr<ResourceManager> resource_manager);
};

}  // namespace HugeCTR
