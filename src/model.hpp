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

struct ModelFeatures {
    float count_total;
    float ewma_2;
    float ewma_5;
    float ewma_20;
    float ewma_100;
    float count_above_mean;
    float count_below_mean;
    float max_group_ewma_5;
    float max_group_ewma_5_max__2;
    float max_group_ewma_5_max__1;
    float max_group_ewma_5_max_1;
    float max_group_ewma_5_max_2;
    float mean_group_ewma_5;
    float mean_group_ewma_5_mean__2;
    float mean_group_ewma_5_mean__1;
    float mean_group_ewma_5_mean_1;
    float mean_group_ewma_5_mean_2;
    ModelFeatures(const float &count_total, const float &ewma_2,
                  const float &ewma_5, const float &ewma_20,
                  const float &ewma_100, const float &count_above_mean,
                  const float &count_below_mean, const float &max_group_ewma_5,
                  const float &max_group_ewma_5_max__2,
                  const float &max_group_ewma_5_max__1,
                  const float &max_group_ewma_5_max_1,
                  const float &max_group_ewma_5_max_2,
                  const float &mean_group_ewma_5,
                  const float &mean_group_ewma_5_mean__2,
                  const float &mean_group_ewma_5_mean__1,
                  const float &mean_group_ewma_5_mean_1,
                  const float &mean_group_ewma_5_mean_2)
        : count_total(count_total), ewma_2(ewma_2), ewma_5(ewma_5),
          ewma_20(ewma_20), ewma_100(ewma_100),
          count_above_mean(count_above_mean),
          count_below_mean(count_below_mean),
          max_group_ewma_5(max_group_ewma_5),
          max_group_ewma_5_max__2(max_group_ewma_5_max__2),
          max_group_ewma_5_max__1(max_group_ewma_5_max__1),
          max_group_ewma_5_max_1(max_group_ewma_5_max_1),
          max_group_ewma_5_max_2(max_group_ewma_5_max_2),
          mean_group_ewma_5(mean_group_ewma_5),
          mean_group_ewma_5_mean__2(mean_group_ewma_5_mean__2),
          mean_group_ewma_5_mean__1(mean_group_ewma_5_mean__1),
          mean_group_ewma_5_mean_1(mean_group_ewma_5_mean_1),
          mean_group_ewma_5_mean_2(mean_group_ewma_5_mean__2) {}
};

struct ServingModel {
    inline const std::vector<float> &
    Predict(const std::vector<ModelFeatures> &Xs, const float &count_total);

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
    serving_api::NumericalFeatureId feature_Count_Total;
    serving_api::NumericalFeatureId feature_EWMA_2;
    serving_api::NumericalFeatureId feature_EWMA_5;
    serving_api::NumericalFeatureId feature_EWMA_20;
    serving_api::NumericalFeatureId feature_EWMA_100;
    serving_api::NumericalFeatureId feature_Count_Above_Mean;
    serving_api::NumericalFeatureId feature_Count_Below_Mean;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_5;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_5_Max__2;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_5_Max__1;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_5_Max_1;
    serving_api::NumericalFeatureId feature_Max_Group_EWMA_5_Max_2;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_5;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_5_Mean__2;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_5_Mean__1;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_5_Mean_1;
    serving_api::NumericalFeatureId feature_Mean_Group_EWMA_5_Mean_2;

    // Array of all feature IDs for convenience.
    std::vector<serving_api::NumericalFeatureId> all_features;

    int64_t examples_allocated = -1;
    std::unique_ptr<yggdrasil_decision_forests::serving::AbstractExampleSet>
        examples;
};

