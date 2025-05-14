// Automatically generated code running an Yggdrasil Decision Forests model in
// C++. This code was generated with "model.to_cpp()".
//
// Date of generation: 2025-05-13 16:38:40.460904
// YDF Version: 0.11.0
//
// How to use this code:
//
// 1. Copy this code in a new .h file.
// 2. If you use Bazel/Blaze, use the following dependencies:
//      //third_party/absl/status:statusor
//      //third_party/absl/strings
//      //external/ydf_cc/yggdrasil_decision_forests/api:serving
// 3. In your existing code, include the .h file. Make predictions as follows:
//   // Load the model (to do only once).
//   namespace ydf = yggdrasil_decision_forests;
//   const auto model = ydf::exported_model_123::Load(<path to model>);
//   // Run the model
//   predictions = model.Predict();
// 4. By default, the "Predict" function takes no inputs and creates fake
//   examples. In practice, you want to add your input data as arguments to
//   "Predict" and call "examples->Set..." functions accordingly.
// 5. (Bonus)
//   Allocate one `examples` and `predictions` vector per thread and reuse them
//   to speed-up the inference.
//
#ifndef YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
#define YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "yggdrasil_decision_forests/api/serving.h"
#include "yggdrasil_decision_forests/dataset/data_spec.h"
#include "yggdrasil_decision_forests/dataset/data_spec.pb.h"
#include "yggdrasil_decision_forests/dataset/data_spec_inference.h"
#include "yggdrasil_decision_forests/dataset/vertical_dataset_io.h"
#include "yggdrasil_decision_forests/learner/learner_library.h"
#include "yggdrasil_decision_forests/metric/metric.h"
#include "yggdrasil_decision_forests/metric/report.h"
#include "yggdrasil_decision_forests/model/model_library.h"
#include "yggdrasil_decision_forests/utils/filesystem.h"
#include "yggdrasil_decision_forests/utils/logging.h"

namespace yggdrasil_decision_forests
{
namespace exported_model_sqlite_4GB_ycsba
{

struct ServingModel
{
    std::vector<float> Predict(const std::vector<std::array<float, 13>> &Xs) const;

    // Compiled model.
    std::unique_ptr<serving_api::FastEngine> engine;

    // Index of the input features of the model.
    //
    // Non-owning pointer. The data is owned by the engine.
    const serving_api::FeaturesDefinition *features;

    // Number of output predictions for each example.
    // Equal to 1 for regression, ranking and binary classification with compact
    // format. Equal to the number of classes for classification.
    int NumPredictionDimension() const
    {
        return engine->NumPredictionDimension();
    }

    // Indexes of the input features.
    serving_api::NumericalFeatureId feature_EWMA_2;
    serving_api::NumericalFeatureId feature_EWMA_20;
    serving_api::NumericalFeatureId feature_EWMA_100;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_2;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_2_Max__2;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_2_Max__1;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_2_Max_1;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_2_Max_2;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_2;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_2_Mean__2;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_2_Mean__1;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_2_Mean_1;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_2_Mean_2;

