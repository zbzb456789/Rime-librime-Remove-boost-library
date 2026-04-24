//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2012-02-21 GONG Chen <chen.sst@gmail.com>
//
#include <time.h>
#include <rime/config.h>
#include <rime/deployer.h>
#include <rime/signature.h>

namespace rime {

bool Signature::Sign(Config* config, Deployer* deployer) {
  if (!config)
    return false;
  config->SetString(key_ + "/generator", generator_);
  time_t now = time(NULL);
  string time_str(ctime(&now));
  
  // 去除首尾的空白字符，替代 booost::trim
  time_str.erase(time_str.find_last_not_of(" \t\n\r\f\v") + 1);
  time_str.erase(0, time_str.find_first_not_of(" \t\n\r\f\v"));
  
  config->SetString(key_ + "/modified_time", time_str);
  config->SetString(key_ + "/distribution_code_name",
                    deployer->distribution_code_name);
  config->SetString(key_ + "/distribution_version",
                    deployer->distribution_version);
  config->SetString(key_ + "/rime_version", RIME_VERSION);
  return true;
}

}  // namespace rime
