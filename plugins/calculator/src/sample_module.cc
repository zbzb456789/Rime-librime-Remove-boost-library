//
// Copyright RIME Developers
// Distributed under the BSD License
//
// 2014-01-04 GONG Chen <chen.sst@gmail.com>
//

#include <rime_api.h>
#include <rime/common.h>
#include <rime/registry.h>

#include "calculator_translator.h"

using namespace rime;

static void rime_calculator_initialize() {
  LOG(INFO) << "registering components from module 'calculator'.";
  Registry& r = Registry::instance();
  r.Register("calculator_translator", new Component<calculator::CalculatorTranslator>);
}

static void rime_calculator_finalize() {}

RIME_REGISTER_MODULE(calculator)