    // Array of all feature IDs for convenience.
    std::array<serving_api::NumericalFeatureId, 13> all_features;
};

// TODO: Pass input feature values to "Predict".
inline std::vector<float> ServingModel::Predict(const std::vector<std::array<float, 13>> &Xs) const
{

    std::cout << feature_EWMA_2.index << std::endl;
    std::cout << feature_EWMA_20.index << std::endl;
    std::cout << feature_EWMA_100.index << std::endl;
    std::cout << feature_Max_Group_EWMA_2.index << std::endl;
    std::cout << feature_Max_Group_EWMA_2_Max__2.index << std::endl;
    std::cout << feature_Max_Group_EWMA_2_Max__1.index << std::endl;
    std::cout << feature_Max_Group_EWMA_2_Max_1.index << std::endl;
    std::cout << feature_Max_Group_EWMA_2_Max_2.index << std::endl;
    std::cout << feature_Mean_Group_EWMA_2.index << std::endl;
    std::cout << feature_Mean_Group_EWMA_2_Mean__2.index << std::endl;
    std::cout << feature_Mean_Group_EWMA_2_Mean__1.index << std::endl;
    std::cout << feature_Mean_Group_EWMA_2_Mean_1.index << std::endl;
    std::cout << feature_Mean_Group_EWMA_2_Mean_2.index << std::endl;

    const int num_examples = Xs.size();

    auto examples = engine->AllocateExamples(num_examples);
    for (int i = 0; i < num_examples; ++i)
    {
        examples->FillMissing(*features);
        for (int j = 0; j < all_features.size(); ++j)
        {
            examples->SetNumerical(i, all_features[j], Xs[i][j], *features);
        }
    }


    // TODO: Make this reuse prediction vector.
    std::vector<float> predictions;
    engine->Predict(*examples, num_examples, &predictions);
    return predictions;
}

inline absl::StatusOr<ServingModel> Load(absl::string_view path)
{

    ServingModel m;

    // Load the model
    ASSIGN_OR_RETURN(auto model, serving_api::LoadModel(path));

    // Compile the model into an inference engine.
    ASSIGN_OR_RETURN(m.engine, model->BuildFastEngine());

    // Index the input features of the model.
    m.features = &m.engine->features();

    // Index the input features.
    ASSIGN_OR_RETURN(m.feature_EWMA_2, m.features->GetNumericalFeatureId("EWMA_2"));
    ASSIGN_OR_RETURN(m.feature_EWMA_20, m.features->GetNumericalFeatureId("EWMA_20"));
    ASSIGN_OR_RETURN(m.feature_EWMA_100, m.features->GetNumericalFeatureId("EWMA_100"));
    ASSIGN_OR_RETURN(m.feature_Max_Group_EWMA_2, m.features->GetNumericalFeatureId("Max Group EWMA_2"));
    ASSIGN_OR_RETURN(m.feature_Max_Group_EWMA_2_Max__2, m.features->GetNumericalFeatureId("Max Group EWMA_2 Max -2"));
    ASSIGN_OR_RETURN(m.feature_Max_Group_EWMA_2_Max__1, m.features->GetNumericalFeatureId("Max Group EWMA_2 Max -1"));
    ASSIGN_OR_RETURN(m.feature_Max_Group_EWMA_2_Max_1, m.features->GetNumericalFeatureId("Max Group EWMA_2 Max 1"));
    ASSIGN_OR_RETURN(m.feature_Max_Group_EWMA_2_Max_2, m.features->GetNumericalFeatureId("Max Group EWMA_2 Max 2"));
    ASSIGN_OR_RETURN(m.feature_Mean_Group_EWMA_2, m.features->GetNumericalFeatureId("Mean Group EWMA_2"));
    ASSIGN_OR_RETURN(m.feature_Mean_Group_EWMA_2_Mean__2,
                     m.features->GetNumericalFeatureId("Mean Group EWMA_2 Mean -2"));
    ASSIGN_OR_RETURN(m.feature_Mean_Group_EWMA_2_Mean__1,
                     m.features->GetNumericalFeatureId("Mean Group EWMA_2 Mean -1"));
    ASSIGN_OR_RETURN(m.feature_Mean_Group_EWMA_2_Mean_1, m.features->GetNumericalFeatureId("Mean Group EWMA_2 Mean 1"));
    ASSIGN_OR_RETURN(m.feature_Mean_Group_EWMA_2_Mean_2, m.features->GetNumericalFeatureId("Mean Group EWMA_2 Mean 2"));

    m.all_features = {m.feature_EWMA_2,
                      m.feature_EWMA_20,
                      m.feature_EWMA_100,
                      m.feature_Max_Group_EWMA_2,
                      m.feature_Max_Group_EWMA_2_Max__2,
                      m.feature_Max_Group_EWMA_2_Max__1,
                      m.feature_Max_Group_EWMA_2_Max_1,
                      m.feature_Max_Group_EWMA_2_Max_2,
                      m.feature_Mean_Group_EWMA_2,
                      m.feature_Mean_Group_EWMA_2_Mean__2,
                      m.feature_Mean_Group_EWMA_2_Mean__1,
                      m.feature_Mean_Group_EWMA_2_Mean_1,
                      m.feature_Mean_Group_EWMA_2_Mean_2};

    return m;
}

} // namespace exported_model_sqlite_4GB_ycsba
} // namespace yggdrasil_decision_forests

#endif // YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
