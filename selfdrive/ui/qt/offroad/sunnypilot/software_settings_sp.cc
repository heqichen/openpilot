#include "selfdrive/ui/qt/offroad/sunnypilot/software_settings_sp.h"

SoftwarePanelSP::SoftwarePanelSP(QWidget *parent) : SoftwarePanel(parent) {
  // Get current model name and create new ButtonControl
  const auto current_model = GetModelName();
  currentModelLblBtn = new ButtonControl(tr("Driving Model"), tr("SELECT"), current_model);
  currentModelLblBtn->setValue(current_model);

  connect(&models_fetcher, &ModelsFetcher::downloadProgress, this, [this](const double progress) {
    handleDownloadProgress(progress, "driving");
  });

  connect(&nav_models_fetcher, &ModelsFetcher::downloadProgress, this, [this](const double progress) {
    handleDownloadProgress(progress, "navigation");
  });

  connect(&metadata_fetcher, &ModelsFetcher::downloadProgress, this, [this](const double progress) {
    handleDownloadProgress(progress, "metadata");
  });
  
  connect(&models_fetcher, &ModelsFetcher::downloadComplete, this, [this](const QByteArray&data, bool fromCache = false) {
    modelFromCache = fromCache;
    updateLabels();
  });
  
  connect(&nav_models_fetcher, &ModelsFetcher::downloadComplete, this, [this](const QByteArray&data, bool fromCache = false) {
    navModelFromCache = fromCache;
    updateLabels();
  });
  
  connect(&metadata_fetcher, &ModelsFetcher::downloadComplete, this, [this](const QByteArray&data, bool fromCache = false) {
    metadataFromCache = fromCache;
    updateLabels();
  });

  connect(&models_fetcher, &ModelsFetcher::downloadFailed, this, &SoftwarePanelSP::handleDownloadFailed);
  connect(&nav_models_fetcher, &ModelsFetcher::downloadFailed, this, &SoftwarePanelSP::handleDownloadFailed);
  connect(&metadata_fetcher, &ModelsFetcher::downloadFailed, this, &SoftwarePanelSP::handleDownloadFailed);


  // Connect click event from currentModelLblBtn to local slot
  connect(currentModelLblBtn, &ButtonControl::clicked, this, &SoftwarePanelSP::handleCurrentModelLblBtnClicked);

  ReplaceOrAddWidget(currentModelLbl, currentModelLblBtn);
}

void SoftwarePanelSP::handleDownloadFailed(const QString &modelType) {
  model_download_failed = true;
  LOGE("Download failed reported for [%s]", modelType.toStdString().c_str());
  failed_downloads_description += "["+ modelType +"]\n";
  HandleModelDownloadProgressReport(); // Call to update UI based on the new status
}

void SoftwarePanelSP::handleDownloadProgress(const double progress, const QString &modelType) {
  if (modelType == "driving") {
    modelDownloadProgress = progress;
  } else if (modelType == "navigation") {
    navModelDownloadProgress = progress;
  } else if (modelType == "metadata") {
    metadataDownloadProgress = progress;
  }
  
  HandleModelDownloadProgressReport();
}

QString SoftwarePanelSP::GetModelName() {
  if (selectedModelToDownload.has_value()) {
    return selectedModelToDownload->displayName;
  }

  if (params.getBool("CustomDrivingModel")) {
    return QString::fromStdString(params.get("DrivingModelName"));
  }

  return CURRENT_MODEL;
}

QString SoftwarePanelSP::GetNavModelName() {
  if (selectedNavModelToDownload.has_value()) {
    return selectedNavModelToDownload->fullNameNav;
  }

  return QString::fromStdString(params.get("NavModelText"));
}

QString SoftwarePanelSP::GetMetadataName() {
  if (selectedMetadataToDownload.has_value()) {
    // Assuming your metadata structure has a 'name' or similar field
    return selectedMetadataToDownload->fullNameMetadata;
  }

  // Return a default name or an empty string if there's no metadata selected
  return QString::fromStdString(params.get("ModelMetadataText"));
}

