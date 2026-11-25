#include "GLTFLoader.h"
#include <format>
#include <stdexcept>
#include <tiny_gltf_v3.h>
#include <memory>

namespace {
    auto LoadModel(const std::string& path) {
        tg3_parse_options opts;
        tg3_error_stack errors;
        tg3_model model;

        tg3_error_code err = tg3_parse_file(&model, &errors, path.c_str(), path.size(), &opts);
        if (err != TG3_OK) {
            std::string errorStr = {};
            for (uint32_t i = 0; i < errors.count; i++) {
                errorStr += std::format("[%d] %s\n", (int)errors.entries[i].severity,
                                        errors.entries[i].message ? errors.entries[i].message : "(null)") + "\n";
            }
            tg3_model_free(&model);
            tg3_error_stack_free(&errors);
            throw std::runtime_error(errorStr);
        }
        return model;
    }

    void ReleaseModel(tg3_model& model) {
        tg3_model_free(&model);
    }
}

namespace DemoTools::GLTF {
    std::unique_ptr<ModelData> GLTFLoader::Load(const std::string &str) {
        auto modelData = std::make_unique<ModelData>();
        auto gltfModel = LoadModel(str);

        return modelData;
    }
}