inline const std::vector<float> &
ServingModel::Predict(const std::vector<ModelFeatures> &Xs,
                      const float &count_total) {

    const int num_examples = Xs.size();
    if (examples_allocated != num_examples) {

        examples = engine->AllocateExamples(num_examples);
        examples->FillMissing(*features);
        examples_allocated = num_examples;
    }

    for (int i = 0; i < num_examples; ++i) {
        if (feature_Count_Total.index != -1)
            examples->SetNumerical(i, feature_Count_Total, Xs[i].count_total,
                                   *features);
        if (feature_EWMA_2.index != -1)
            examples->SetNumerical(i, feature_EWMA_2, Xs[i].ewma_2, *features);
        if (feature_EWMA_5.index != -1)
            examples->SetNumerical(i, feature_EWMA_5, Xs[i].ewma_5, *features);
        if (feature_EWMA_20.index != -1)
            examples->SetNumerical(i, feature_EWMA_20, Xs[i].ewma_20,
                                   *features);
        if (feature_EWMA_100.index != -1)
            examples->SetNumerical(i, feature_EWMA_100, Xs[i].ewma_100,
                                   *features);
        if (feature_Count_Above_Mean.index != -1)
            examples->SetNumerical(i, feature_Count_Above_Mean,
                                   Xs[i].count_above_mean, *features);
        if (feature_Count_Below_Mean.index != -1)
            examples->SetNumerical(i, feature_Count_Below_Mean,
                                   Xs[i].count_below_mean, *features);
        if (feature_Max_Group_EWMA_5.index != -1)
            examples->SetNumerical(i, feature_Max_Group_EWMA_5,
                                   Xs[i].max_group_ewma_5, *features);
        if (feature_Max_Group_EWMA_5_Max__2.index != -1)
            examples->SetNumerical(i, feature_Max_Group_EWMA_5_Max__2,
                                   Xs[i].max_group_ewma_5_max__2, *features);
        if (feature_Max_Group_EWMA_5_Max__1.index != -1)
            examples->SetNumerical(i, feature_Max_Group_EWMA_5_Max__1,
                                   Xs[i].max_group_ewma_5_max__1, *features);
        if (feature_Max_Group_EWMA_5_Max_1.index != -1)
            examples->SetNumerical(i, feature_Max_Group_EWMA_5_Max_1,
                                   Xs[i].max_group_ewma_5_max_1, *features);
        if (feature_Max_Group_EWMA_5_Max_2.index != -1)
            examples->SetNumerical(i, feature_Max_Group_EWMA_5_Max_2,
                                   Xs[i].max_group_ewma_5_max_2, *features);
        if (feature_Mean_Group_EWMA_5.index != -1)
            examples->SetNumerical(i, feature_Mean_Group_EWMA_5,
                                   Xs[i].mean_group_ewma_5, *features);
        if (feature_Mean_Group_EWMA_5_Mean__2.index != -1)
            examples->SetNumerical(i, feature_Mean_Group_EWMA_5_Mean__2,
                                   Xs[i].mean_group_ewma_5_mean__2, *features);
        if (feature_Mean_Group_EWMA_5_Mean__1.index != -1)
            examples->SetNumerical(i, feature_Mean_Group_EWMA_5_Mean__1,
                                   Xs[i].mean_group_ewma_5_mean__1, *features);
        if (feature_Mean_Group_EWMA_5_Mean_1.index != -1)
            examples->SetNumerical(i, feature_Mean_Group_EWMA_5_Mean_1,
                                   Xs[i].mean_group_ewma_5_mean_1, *features);
        if (feature_Mean_Group_EWMA_5_Mean_2.index != -1)
            examples->SetNumerical(i, feature_Mean_Group_EWMA_5_Mean_2,
                                   Xs[i].mean_group_ewma_5_mean_2, *features);
    }

    engine->Predict(*examples, num_examples, &predictions);

    // Normalize the predictions by the count_total.
    for (auto &pred : predictions) {
        // pred *= count_total;
    }

    return predictions;
}

inline void try_assign_feature(ServingModel *m,
                               serving_api::NumericalFeatureId &feature,
                               const absl::string_view &name) {

    if (m->features->HasInputFeature(name)) {
        feature = m->features->GetNumericalFeatureId(name).value();
    } else {

        feature = serving_api::NumericalFeatureId{-1};
    }
    m->all_features.push_back(feature);
}

inline absl::StatusOr<ServingModel *> Load(const absl::string_view &path) {
    static ServingModel *m = NULL;
    int x;

    if (m == NULL) {
        m = new ServingModel();

        // Load the model
        ASSIGN_OR_RETURN(auto model, serving_api::LoadModel(path));

        // Compile the model into an inference engine.
        ASSIGN_OR_RETURN(m->engine, model->BuildFastEngine());

        // Index the input features of the model.
        m->features = &(m->engine->features());

        // Index the input features.

        try_assign_feature(m, m->feature_Count_Total, "Count Total");

        try_assign_feature(m, m->feature_EWMA_2, "EWMA_2");
        try_assign_feature(m, m->feature_EWMA_5, "EWMA_5");
        try_assign_feature(m, m->feature_EWMA_20, "EWMA_20");
        try_assign_feature(m, m->feature_EWMA_100, "EWMA_100");
        try_assign_feature(m, m->feature_Count_Above_Mean, "Count Above Mean");
        try_assign_feature(m, m->feature_Count_Below_Mean, "Count Below Mean");
        try_assign_feature(m, m->feature_Max_Group_EWMA_5, "Max Group EWMA_5");

        try_assign_feature(m, m->feature_Max_Group_EWMA_5_Max__2,
                           "Max Group EWMA_5 Max -2");
        try_assign_feature(m, m->feature_Max_Group_EWMA_5_Max__1,
                           "Max Group EWMA_5 Max -1");
        try_assign_feature(m, m->feature_Max_Group_EWMA_5_Max_1,
                           "Max Group EWMA_5 Max 1");
        try_assign_feature(m, m->feature_Max_Group_EWMA_5_Max_2,
                           "Max Group EWMA_5 Max 2");
        try_assign_feature(m, m->feature_Mean_Group_EWMA_5,
                           "Mean Group EWMA_5");
        try_assign_feature(m, m->feature_Mean_Group_EWMA_5_Mean__2,
                           "Mean Group EWMA_5 Mean -2");
        try_assign_feature(m, m->feature_Mean_Group_EWMA_5_Mean__1,
                           "Mean Group EWMA_5 Mean -1");
        try_assign_feature(m, m->feature_Mean_Group_EWMA_5_Mean_1,
                           "Mean Group EWMA_5 Mean 1");
        try_assign_feature(m, m->feature_Mean_Group_EWMA_5_Mean_2,
                           "Mean Group EWMA_5 Mean 2");
    }

    return m;
}

} // namespace exported_model
} // namespace yggdrasil_decision_forests

#endif // YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
