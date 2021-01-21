/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "apexd"

#include "apex_preinstalled_data.h"

#include <unordered_map>

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/result.h>
#include <android-base/strings.h>

#include "apex_constants.h"
#include "apex_file.h"
#include "apexd_utils.h"

using android::base::Error;
using android::base::GetProperty;
using android::base::Result;

namespace android {
namespace apex {

Result<void> ApexPreinstalledData::ScanDir(const std::string& dir) {
  LOG(INFO) << "Scanning " << dir << " for preinstalled data";
  if (access(dir.c_str(), F_OK) != 0 && errno == ENOENT) {
    LOG(INFO) << dir << " does not exist. Skipping";
    return {};
  }

  Result<std::vector<std::string>> all_apex_files = FindFilesBySuffix(
      dir, {kApexPackageSuffix, kCompressedApexPackageSuffix});
  if (!all_apex_files.ok()) {
    return all_apex_files.error();
  }

  for (const auto& file : *all_apex_files) {
    Result<ApexFile> apex_file = ApexFile::Open(file);
    if (!apex_file.ok()) {
      return Error() << "Failed to open " << file << " : " << apex_file.error();
    }

    const std::string& name = apex_file->GetManifest().name();
    ApexData apex_data;
    apex_data.public_key = apex_file->GetBundledPublicKey();
    apex_data.path = apex_file->GetPath();

    auto it = data_.find(name);
    if (it == data_.end()) {
      data_[name] = apex_data;
    } else if (it->second.path != apex_data.path) {
      // Currently, on some -eng builds there are two art apexes on /system
      // partition. While this issue is not fixed, exempt art apex from the
      // duplicate check on -eng builds.
      // TODO(b/176497601): remove this exemption once issue with duplicate art
      // apex is resolved.
      std::string build_type = GetProperty("ro.build.type", "");
      auto level = build_type == "eng" && name == "com.android.art"
                       ? base::ERROR
                       : base::FATAL;
      LOG(level) << "Found two apex packages " << it->second.path << " and "
                 << apex_data.path << " with the same module name " << name;
    } else if (it->second.public_key != apex_data.public_key) {
      LOG(FATAL) << "Public key of apex package " << it->second.path << " ("
                 << name << ") has unexpectedly changed";
    }
  }
  return {};
}

ApexPreinstalledData& ApexPreinstalledData::GetInstance() {
  static ApexPreinstalledData instance;
  return instance;
}

Result<void> ApexPreinstalledData::Initialize(
    const std::vector<std::string>& dirs) {
  for (const auto& dir : dirs) {
    if (auto result = ScanDir(dir); !result.ok()) {
      return result.error();
    }
  }
  return {};
}

Result<const std::string> ApexPreinstalledData::GetPublicKey(
    const std::string& name) const {
  auto it = data_.find(name);
  if (it == data_.end()) {
    return Error() << "No preinstalled data found for package " << name;
  }
  return it->second.public_key;
}

Result<const std::string> ApexPreinstalledData::GetPreinstalledPath(
    const std::string& name) const {
  auto it = data_.find(name);
  if (it == data_.end()) {
    return Error() << "No preinstalled data found for package " << name;
  }
  return it->second.path;
}

bool ApexPreinstalledData::HasPreInstalledVersion(
    const std::string& name) const {
  return data_.find(name) != data_.end();
}

bool ApexPreinstalledData::IsPreInstalledApex(const ApexFile& apex) const {
  auto it = data_.find(apex.GetManifest().name());
  if (it == data_.end()) {
    return false;
  }
  return it->second.path == apex.GetPath();
}

}  // namespace apex
}  // namespace android
