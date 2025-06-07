#ifndef YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
#define YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba

#include <memory>
#include <vector>

#include "groups.h"

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

struct group_features {
    float max;
    float mean;
};

struct group_feature_ids {
    serving_api::NumericalFeatureId max;
    serving_api::NumericalFeatureId mean;
};

struct ModelFeatures {
    float count_total;
    float ewma_2;
    float ewma_5;
    float ewma_20;
    float ewma_100;
    float count_above_mean;
    float count_below_mean;
    struct group_features group_ewma_5;
    std::array<struct group_features, 4> group_ewma_5_above;
    std::array<struct group_features, 4> group_ewma_5_below;

    ModelFeatures(const float &count_total, const float &ewma_2,
                  const float &ewma_5, const float &ewma_20,
                  const float &ewma_100, const float &count_above_mean,
                  const float &count_below_mean,
                  const std::array<struct page_group *, 4> &above,
                  const struct page_group *eq,
                  const std::array<struct page_group *, 4> &below)
        : count_total(count_total), ewma_2(ewma_2), ewma_5(ewma_5),
          ewma_20(ewma_20), ewma_100(ewma_100),
          count_above_mean(count_above_mean),
          count_below_mean(count_below_mean) {
        this->group_ewma_5.max = eq ? eq->max : 0;
        this->group_ewma_5.mean = eq ? eq->avg : 0;
        for (int i = 0; i < group_ewma_5_above.size(); i++) {
            this->group_ewma_5_above[i].max = above[i] ? above[i]->max : 0;
            this->group_ewma_5_above[i].mean = above[i] ? above[i]->avg : 0;
        }
        for (int i = 0; i < group_ewma_5_below.size(); i++) {
            this->group_ewma_5_below[i].max = below[i] ? below[i]->max : 0;
            this->group_ewma_5_below[i].mean = below[i] ? below[i]->avg : 0;
        }
    }
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

    group_feature_ids feature_group_EWMA_5;
    std::array<group_feature_ids, 4> feature_group_EWMA_5_Above;
    std::array<group_feature_ids, 4> feature_group_EWMA_5_Below;

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

        if (feature_group_EWMA_5.max.index != -1)
            examples->SetNumerical(i, feature_group_EWMA_5.max,
                                   Xs[i].group_ewma_5.max, *features);
        if (feature_group_EWMA_5.mean.index != -1)
            examples->SetNumerical(i, feature_group_EWMA_5.mean,
                                   Xs[i].group_ewma_5.mean, *features);

        for (int j = 0; j < feature_group_EWMA_5_Above.size(); j++) {
            if (feature_group_EWMA_5_Above[j].max.index != -1)
                examples->SetNumerical(i, feature_group_EWMA_5_Above[j].max,
                                       Xs[i].group_ewma_5_above[j].max,
                                       *features);
            if (feature_group_EWMA_5_Above[j].mean.index != -1)
                examples->SetNumerical(i, feature_group_EWMA_5_Above[j].mean,
                                       Xs[i].group_ewma_5_above[j].mean,
                                       *features);
            if (feature_group_EWMA_5_Below[j].max.index != -1)
                examples->SetNumerical(i, feature_group_EWMA_5_Below[j].max,
                                       Xs[i].group_ewma_5_below[j].max,
                                       *features);
            if (feature_group_EWMA_5_Below[j].mean.index != -1)
                examples->SetNumerical(i, feature_group_EWMA_5_Below[j].mean,
                                       Xs[i].group_ewma_5_below[j].mean,
                                       *features);
        }
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
        std::cerr << "Feature '" << name
                  << "' not found in the model. Using default value of -1."
                  << std::endl;
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

        try_assign_feature(m, m->feature_group_EWMA_5.max, "Max Group EWMA_5");
        try_assign_feature(m, m->feature_group_EWMA_5.mean,
                           "Mean Group EWMA_5");

        for (int i = 0; i < m->feature_group_EWMA_5_Above.size(); i++) {
            try_assign_feature(m, m->feature_group_EWMA_5_Above[i].max,
                               "Max Group EWMA_5 " + std::to_string(i + 1));
            try_assign_feature(m, m->feature_group_EWMA_5_Above[i].mean,
                               "Mean Group EWMA_5 " + std::to_string(i + 1));
        }

        for (int i = 0; i < m->feature_group_EWMA_5_Below.size(); i++) {
            try_assign_feature(m, m->feature_group_EWMA_5_Below[i].max,
                               "Max Group EWMA_5 -" + std::to_string(i + 1));
            try_assign_feature(m, m->feature_group_EWMA_5_Below[i].mean,
                               "Mean Group EWMA_5 -" + std::to_string(i + 1));
        }
    }

    return m;
}

} // namespace exported_model
} // namespace yggdrasil_decision_forests

#endif // YGGDRASIL_DECISION_FORESTS_GENERATED_MODEL_sqlite_4GB_ycsba