void SoftwarePanelSP::HandleModelDownloadProgressReport() {
  // Template strings for download status
  const QString downloadingTemplate = "Downloading %1 model [%2]... (%3%)";
  const QString downloadedTemplate = "%1 model [%2] downloaded";

  // Get model names
  QString drivingModelName = GetModelName();
  QString navModelName = GetNavModelName();
  QString metadataName = GetMetadataName();
  QString description;

  // Driving model status
  if (isDownloadingModel()) {
    description += downloadingTemplate.arg("Driving", drivingModelName, QString::number(modelDownloadProgress.value_or(0.0), 'f', 2));
  } else {
    if (modelFromCache) drivingModelName += " (CACHED)";
    description += downloadedTemplate.arg("Driving", drivingModelName);
  }

  // Navigation model status
  if (isDownloadingNavModel()) {
    if (!description.isEmpty()) description += "\n"; // Add newline if driving model status is already appended
    description += downloadingTemplate.arg("Navigation", navModelName,
                                           QString::number(navModelDownloadProgress.value_or(0.0), 'f', 2));
  } else {
    if (navModelFromCache) navModelName += " (CACHED)";
    if (!description.isEmpty()) description += "\n"; // Ensure newline separation
    description += downloadedTemplate.arg("Navigation", navModelName);
  }

  if (isDownloadingMetadata()) {
    if (!description.isEmpty()) description += "\n";
    description += downloadingTemplate.arg("Metadata", metadataName, QString::number(metadataDownloadProgress.value_or(0.0), 'f', 2));
  } else {
    if (metadataFromCache) metadataName += " (CACHED)";
    if (!description.isEmpty()) description += "\n";
    description += downloadedTemplate.arg("Metadata", metadataName);
  }

  if (model_download_failed) {
    description = "Downloads have failed, please try swapping the model!\n"
                  "Failed:\n" + failed_downloads_description;
    LOGE("MODEL DOWNLOADS FAILED!!!");
  }

  currentModelLblBtn->setDescription(description);
  currentModelLblBtn->showDescription();
  currentModelLblBtn->setEnabled(
      !(is_onroad || (isDownloadingModel() || isDownloadingMetadata() || isDownloadingNavModel())));

  // If not downloading and there is a selected model, update parameters
  if (!isDownloadingModel() && modelDownloadProgress.has_value()) {
    params.put("DrivingModelText", selectedModelToDownload->fullName.toStdString());
    params.put("DrivingModelName", selectedModelToDownload->displayName.toStdString());
    //params.put("DrivingModelUrl", selectedModelToDownload->downloadUri.toStdString());  // TODO: Placeholder for future implementation
    LOGD("Resetting selectedModelToDownload");
    selectedModelToDownload.reset();
    modelDownloadProgress.reset();
    modelFromCache = false;
    params.putBool("CustomDrivingModel", !model_download_failed);
  }

  // If not downloading and there is a selected model, update parameters
  if (!isDownloadingNavModel() && navModelDownloadProgress.has_value()) {
    params.put("DrivingModelGeneration", selectedNavModelToDownload->generation.toStdString());
    params.put("NavModelText", selectedNavModelToDownload->fullNameNav.toStdString());
    LOGD("Resetting selectedNavModelToDownload");
    selectedNavModelToDownload.reset();
    navModelDownloadProgress.reset();
    navModelFromCache = false;
  }

  if (!isDownloadingMetadata() && metadataDownloadProgress.has_value()) {
    params.put("ModelMetadataText", selectedMetadataToDownload->fullNameMetadata.toStdString());
    LOGD("Resetting selectedMetadataToDownload");
    selectedMetadataToDownload.reset();
    metadataDownloadProgress.reset();
    metadataFromCache = false;
  }

}

void SoftwarePanelSP::handleCurrentModelLblBtnClicked() {
  // Disabling label button and displaying fetching message
  currentModelLblBtn->setEnabled(false);
  currentModelLblBtn->setValue("Fetching models...");

  checkNetwork();
  const auto currentModelName = QString::fromStdString(params.get("DrivingModelName"));
  const bool is_release_sp = params.getBool("IsReleaseSPBranch");
  const auto models = ModelsFetcher::getModelsFromURL();

  QMap<QString, QString> index_to_model;

  // Collecting indices with display names
  for (const auto &model: models) {
    if ((is_release_sp && model.environment == "release") || !is_release_sp) {
      index_to_model.insert(model.index, model.displayName);
    }
  }

  QStringList modelNames;
  QStringList indices = index_to_model.keys();
  std::sort(indices.begin(), indices.end(), [&](const QString &index1, const QString &index2) {
    return index1.toInt() > index2.toInt();
  });

  for (const QString &index: indices) {
    modelNames.push_back(index_to_model[index]);
  }

  currentModelLblBtn->setEnabled(!is_onroad);
  currentModelLblBtn->setValue(GetModelName());

  const QString selectedModelName = MultiOptionDialog::getSelection(tr("Select a Driving Model"), modelNames,
                                                                    currentModelName, this);

  // Bail if no selected model or the user doesn't want to continue while on metered
  if (selectedModelName.isEmpty() || !canContinueOnMeteredDialog()) {
    return;
  }

  // Finding and setting the selected model
  for (auto &model: models) {
    if (model.displayName == selectedModelName) {
      selectedModelToDownload = model;
      selectedNavModelToDownload = model;
      selectedMetadataToDownload = model;
      params.putBool("CustomDrivingModel", false);
      break;
    }
  }

  // If decision is to download and there is a selected model, update UI and begin downloading
  if (selectedModelToDownload.has_value()) {
    model_download_failed = false;
    currentModelLblBtn->setValue(selectedModelToDownload->displayName);
    currentModelLblBtn->setDescription(selectedModelToDownload->displayName);
    models_fetcher.download(selectedModelToDownload->downloadUri, selectedModelToDownload->fileName);
    nav_models_fetcher.download(selectedNavModelToDownload->downloadUriNav, selectedNavModelToDownload->fileNameNav);
    metadata_fetcher.download(selectedMetadataToDownload->downloadUriMetadata,
                              selectedMetadataToDownload->fileNameMetadata);

    // Disable select button until download completes
    currentModelLblBtn->setEnabled(false);
    showResetParamsDialog(); 
  }
  updateLabels();
}

void SoftwarePanelSP::checkNetwork() {
  const SubMaster &sm = *(uiState()->sm);
  const auto device_state = sm["deviceState"].getDeviceState();
  const auto network_type = device_state.getNetworkType();
  is_wifi = network_type == cereal::DeviceState::NetworkType::WIFI;
  is_metered = device_state.getNetworkMetered();
}

void SoftwarePanelSP::updateLabels() {
  if (!isVisible()) {
    return;
  }
  
  if(!model_download_failed)
    failed_downloads_description = "";

  checkNetwork();
  currentModelLblBtn->setEnabled(!(is_onroad || (isDownloadingModel() || isDownloadingMetadata() || isDownloadingNavModel())));
  SoftwarePanel::updateLabels();
}

void SoftwarePanelSP::showResetParamsDialog() {
  const auto confirmMsg = tr(
      "Download has started in the background.\nWe STRONGLY suggest you to reset calibration, would you like to do that now?");
  const auto button_text = tr("Reset Calibration");

  // If user confirms, remove specified parameters
  if (showConfirmationDialog(confirmMsg, button_text, false)) {
    params.remove("CalibrationParams");
    params.remove("LiveTorqueParameters");
  }
}