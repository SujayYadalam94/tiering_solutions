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

namespace yggdrasil_decision_forests {
namespace exported_model {

struct ServingModel {
    inline const std::vector<float> &
    Predict(const std::vector<std::array<float, 13>> &Xs);

    static inline std::array<float, 13> fill_features(
        const float &ewma_2, const float ewma_20, const float &ewma_100,
        const float &max_group_ewma_2, const float &max_group_ewma_2_max__2,
        const float &max_group_ewma_2_max__1,
        const float &max_group_ewma_2_max_1,
        const float &max_group_ewma_2_max_2, const float &mean_group_ewma_2,
        const float &mean_group_ewma_2_mean__2,
        const float &mean_group_ewma_2_mean__1,
        const float &mean_group_ewma_2_mean_1,
        const float &mean_group_ewma_2_mean_2) {
        return {ewma_2,
                ewma_20,
                ewma_100,
                max_group_ewma_2,
                max_group_ewma_2_max__2,
                max_group_ewma_2_max__1,
                max_group_ewma_2_max_1,
                max_group_ewma_2_max_2,
                mean_group_ewma_2,
                mean_group_ewma_2_mean__2,
                mean_group_ewma_2_mean__1,
                mean_group_ewma_2_mean_1,
                mean_group_ewma_2_mean_2};
    }

    // Compiled model.
    std::unique_ptr<serving_api::FastEngine> engine;

    // Index of the input features of the model
    // Non-owning pointer. The data is owned by the engine.
    const serving_api::FeaturesDefinition *features;
    std::vector<float> predictions;

    // Number of output predictions for each example.
    inline int NumPredictionDimension() const {
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

    int64_t examples_allocated = -1;
    std::unique_ptr<yggdrasil_decision_forests::serving::AbstractExampleSet>
        examples;
};

inline const std::vector<float> &
ServingModel::Predict(const std::vector<std::array<float, 13>> &Xs) {
    const int num_examples = Xs.size();
    if (examples_allocated != num_examples) {
        fflush(stdout);
        examples = engine->AllocateExamples(num_examples);
        examples->FillMissing(*features);
        examples_allocated = num_examples;
    }

    for (int i = 0; i < num_examples; ++i) {
        for (int j = 0; j < all_features.size(); ++j) {
            examples->SetNumerical(i, all_features[j], Xs[i][j], *features);
        }
    }
    engine->Predict(*examples, num_examples, &predictions);
    return predictions;
}

inline absl::StatusOr<ServingModel *> Load(const absl::string_view &path) {
    static ServingModel *m = NULL;

    if (m == NULL) {
        m = new ServingModel();

        // Load the model
        ASSIGN_OR_RETURN(auto model, serving_api::LoadModel(path));

        // Compile the model into an inference engine.
        ASSIGN_OR_RETURN(m->engine, model->BuildFastEngine());

        // Index the input features of the model.
        m->features = &(m->engine->features());

        // Index the input features.
        ASSIGN_OR_RETURN(m->feature_EWMA_2,
                         m->features->GetNumericalFeatureId("EWMA_2"));
        ASSIGN_OR_RETURN(m->feature_EWMA_20,
                         m->features->GetNumericalFeatureId("EWMA_20"));
        ASSIGN_OR_RETURN(m->feature_EWMA_100,
                         m->features->GetNumericalFeatureId("EWMA_100"));
        ASSIGN_OR_RETURN(
            m->feature_Max_Group_EWMA_2,
            m->features->GetNumericalFeatureId("Max Group EWMA_2"));
        ASSIGN_OR_RETURN(
            m->feature_Max_Group_EWMA_2_Max__2,
            m->features->GetNumericalFeatureId("Max Group EWMA_2 Max -2"));
        ASSIGN_OR_RETURN(
            m->feature_Max_Group_EWMA_2_Max__1,
            m->features->GetNumericalFeatureId("Max Group EWMA_2 Max -1"));
        ASSIGN_OR_RETURN(
            m->feature_Max_Group_EWMA_2_Max_1,
            m->features->GetNumericalFeatureId("Max Group EWMA_2 Max 1"));
        ASSIGN_OR_RETURN(
            m->feature_Max_Group_EWMA_2_Max_2,
            m->features->GetNumericalFeatureId("Max Group EWMA_2 Max 2"));
        ASSIGN_OR_RETURN(
            m->feature_Mean_Group_EWMA_2,
            m->features->GetNumericalFeatureId("Mean Group EWMA_2"));
        ASSIGN_OR_RETURN(
            m->feature_Mean_Group_EWMA_2_Mean__2,
            m->features->GetNumericalFeatureId("Mean Group EWMA_2 Mean -2"));
        ASSIGN_OR_RETURN(
            m->feature_Mean_Group_EWMA_2_Mean__1,
            m->features->GetNumericalFeatureId("Mean Group EWMA_2 Mean -1"));
        ASSIGN_OR_RETURN(
            m->feature_Mean_Group_EWMA_2_Mean_1,
            m->features->GetNumericalFeatureId("Mean Group EWMA_2 Mean 1"));
        ASSIGN_OR_RETURN(
            m->feature_Mean_Group_EWMA_2_Mean_2,
            m->features->GetNumericalFeatureId("Mean Group EWMA_2 Mean 2"));
        m->all_features = {m->feature_EWMA_2,
                           m->feature_EWMA_20,
                           m->feature_EWMA_100,
                           m->feature_Max_Group_EWMA_2,
                           m->feature_Max_Group_EWMA_2_Max__2,
                           m->feature_Max_Group_EWMA_2_Max__1,
                           m->feature_Max_Group_EWMA_2_Max_1,
                           m->feature_Max_Group_EWMA_2_Max_2,
                           m->feature_Mean_Group_EWMA_2,
                           m->feature_Mean_Group_EWMA_2_Mean__2,
                           m->feature_Mean_Group_EWMA_2_Mean__1,
                           m->feature_Mean_Group_EWMA_2_Mean_1,
                           m->feature_Mean_Group_EWMA_2_Mean_2};
    }

    return m;
}

} // namespace exported_model
} // namespace yggdrasil_decision_forests

#endif // YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
