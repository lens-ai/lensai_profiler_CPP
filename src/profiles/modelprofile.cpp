#include "modelprofile.h"
#include "iniparser.h"
#include "parser_factory.h"

ModelProfile::~ModelProfile() {
    delete saver;
    for (const auto& pair : model_classes_stat_) {
        delete pair.second;
    }
    delete sketch1;
    delete model_embeddings;
}

/**
 * @class ModelProfile
 * @brief Class for computing and managing various model statistics
 */

/**
 * @brief ModelProfile class constructor
 * @param model_id Unique identifier for the model
 * @param no_of_classes Number of classes the model predicts
 * @param saver Reference to a Saver object used for saving model statistics
 */
ModelProfile::ModelProfile(std::string model_id, std::string conf_path,
	       	int save_interval, int top_classes): saver(new Saver(save_interval, "ModelProfile")){
    std::string endpointUrl="";
    std::string token="";

  // Set member variables
  model_id_ = model_id;
  IniParser parser;
  modelConfig = parser.parseIniFile(conf_path,
                          "model", "");
  statSavepath = modelConfig["filepath"][0];
  dataSavepath = modelConfig["filepath"][1];
  createFolderIfNotExists(statSavepath, dataSavepath);
  top_classes_ = top_classes;
  sketch1 = new frequent_class_sketch(64);
  model_embeddings = new distributionBox(200);
  registerStatistics();
  saver->StartSaving();
#ifndef TEST
    /*int uploadtype=0;
    s3_client_config_t s3_client_config; 
    std::string bucketName;
    std::string objectKey;
    //uploader = new ImageUploader(uploadtype, endpointUrl, token, s3_client_config);
    //uploader->startUploadThread(filesSavePath, bucketName, objectKey, interval);*/
#endif
}


// Public accessor to get the number of distribution boxes
int ModelProfile::getNumDistributionBoxes() const {
        return top_classes_;
    }

// Register Model embeddings saver
void ModelProfile::registerStatistics(){
   saver->AddObjectToSave((void*)(model_embeddings), KLL_TYPE, statSavepath + "embeddings.bin");
}

/**
 * @brief Logs classification model statistics
 * @param inference_latency Time taken for model inference
 * @param results Reference to the classification results
 * @return 0 on success, negative value on error
 *
 * This function iterates through the provided results and logs statistics for the most frequent classes.
 * It updates the `model_classes_stat` map with scores for each class.
 */
int ModelProfile::log_classification_model_stats(float inference_latency __attribute__((unused)),
	       	const ClassificationResults& results) {
    for (const auto& result : results) {
        int cls = result.second;
        float score = result.first;
        auto it = model_classes_stat_.find(cls);
        if (it != model_classes_stat_.end()) {
            // Key exists, update the value
            it->second->update(score);
        } else {
            // Key does not exist, add the key-value pair
            dBox = new distributionBox(200);
            model_classes_stat_[cls] = dBox;
            model_classes_stat_[cls]->update(score);
            saver->AddObjectToSave((void *)(dBox), KLL_TYPE,
                                   statSavepath + model_id_ + std::to_string(cls) + ".bin");  // Register with Saver for saving
        }
        sketch1->update(std::to_string(cls));  // Placeholder for storing frequent class IDs
    }
  return 0; // Assuming successful logging, replace with error handling if needed
}

/**
 * @brief Logs embeddings 
 */
int ModelProfile::log_embeddings(const std::vector<float>& embeddings) {
    for (const auto& value : embeddings) {
        model_embeddings->update(value);
    }
    return 1;
}

/**
 * @brief Computes and logs selected image statistics
 * @param img OpenCV image matrix
 * @param save_sample Flag indicating whether to save samples exceeding thresholds
 * @return 1 on success, error code on failure
 */
int ModelProfile::log_embeddings(const std::vector<float>& embeddings, int cls) {
    // Get the distributionBox for the given statistic name
    distributionBox* cls_dBox = getBox(cls);    

    // Update the distributionBox with the given value
    for (const auto& value : embeddings) {
        cls_dBox->update(value);
    }
    return 1; // Indicate success
}


/**
 * @brief Gets the distributionBox (KLL sketch) for the given statistic name.
 * If it doesn't exist, a new one is created and added to the map.
 * @param name Statistic name
 * @return Pointer to the distributionBox object
 */
distributionBox* ModelProfile::getBox(const int cls) {

    // Check if the distributionBox exists in the map
    if (embeddings_stat_.find(cls) == embeddings_stat_.end()) {
        // If not, create a new distributionBox and add it to the map
        embeddings_stat_[cls] = new distributionBox();
        
        // Also register the new box for saving
        saver->AddObjectToSave((void*)(embeddings_stat_[cls]), KLL_TYPE, statSavepath + std::to_string(cls) + "_embedding.bin");
    }

    // Return the distributionBox
    return embeddings_stat_[cls];
}
