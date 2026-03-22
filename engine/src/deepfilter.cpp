#include "noise/deepfilter.h"
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <cstdio>
#include <cstring>

static constexpr int64_t FRAME_SIZE = 480;
static constexpr int64_t STATE_SIZE = 45304;

namespace noise {

struct DeepFilterEngine::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "DeepFilterNet"};
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memory_info{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};

    // Recurrent state carried across frames
    std::vector<float> states;

    // Attenuation limit in dB. 0.0 = no limit (maximum noise suppression).
    // Voice recovery is handled via dry/wet mix post-processing in Engine.
    float atten_lim_db = 0.0f;

    // Cached input/output names (determined at model load time)
    std::vector<std::string> input_names_str;
    std::vector<std::string> output_names_str;
    std::vector<const char*> input_names;
    std::vector<const char*> output_names;

    bool loaded = false;

    Impl() : states(STATE_SIZE, 0.0f) {}
};

DeepFilterEngine::DeepFilterEngine()
    : impl_(std::make_unique<Impl>())
{
}

DeepFilterEngine::~DeepFilterEngine() = default;

bool DeepFilterEngine::load(const std::string& model_path)
{
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        impl_->session = std::make_unique<Ort::Session>(
            impl_->env, model_path.c_str(), opts);

        // Query and cache input names
        Ort::AllocatorWithDefaultOptions allocator;
        size_t num_inputs = impl_->session->GetInputCount();
        impl_->input_names_str.clear();
        impl_->input_names.clear();
        fprintf(stderr, "[NoiseAI-DeepFilter] Model inputs (%zu):\n", num_inputs);
        for (size_t i = 0; i < num_inputs; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, allocator);
            fprintf(stderr, "  [%zu] %s", i, name.get());
            auto type_info = impl_->session->GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            fprintf(stderr, " shape=[");
            for (size_t j = 0; j < shape.size(); ++j) {
                fprintf(stderr, "%s%lld", j > 0 ? "," : "", shape[j]);
            }
            fprintf(stderr, "]\n");
            impl_->input_names_str.emplace_back(name.get());
        }

        // Query and cache output names
        size_t num_outputs = impl_->session->GetOutputCount();
        impl_->output_names_str.clear();
        impl_->output_names.clear();
        fprintf(stderr, "[NoiseAI-DeepFilter] Model outputs (%zu):\n", num_outputs);
        for (size_t i = 0; i < num_outputs; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, allocator);
            fprintf(stderr, "  [%zu] %s", i, name.get());
            auto type_info = impl_->session->GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            fprintf(stderr, " shape=[");
            for (size_t j = 0; j < shape.size(); ++j) {
                fprintf(stderr, "%s%lld", j > 0 ? "," : "", shape[j]);
            }
            fprintf(stderr, "]\n");
            impl_->output_names_str.emplace_back(name.get());
        }

        // Build const char* arrays for Run()
        for (const auto& s : impl_->input_names_str) {
            impl_->input_names.push_back(s.c_str());
        }
        for (const auto& s : impl_->output_names_str) {
            impl_->output_names.push_back(s.c_str());
        }

        // Initialize state to zeros
        reset_state();

        impl_->loaded = true;
        fprintf(stderr, "[NoiseAI-DeepFilter] Model loaded successfully from %s\n",
                model_path.c_str());
        return true;
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "[NoiseAI-DeepFilter] ONNX Runtime error: %s\n", e.what());
        impl_->loaded = false;
        return false;
    }
}

bool DeepFilterEngine::process_frame(const float* input, float* output)
{
    if (!impl_->loaded || !impl_->session) {
        return false;
    }

    try {
        // Prepare input tensors
        std::array<int64_t, 1> input_frame_shape = {FRAME_SIZE};
        std::array<int64_t, 1> states_shape = {STATE_SIZE};
        std::array<int64_t, 1> atten_shape = {1};

        // Create input tensors - input_frame is const, so we need a copy
        std::vector<float> input_copy(input, input + FRAME_SIZE);

        Ort::Value input_tensors[] = {
            Ort::Value::CreateTensor<float>(
                impl_->memory_info,
                input_copy.data(), FRAME_SIZE,
                input_frame_shape.data(), input_frame_shape.size()),
            Ort::Value::CreateTensor<float>(
                impl_->memory_info,
                impl_->states.data(), STATE_SIZE,
                states_shape.data(), states_shape.size()),
            Ort::Value::CreateTensor<float>(
                impl_->memory_info,
                &impl_->atten_lim_db, 1,
                atten_shape.data(), atten_shape.size()),
        };

        // Run inference
        auto output_tensors = impl_->session->Run(
            Ort::RunOptions{nullptr},
            impl_->input_names.data(),
            input_tensors,
            impl_->input_names.size(),
            impl_->output_names.data(),
            impl_->output_names.size());

        // Copy enhanced audio to output buffer
        const float* enhanced = output_tensors[0].GetTensorData<float>();
        std::memcpy(output, enhanced, FRAME_SIZE * sizeof(float));

        // Copy new states back for next frame
        const float* new_states = output_tensors[1].GetTensorData<float>();
        std::memcpy(impl_->states.data(), new_states, STATE_SIZE * sizeof(float));

        return true;
    } catch (const Ort::Exception& e) {
        // Log only occasionally to avoid flooding stderr
        static uint32_t error_count = 0;
        if (error_count < 5 || (error_count % 1000) == 0) {
            fprintf(stderr, "[NoiseAI-DeepFilter] Inference error (#%u): %s\n",
                    error_count, e.what());
        }
        ++error_count;
        // On error, pass through silence to avoid noise
        std::memset(output, 0, FRAME_SIZE * sizeof(float));
        return false;
    }
}

void DeepFilterEngine::reset_state()
{
    std::fill(impl_->states.begin(), impl_->states.end(), 0.0f);
}

void DeepFilterEngine::set_attenuation_limit(float db)
{
    impl_->atten_lim_db = db;
    fprintf(stderr, "[NoiseAI-DeepFilter] Attenuation limit set to %.1f dB\n", db);
}

bool DeepFilterEngine::is_loaded() const
{
    return impl_->loaded;
}

} // namespace noise
